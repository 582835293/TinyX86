#define _CRT_SECURE_NO_WARNINGS
#include "PELoader.h"
#include "PETools.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#endif

// 全局记录，用于释放与调试器栈边界检查
static void* g_CodeMemory = NULL;
void* g_StackMemory = NULL;
uint32_t g_StackGuestBase = 0x00100000;
uint32_t g_StackSize = 1024 * 64;

static void* AllocHostMemory(uint32_t preferred_addr, uint32_t size) {
#if defined(_WIN32) && !defined(_WIN64)
    void* p = VirtualAlloc((LPVOID)(uintptr_t)preferred_addr, size, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE);
    if (!p && preferred_addr != 0) {
        p = VirtualAlloc(NULL, size, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE);
    }
    return p;
#else
    (void)preferred_addr;
    return calloc(1, size);
#endif
}

static void FreeHostMemory(void* ptr) {
    if (!ptr) return;
#if defined(_WIN32) && !defined(_WIN64)
    VirtualFree(ptr, 0, MEM_RELEASE);
#else
    free(ptr);
#endif
}

// ---------------------------------------------------------
// 通用 CPU 初始化 (设置 EIP, ESP, 段寄存器)
// ---------------------------------------------------------
static bool InitCpuCommon(CPU_Context* ctx, uint32_t entryPoint, uint32_t stackSize) {
    if (stackSize == 0) stackSize = 1024 * 64; // 默认 64KB
    g_StackSize = stackSize;

    // 1. 分配堆栈
    g_StackMemory = AllocHostMemory(0, stackSize);
    if (!g_StackMemory) {
        printf("[Loader] Failed to allocate stack.\n");
        return false;
    }

#if defined(_WIN32) && !defined(_WIN64)
    g_StackGuestBase = (uint32_t)(uintptr_t)g_StackMemory;
#else
    g_StackGuestBase = 0x00100000;
#endif
    TinyX86_RegisterRegion(g_StackGuestBase, g_StackMemory, stackSize);

    // 2. 清零上下文
    memset(ctx, 0, sizeof(CPU_Context));

    // 3. 设置寄存器
    ctx->EIP = entryPoint;
    ctx->ESP.I32 = g_StackGuestBase + stackSize - 0x20; // 栈底
    ctx->EBP.I32 = ctx->ESP.I32;

    // 简单设置段寄存器 (Flat Model)
    ctx->CS = 0x1B; ctx->DS = 0x23; ctx->ES = 0x23; ctx->SS = 0x23;
    ctx->EFLAGS.IF = 1;
    ctx->FPU.CW.Value = 0x037F;
    return true;
}

// ---------------------------------------------------------
// LoadExe: 利用 PETools 解析并映射 (支持 Windows / Linux / macOS)
// ---------------------------------------------------------
bool LoadExe(const char* filepath, CPU_Context* ctx) {
    void* pFileBuffer = NULL;
    uint32_t fileSize = ReadPEFile(filepath, &pFileBuffer);
    if (!pFileBuffer || fileSize == 0) {
        printf("[Loader Critical] ReadPEFile failed. Path: %s\n", filepath ? filepath : "(null)");
        return false;
    }

    PEHeaderInfo info;
    memset(&info, 0, sizeof(info));
    if (!ParsePEHeaders(pFileBuffer, &info) || info.dwSizeOfImage == 0) {
        printf("[Loader Critical] Invalid PE32 header in %s\n", filepath);
        free(pFileBuffer);
        return false;
    }

    printf("[Loader Info] Target ImageBase: 0x%08X\n", info.dwImageBase);
    TinyX86_ClearRegions();

    g_CodeMemory = AllocHostMemory(info.dwImageBase, info.dwSizeOfImage);
    if (!g_CodeMemory) {
        printf("[Loader Critical] Memory allocation failed for ImageSize 0x%08X\n", info.dwSizeOfImage);
        free(pFileBuffer);
        return false;
    }

    memset(g_CodeMemory, 0, info.dwSizeOfImage);
    memcpy(g_CodeMemory, pFileBuffer, info.dwSizeOfHeaders);

    PIMAGE_DOS_HEADER pDos = (PIMAGE_DOS_HEADER)pFileBuffer;
    PIMAGE_NT_HEADERS32 pNt = (PIMAGE_NT_HEADERS32)((uint8_t*)pFileBuffer + pDos->e_lfanew);
    PIMAGE_SECTION_HEADER pSec = IMAGE_FIRST_SECTION(pNt);

    for (int i = 0; i < info.wNumberOfSections; i++) {
        if (pSec[i].SizeOfRawData > 0) {
            void* dest = (uint8_t*)g_CodeMemory + pSec[i].VirtualAddress;
            void* src  = (uint8_t*)pFileBuffer + pSec[i].PointerToRawData;
            memcpy(dest, src, pSec[i].SizeOfRawData);
        }
    }
    free(pFileBuffer);

    uint32_t guestBase = info.dwImageBase ? info.dwImageBase : 0x00400000;
#if defined(_WIN32) && !defined(_WIN64)
    guestBase = (uint32_t)(uintptr_t)g_CodeMemory;
#endif
    TinyX86_RegisterRegion(guestBase, g_CodeMemory, info.dwSizeOfImage);

    printf("[Loader Success] Loaded at 0x%08X. Entry: 0x%08X\n", guestBase, info.dwEntryPoint);
    return InitCpuCommon(ctx, guestBase + info.dwEntryPoint, 0);
}

