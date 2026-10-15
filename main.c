#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include "TinyX86.h"
#include "PELoader.h"
#include "debugger.h"
#include <windows.h>
#include <psapi.h> // WhoIsThere测试函数需要
#pragma comment(lib, "psapi.lib")

// =============================================================
// 内置默认测试机器码
// =============================================================
// C代码参考:
// double power(double base, int exp) {
//     if (exp <= 0) return 1.0;
//     return base * power(base, exp - 1);
// }
static const uint8_t g_TestCode[] = {
    // --- [0x00] CALLER ---
        0x6A, 0x03,                         // 0x00 | push 3
        0x68, 0x00, 0x00, 0x00, 0x40,       // 0x02 | push 0x40000000 (2.0)
        0x6A, 0x00,                         // 0x07 | push 0x00000000
        0xE8, 0x06, 0x00, 0x00, 0x00,       // 0x09 | call +6 (End:0x0E + 6 = 0x14)
        0x83, 0xC4, 0x0C,                   // 0x0E | add esp, 12
        0xCC,                               // 0x11 | int3
        0x90, 0x90,                         // 0x12 | nop (对齐，确保函数从 0x14 开始)

        // --- [0x14] POWER FUNCTION START ---
        0x55,                               // 0x14 | push ebp
        0x8B, 0xEC,                         // 0x15 | mov ebp, esp
        0x83, 0xEC, 0x40,                   // 0x17 | sub esp, 0x40

        // 1. 先填充脏数据 (这时栈顶还是 esp-0x40)
        0x8D, 0x7D, 0xC0,                   // 0x1A | lea edi, [ebp-40h]
        0xB9, 0x10, 0x00, 0x00, 0x00,       // 0x1D | mov ecx, 10h
        0xB8, 0xCC, 0xCC, 0xCC, 0xCC,       // 0x22 | mov eax, 0xCCCCCCCC
        0xF3, 0xAB,                         // 0x27 | rep stos dword ptr [edi]

        // 2. 再压入保护寄存器 (修正后这里地址顺延)
        0x53,                               // 0x29 | push ebx
        0x56,                               // 0x2A | push esi
        0x57,                               // 0x2B | push edi

        // --- 业务逻辑 ---
        // 
        0x83, 0x7D, 0x10, 0x00,             // 0x2C | cmp dword ptr [ebp+16], 0

        // JG 跳转目标是 0x36 (mov eax...)
        // 当前指令结束于 0x32。Offset = Target(0x36) - End(0x32) = 0x04
        0x7F, 0x04,                         // 0x30 | jg +4 (跳到 0x36)

        // --- Base Case ---
        0xD9, 0xE8,                         // 0x32 | fld1
        //JMP 跳转目标是 0x4C (函数收尾)
        // 当前指令结束于 0x36。Offset = Target(0x4C) - End(0x36) = 0x16 (22)
        0xEB, 0x16,                         // 0x34 | jmp +22 (跳到 0x4C)

        // --- Recursive Step (地址 0x36) ---
        0x8B, 0x45, 0x10,                   // 0x36 | mov eax, [ebp+16]
        0x48,                               // 0x39 | dec eax
        0x50,                               // 0x3A | push eax
        0xFF, 0x75, 0x0C,                   // 0x3B | push dword ptr [ebp+12]
        0xFF, 0x75, 0x08,                   // 0x3E | push dword ptr [ebp+8]

        // CALL 跳转目标是 0x14 (函数开头)
        // 当前指令结束于 0x46。Offset = Target(0x14) - End(0x46) = -50 (0xFFFFFFCE)
        0xE8, 0xCE, 0xFF, 0xFF, 0xFF,       // 0x41 | call -50 (跳回 0x14)

        0x83, 0xC4, 0x0C,                   // 0x46 | add esp, 12
        0xDC, 0x4D, 0x08,                   // 0x49 | fmul qword ptr [ebp+8]

        // --- [0x4C] Epilogue ---
        0x5F,                               // 0x4C | pop edi
        0x5E,                               // 0x4D | pop esi
        0x5B,                               // 0x4E | pop ebx
        0x8B, 0xE5,                         // 0x4F | mov esp, ebp
        0x5D,                               // 0x51 | pop ebp
        0xC3                                // 0x52 | ret
};



