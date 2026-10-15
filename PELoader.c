#define _CRT_SECURE_NO_WARNINGS
#include "PELoader.h"
#include "PETools.h" // 复用你现有的工具
#include <stdio.h>
#include <windows.h> // VirtualAlloc
// 全局记录，用于释放
static void* g_CodeMemory = NULL;
 void* g_StackMemory = NULL;//全局变量，传递分配的栈地址
// ---------------------------------------------------------
// 通用 CPU 初始化 (设置 EIP, ESP, 段寄存器)
// ---------------------------------------------------------
static bool InitCpuCommon(CPU_Context* ctx, uint32_t entryPoint, uint32_t stackSize) {
    if (stackSize == 0) stackSize = 1024 * 64; // 默认 64KB
    // 1. 分配堆栈
    g_StackMemory = VirtualAlloc(NULL, stackSize, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (!g_StackMemory) {
        printf("[Loader] Failed to allocate stack.\n");
        return false;
    }
    // 2. 清零上下文
    memset(ctx, 0, sizeof(CPU_Context));
    // 3. 设置寄存器
    ctx->EIP = entryPoint;
    ctx->ESP.I32 = (uint32_t)g_StackMemory + stackSize - 0x20; // 栈底
    ctx->EBP.I32 = ctx->ESP.I32;
    // 简单设置段寄存器 (Flat Model)
    ctx->CS = 0x1B; ctx->DS = 0x23; ctx->ES = 0x23; ctx->SS = 0x23;
    return true;
}
// ---------------------------------------------------------
// LoadExe: 利用 PETools 解析并映射
// ---------------------------------------------------------
// ==========================================================
// 修改后的 LoadExe (带详细诊断)
// ==========================================================
bool LoadExe(const char* filepath, CPU_Context* ctx) {
    LPVOID pFileBuffer = NULL;
    DWORD fileSize = 0;
    // ==========================================================
    // 【新增修复】 防止 ReadPEFile 误抢 0x400000
    // ==========================================================
    // 先在 0x400000 占个坑 (MEM_RESERVE)，随便占多大 (比如 1MB)
    // 这样 ReadPEFile 分配内存时，就被迫去别的地方，不会碰这里。
    // ==========================================================
    void* pPlaceholder = VirtualAlloc((LPVOID)0x00400000, 0x100000, MEM_RESERVE, PAGE_NOACCESS);
    if (!pPlaceholder) {
        printf("[Loader Warning] Could not reserve placeholder at 0x400000. Proceeding anyway...\n");
    }
    // --- 1. 现在可以放心读取文件了 ---
    // pFileBuffer 绝对不会分配在 0x400000，因为那里被我们占了
    // --- 1. 解决字符集问题 ---
    // 我们的输入 filepath 是 char* (ANSI)，但 PETools 可能想要 TCHAR (Unicode)
#ifdef UNICODE
    WCHAR wPath[MAX_PATH];
    MultiByteToWideChar(CP_ACP, 0, filepath, -1, wPath, MAX_PATH);
    fileSize = ReadPEFile(wPath, &pFileBuffer); // 传宽字符
#else
    fileSize = ReadPEFile(filepath, &pFileBuffer); // 传 ANSI
#endif
    // ==========================================================
    // 【新增修复】 读完文件了，把坑让出来
    // ==========================================================
    if (pPlaceholder) {
        VirtualFree(pPlaceholder, 0, MEM_RELEASE);
    }
    if (!pFileBuffer || fileSize == 0) {
        // ReadPEFile 内部通常会打印错误，如果它没打，我们补一句
        printf("[Loader Critical] ReadPEFile failed. Is the path correct?\n");
        printf("                Path seen: %s\n", filepath);
        return false;
    }
    // --- 2. 解析 PE 头 ---
    PEHeaderInfo info = { 0 };
    ParsePEHeaders(pFileBuffer, &info);
    printf("[Loader Info] Target ImageBase: 0x%08X\n", info.dwImageBase);
    // --- 3. 申请内存 (关键故障点) ---
    // 尝试在目标 ImageBase 处强制分配
    g_CodeMemory = VirtualAlloc((LPVOID)info.dwImageBase, info.dwSizeOfImage,
        MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE);
    if (!g_CodeMemory) {
        DWORD err = GetLastError();
        printf("[Loader Critical] VirtualAlloc failed at 0x%08X! Error Code: %d\n", info.dwImageBase, err);
        if (err == 487) { // 487 = ERROR_INVALID_ADDRESS (通常意味着地址被占用了)
            printf("    [Analysis] Memory conflict! The host emulator (TinyX86) or system \n");
            printf("               is already using address 0x%08X.\n", info.dwImageBase);
            printf("    [Fix] You need to enable ASLR for TinyX86 or support Relocations.\n");
        }
        free(pFileBuffer);
        return false;
    }
    // ... (后续代码：清零、复制 Header、复制 Section 保持不变) ...
    memset(g_CodeMemory, 0, info.dwSizeOfImage);
    memcpy(g_CodeMemory, pFileBuffer, info.dwSizeOfHeaders);
    PIMAGE_DOS_HEADER pDos = (PIMAGE_DOS_HEADER)pFileBuffer;
    PIMAGE_NT_HEADERS pNt = (PIMAGE_NT_HEADERS)((DWORD)pFileBuffer + pDos->e_lfanew);
    PIMAGE_SECTION_HEADER pSec = IMAGE_FIRST_SECTION(pNt);
    for (int i = 0; i < info.wNumberOfSections; i++) {
        if (pSec[i].SizeOfRawData > 0) {
            void* dest = (void*)((DWORD)g_CodeMemory + pSec[i].VirtualAddress);
            void* src = (void*)((DWORD)pFileBuffer + pSec[i].PointerToRawData);
            memcpy(dest, src, pSec[i].SizeOfRawData);
        }
    }
    free(pFileBuffer);
    // 初始化 CPU
    printf("[Loader Success] Loaded at 0x%08X. Entry: 0x%08X\n", (uint32_t)g_CodeMemory, info.dwEntryPoint);
    return InitCpuCommon(ctx, (uint32_t)g_CodeMemory + info.dwEntryPoint, 0);
}
// ---------------------------------------------------------
// LoadBin: 加载二进制文件
// ---------------------------------------------------------
bool LoadBin(const char* filepath, CPU_Context* ctx, uint32_t baseAddr) {
    FILE* f = fopen(filepath, "rb");
    if (!f) { printf("[Loader] Cannot open file: %s\n", filepath); return false; }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    rewind(f);
    if (baseAddr == 0) baseAddr = 0x00400000;
    g_CodeMemory = VirtualAlloc((LPVOID)baseAddr, size + 0x1000,
        MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE);
    if (!g_CodeMemory) g_CodeMemory = VirtualAlloc(NULL, size + 0x1000, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE);
    if (!g_CodeMemory) { fclose(f); return false; }
    fread(g_CodeMemory, 1, size, f);
    fclose(f);
    printf("[Loader] Binary loaded at 0x%08X\n", (uint32_t)g_CodeMemory);
    return InitCpuCommon(ctx, (uint32_t)g_CodeMemory, 0);
}
// ---------------------------------------------------------
// LoadMem: 加载内存数组 (内置测试)
// ---------------------------------------------------------
bool LoadMem(const uint8_t* code, uint32_t size, CPU_Context* ctx, uint32_t baseAddr) {
    if (baseAddr == 0) baseAddr = 0x00400000;
    g_CodeMemory = VirtualAlloc((LPVOID)baseAddr, size + 0x1000, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE);
    if (!g_CodeMemory) g_CodeMemory = VirtualAlloc(NULL, size + 0x1000, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE);
    if (!g_CodeMemory) return false;
    memcpy(g_CodeMemory, code, size);
    printf("[Loader] Test code mapped at 0x%08X\n", (uint32_t)g_CodeMemory);
    return InitCpuCommon(ctx, (uint32_t)g_CodeMemory, 0);
}
void UnloadExe(CPU_Context* ctx) {
    if (g_CodeMemory) { VirtualFree(g_CodeMemory, 0, MEM_RELEASE); g_CodeMemory = NULL; }
    if (g_StackMemory) { VirtualFree(g_StackMemory, 0, MEM_RELEASE); g_StackMemory = NULL; }
}