// ---------------------------------------------------------
// LoadBin: 加载二进制文件
// ---------------------------------------------------------
bool LoadBin(const char* filepath, CPU_Context* ctx, uint32_t baseAddr) {
    FILE* f = fopen(filepath, "rb");
    if (!f) {
        printf("[Loader] Cannot open file: %s\n", filepath);
        return false;
    }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    rewind(f);
    if (size <= 0) {
        fclose(f);
        return false;
    }

    if (baseAddr == 0) baseAddr = 0x00400000;
    uint32_t allocSize = (uint32_t)size + 0x1000;
    TinyX86_ClearRegions();

    g_CodeMemory = AllocHostMemory(baseAddr, allocSize);
    if (!g_CodeMemory) {
        fclose(f);
        return false;
    }

    size_t readBytes = fread(g_CodeMemory, 1, (size_t)size, f);
    fclose(f);
    (void)readBytes;

    uint32_t guestBase = baseAddr;
#if defined(_WIN32) && !defined(_WIN64)
    guestBase = (uint32_t)(uintptr_t)g_CodeMemory;
#endif
    TinyX86_RegisterRegion(guestBase, g_CodeMemory, allocSize);

    printf("[Loader] Binary loaded at 0x%08X\n", guestBase);
    return InitCpuCommon(ctx, guestBase, 0);
}

// ---------------------------------------------------------
// LoadMem: 加载内存数组 (内置测试)
// ---------------------------------------------------------
bool LoadMem(const uint8_t* code, uint32_t size, CPU_Context* ctx, uint32_t baseAddr) {
    if (baseAddr == 0) baseAddr = 0x00400000;
    uint32_t allocSize = size + 0x1000;
    TinyX86_ClearRegions();

    g_CodeMemory = AllocHostMemory(baseAddr, allocSize);
    if (!g_CodeMemory) return false;

    memcpy(g_CodeMemory, code, size);

    uint32_t guestBase = baseAddr;
#if defined(_WIN32) && !defined(_WIN64)
    guestBase = (uint32_t)(uintptr_t)g_CodeMemory;
#endif
    TinyX86_RegisterRegion(guestBase, g_CodeMemory, allocSize);

    printf("[Loader] Test code mapped at 0x%08X\n", guestBase);
    return InitCpuCommon(ctx, guestBase, 0);
}

void UnloadExe(CPU_Context* ctx) {
    (void)ctx;
    if (g_CodeMemory) {
        FreeHostMemory(g_CodeMemory);
        g_CodeMemory = NULL;
    }
    if (g_StackMemory) {
        FreeHostMemory(g_StackMemory);
        g_StackMemory = NULL;
    }
    TinyX86_ClearRegions();
}