void WhoIsThere(void* addr) {//查内存占位的是谁，调试用
    MEMORY_BASIC_INFORMATION mbi;
    if (VirtualQuery(addr, &mbi, sizeof(mbi)) == 0) {
        printf("[Detective] VirtualQuery failed. Address is totally invalid.\n");
        return;
    }

    printf("\n[Detective] Investigating address 0x%08X...\n", (uint32_t)addr);

    // 1. 看看它是空闲的还是被占用的
    if (mbi.State == MEM_FREE) {
        printf("  -> Status: MEM_FREE (空闲! VirtualAlloc 应该成功的!)\n");
        return;
    }

    // 2. 如果被占用，是被什么占用的？
    printf("  -> Status: COMMITTED/RESERVED (被占用)\n");
    printf("  -> AllocationBase: 0x%08X (这块内存的起始位置)\n", (uint32_t)mbi.AllocationBase);

    // 3. 它是谁？
    if (mbi.Type == MEM_IMAGE) {
        printf("  -> Type: MEM_IMAGE (是 DLL 或 EXE 镜像)\n");
        char modName[MAX_PATH];
        if (GetMappedFileNameA(GetCurrentProcess(), mbi.AllocationBase, modName, MAX_PATH)) {
            printf("  -> OWNER: %s \n", modName); // 抓住你了！打印文件名
        } else {
            printf("  -> OWNER: Unknown Image\n");
        }
    } else if (mbi.Type == MEM_PRIVATE) {
        printf("  -> Type: MEM_PRIVATE (通常是 堆(Heap) 或 栈(Stack))\n");
        printf("     (可能是 malloc 分配的内存，或者是主线程的 Stack)\n");
    } else if (mbi.Type == MEM_MAPPED) {
        printf("  -> Type: MEM_MAPPED (文件映射)\n");
    }
    printf("==========================================\n\n");
}

// 在 main 函数第一行调用它：
// WhoIsThere((void*)0x00400000);
// ================= 侦探代码结束 =================

void PrintMenu() {
    printf("\n");
    printf("========== TinyX86 Shell ==========\n");
    printf(" [e]xe  <path> : Load PE Executable\n");
    printf(" [b]in  <path> : Load Raw Binary\n");
    printf(" [t]est        : Run Internal Test (Sum 1..10)\n");
    printf(" [h]elp        : Show Menu\n");
    printf(" [q]uit        : Exit\n");
    printf("===================================\n");
}

int main() {
    //WhoIsThere((void*)0x00400000);
    // ================= 侦探代码结束 =======
    CPU_Context cpu;
    char inputBuffer[256];
    char* cmd;
    char* arg;

    printf("TinyX86 Emulator [Sprint 13]\n");
    printf("Type 'h' for help.\n");

    while (1) {
        printf("\n(Shell) > ");

        if (!fgets(inputBuffer, sizeof(inputBuffer), stdin)) break;

        // 去除换行符
        inputBuffer[strcspn(inputBuffer, "\n")] = 0;

        // 解析命令
        cmd = strtok(inputBuffer, " ");
        if (!cmd) continue;

        // 获取参数 (可能为空)
        arg = strtok(NULL, "");

        // 统一转小写只取首字母
        char op = tolower(cmd[0]);
        bool loaded = false;

        // --- 极速命令匹配 ---
        switch (op) {
            case 'q': // Quit
                printf("Bye.\n");
                return 0;

            case 'h': // Help
                PrintMenu();
                break;

            case 't': // Test
                printf("[Shell] Loading internal test...\n");
                loaded = LoadMem(g_TestCode, sizeof(g_TestCode), &cpu, 0x00400000);
                break;

            case 'e': // Exe
                if (arg) {
                    // 去除引号 (支持拖拽文件路径)
                    if (arg[0] == '\"') { arg++; arg[strlen(arg) - 1] = 0; }
                    printf("[Shell] Loading PE: %s\n", arg);
                    loaded = LoadExe(arg, &cpu);
                } else {
                    printf("[Error] Need file path. Usage: e <path>\n");
                }
                break;

            case 'b': // Bin
                if (arg) {
                    if (arg[0] == '\"') { arg++; arg[strlen(arg) - 1] = 0; }
                    printf("[Shell] Loading Binary: %s\n", arg);
                    loaded = LoadBin(arg, &cpu, 0x00400000);
                } else {
                    printf("[Error] Need file path. Usage: b <path>\n");
                }
                break;

            default:
                printf("[Error] Unknown command '%s'. Try 'h'.\n", cmd);
                break;
        }

        // --- 进入调试器 ---
        if (!loaded) {
            // 如果没加载成功，告诉是不是文件没找到或者格式不对
            printf("[Error] Load operation failed! Check file path or PE format (Must be 32-bit).\n");
        } else {
            // --- 进入调试器 ---
            printf("----------------------------------\n");
            Debugger(&cpu);
            UnloadExe(&cpu);
            printf("----------------------------------\n");
            printf("[Shell] Back to main menu.\n");
        }
    }

    return 0;
}
