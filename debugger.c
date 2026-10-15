#define _CRT_SECURE_NO_WARNINGS
#include "debugger.h"
#include "TinyX86.h"
#include <stdio.h>
#include <string.h> 
#include <ctype.h>
#include <stdlib.h> 
extern void* g_StackMemory;
// ============================================================================
// 内部状态与配置
// ============================================================================
#define MAX_BREAKPOINTS 16
static uint32_t g_Breakpoints[MAX_BREAKPOINTS];
static int g_BpCount = 0;
// ============================================================================
// 内部辅助函数声明
// ============================================================================
static void ToUpperStr(char* str);
static bool IsBreakpoint(uint32_t eip);
static bool IsSoftwareBreakpoint(uint32_t eip);
static bool IsStepOverCandidate(DecodeContext* d_ctx);
// 命令处理函数 (Handlers)
static void Cmd_StepInto(CPU_Context* ctx, const char* args);
static void Cmd_StepOver(CPU_Context* ctx, DecodeContext* d_ctx);
static void Cmd_Continue(CPU_Context* ctx);
static void Cmd_AddBreakpoint(const char* args);
static void Cmd_ListBreakpoints();
static void Cmd_ClearBreakpoints();
static void Cmd_ShowRegs(CPU_Context* ctx);
static void Cmd_Disasm(CPU_Context* ctx, const char* args);
static void Cmd_MemDump(const char* args);
static void Cmd_MemEdit(const char* args);
static void Cmd_MemWriteVal(const char* args, int byte_width);
static void Cmd_RegWrite(CPU_Context* ctx, const char* args);
static void Cmd_StackDump(CPU_Context* ctx, const char* args);
static void PrintHelp();
// ============================================================================
// 主循环逻辑
// ============================================================================
void Debugger(CPU_Context* ctx) {
    char input[128];
    DecodeContext d_ctx;
    printf("\n=== TinyX86 Debugger Initialized ===\n");
    printf("Type 'h' for help.\n");
    while (!ctx->Halted) {
        // 1. 检查是否刚刚命中断点
        if (IsBreakpoint(ctx->EIP)) {
            printf("\n[Debugger] Hit Breakpoint at 0x%08X\n", ctx->EIP);
        }
        // 2. 预解析当前指令 (用于显示和逻辑判断)
        ParseInstuction((uint8_t*)ctx->EIP, ctx->EIP, &d_ctx);
        FormatInstruction((uint8_t*)ctx->EIP, &d_ctx);
        // 3. 显示 Prompt
        printf("\n(0x%08X)  %-30s  ; Next\n", ctx->EIP, d_ctx.asm_str);
        printf("(dbg) > ");
        // 4. 读取输入
        if (!fgets(input, sizeof(input), stdin)) break;
        input[strcspn(input, "\n")] = 0; // 去除换行
        if (strlen(input) == 0) continue;
        // 5. 解析命令与参数
        char cmd = tolower(input[0]);
        char* args = input + 1;
        while (*args == ' ') args++; // 跳过命令后的空格,是子命令则指向第二个字符
        // 6. 命令分发 (Dispatcher)
        switch (cmd) {
            case 's': // Step Into
                Cmd_StepInto(ctx, args);
                break;
            case 'n': // Step Over
                Cmd_StepOver(ctx, &d_ctx);
                break;
            case 'c': // Continue
                Cmd_Continue(ctx);
                break;
            case 'b': // Breakpoint Manager
                if (strncmp(args, "l", 1) == 0) Cmd_ListBreakpoints();      // bl
                else if (strncmp(args, "c", 1) == 0) Cmd_ClearBreakpoints(); // bc
                else Cmd_AddBreakpoint(args);                               // b <addr>
                break;
            case 'r': // Registers
                Cmd_ShowRegs(ctx);
                break;
            case 'u': // Unassemble (Disasm)
                Cmd_Disasm(ctx, args);
                break;
            case 'm': // Memory Dump
                Cmd_MemDump(args);
                break;
            case 'k':
                Cmd_StackDump(ctx, args);
                break;
            case 'e': // Edit Memory
                if (strncmp(args, "d", 1) == 0) Cmd_MemWriteVal(args + 1, 4);      // ed <addr> <val>
                else if (strncmp(args, "w", 1) == 0) Cmd_MemWriteVal(args + 1, 2); // ew <addr> <val>
                else Cmd_MemEdit(args); // 原有的字节流写入 e <addr> <b1> <b2>...
                break;
            case 'w': // Write Register
                Cmd_RegWrite(ctx, args);
                break;
            case 'h': // Help
            case '?':
                PrintHelp();
                break;
            case 'q': // Quit
                ctx->Halted = true;
                break;
            default:
                printf("Unknown command '%c'. Type 'h' for help.\n", cmd);
                break;
        }
        if (cmd == 'q') break;
    }
    printf("Debugger exited. (Halted State: %s)\n", ctx->Halted ? "YES" : "NO");
}
// ============================================================================
// 命令处理实现 (Command Handlers)
// ============================================================================
static void PrintHelp() {
    printf("\n--- Command Help ---\n");
    printf(" Execution:\n");
    printf("  s [N]     : Step Into (execute N instructions, default 1)\n");
    printf("  n         : Step Over (skip CALL、LOOP、REP... instructions)\n");
    printf("  c         : Continue (run until breakpoint or halt)\n");
    printf("  q         : Quit debugger (halt CPU)\n");
    printf(" Breakpoints:\n");
    printf("  b <addr>  : Set Breakpoint at hex address (e.g., b 401005)\n");
    printf("  bl        : List all Breakpoints\n");
    printf("  bc        : Clear all Breakpoints\n");
    printf(" Inspection:\n");
    printf("  r         : Show all Registers (GPR, Flags, FPU)\n");
    printf("  u [N]     : Disassemble next N instructions (default 5)\n");
    printf("  m <addr>  : Dump memory at hex address (64 bytes)\n");
    printf("  k [N]     : Dump Stack (show N dwords around ESP, default 8)\n");
    printf(" Modification:\n");
    printf("  w <reg> <val> : Write Register (e.g., w EAX 1234, w ZF 1)\n");
    printf("  e <addr> <val>... : Edit memory bytes (e.g., e 401000 90 CC)\n");
    printf("  ew <addr> <val>   : Edit memory Word (e.g., ew 401000 1234)\n");
    printf("  ed <addr> <val>   : Edit memory DWORD (e.g., ed 401000 12345678)\n"); 
}
static void Cmd_StepInto(CPU_Context* ctx, const char* args) {
    int count = 1;
    if (*args) {
        if (sscanf(args, "%d", &count) != 1 || count <= 0) count = 1;
    }
    printf("Stepping %d instructions...\n", count);
    for (int i = 0; i < count; i++) {
        if (ctx->Halted) break;
        // --- 特殊处理 INT 3 ---
        if (IsSoftwareBreakpoint(ctx->EIP)) {
            printf("[Step] Trapped by INT 3 at 0x%08X (Skipping instruction)\n", ctx->EIP);
            ctx->EIP++; // 手动跳过 0xCC 字节
            // 通常遇到断点后应该立刻停止步进，把控制权交给用户
            break;
        }
            //执行指令
            runcpu(ctx, 1);
            // 如果步进过程中遇到断点（非当前指令），暂停
            if (i < count - 1 && IsBreakpoint(ctx->EIP)) {
                printf("[Step] Stopped at breakpoint 0x%08X\n", ctx->EIP);
                break;
            }
    }
}
static void Cmd_StepOver(CPU_Context* ctx, DecodeContext* d_ctx) {
    // 使用新的判断逻辑
    if (IsStepOverCandidate(d_ctx)) {
        // 计算"下一步"的目标地址 (即当前指令的下一条指令地址)
        // 无论是 LOOP 跳回去了，还是 REP 原地打转，只要 EIP 不等于这个地址，就说明还没执行完
        uint32_t target_eip = ctx->EIP + d_ctx->instr_len;
        printf("[Debugger] Stepping over Block/Call... (Target EIP: 0x%08X)\n", target_eip);
        // 防止无限循环 (比如 jmp $ 或者死循环的 LOOP)，设置一个较大的步数上限
        int max_steps = 5000000;
        while (ctx->EIP != target_eip && !ctx->Halted && max_steps-- > 0) {
            // --- 拦截机制 ---
            // 1. 检查中间是否遇到了 INT 3 (软断点)
            if (IsSoftwareBreakpoint(ctx->EIP)) {
                printf("\n[Debugger] Hit INT 3 inside StepOver at 0x%08X\n", ctx->EIP);
                break; // 停止步过，交还控制权
            }
            // 执行单步
            runcpu(ctx, 1);
            // 2. 检查中间是否遇到了用户设置的断点 (b <addr>)
            if (IsBreakpoint(ctx->EIP)) {
                printf("\n[Debugger] Hit User Breakpoint inside StepOver at 0x%08X\n", ctx->EIP);
                break;
            }
        }
        if (max_steps <= 0) {
            printf("[Warn] StepOver timeout (Loop too long or infinite?). Stopped.\n");
        }
    } else {
        // 如果不是特殊指令，StepOver 等同于 StepInto
        Cmd_StepInto(ctx, "");
    }
}
static void Cmd_Continue(CPU_Context* ctx) {
    printf("Running... (Press Ctrl+C if stuck loop)\n");
    // 如果当前就在自定义断点上，先走一步，防止死循环
    // (注：如果是 INT 3，用户需要先按 's' 跳过，否则 'c' 会立刻再次被 INT 3 拦截)
    if (IsBreakpoint(ctx->EIP)) {
        runcpu(ctx, 1);
    }
    // 持续运行
    while (!ctx->Halted) {
        //  运行前检查 INT 3 ---
        // 必须在 runcpu 之前检查，否则 CPU 执行到 CC 可能会 Halt
        if (IsSoftwareBreakpoint(ctx->EIP)) {
            printf("\n[Debugger] Hit Software Breakpoint (INT 3) at 0x%08X\n", ctx->EIP);
            // 关键：不设置 Halted，直接 break 出循环，回到 Debugger 的 while 输入循环
            break;
        }
        runcpu(ctx, 1);
        // 检查用户定义的断点 (断点数组)
        if (IsBreakpoint(ctx->EIP)) {
            printf("\n[Debugger] Hit Breakpoint at 0x%08X\n", ctx->EIP);
            break;
        }
    }
}
static void Cmd_AddBreakpoint(const char* args) {
    uint32_t addr = 0;
    if (sscanf(args, "%x", &addr) == 1) {
        if (g_BpCount >= MAX_BREAKPOINTS) {
            printf("Error: Breakpoint list full (%d).\n", MAX_BREAKPOINTS);
            return;
        }
        if (IsBreakpoint(addr)) {
            printf("Breakpoint already exists at 0x%08X.\n", addr);
            return;
        }
        g_Breakpoints[g_BpCount++] = addr;
        printf("Breakpoint set at 0x%08X\n", addr);
    } else {
        printf("Usage: b <hex_addr>\n");
    }
}
static void Cmd_ListBreakpoints() {
    if (g_BpCount == 0) {
        printf("No breakpoints set.\n");
    } else {
        printf("Breakpoints:\n");
        for (int i = 0; i < g_BpCount; i++) {
            printf("  [%d] 0x%08X\n", i, g_Breakpoints[i]);
        }
    }
}
static void Cmd_ClearBreakpoints() {
    g_BpCount = 0;
    printf("All breakpoints cleared.\n");
}
static void Cmd_ShowRegs(CPU_Context* ctx) {
    ShowState(ctx);
}
static void Cmd_Disasm(CPU_Context* ctx, const char* args) {
    int lines = 5;
    if (*args) {
        sscanf(args, "%d", &lines);
        if (lines <= 0) lines = 5;
        if (lines > 50) lines = 50; // 限制最大行数
    }
    uint32_t temp_eip = ctx->EIP;
    DecodeContext temp_dctx;
    printf("Disassembly (%d lines):\n", lines);
    for (int i = 0; i < lines; i++) {
        ParseInstuction((uint8_t*)temp_eip, temp_eip, &temp_dctx);
        FormatInstruction((uint8_t*)temp_eip, &temp_dctx);
        printf("  0x%08X: ", temp_eip);
        // 打印机器码
        for (int j = 0; j < temp_dctx.instr_len; j++) {
            printf("%02X", ((uint8_t*)temp_eip)[j]);
        }
        // 对齐空格
        for (int j = temp_dctx.instr_len; j < 8; j++) printf("  ");
        printf(" | %s\n", temp_dctx.asm_str);
        temp_eip += temp_dctx.instr_len;
    }
}
static void Cmd_MemDump(const char* args) {
    uint32_t addr = 0;
    if (sscanf(args, "%x", &addr) == 1) {
        printf("Memory at 0x%08X:\n", addr);
        // 显示 4 行，每行 16 字节
        for (int i = 0; i < 4; i++) {
            uint32_t line_addr = addr + i * 16;
            printf("  %08X: ", line_addr);
            // Hex部分
            for (int j = 0; j < 16; j++) {
                printf("%02X ", MemRead(line_addr + j, 1));
            }
            // ASCII部分
            printf(" | ");
            for (int j = 0; j < 16; j++) {
                uint8_t val = MemRead(line_addr + j, 1);
                printf("%c", (isprint(val) ? val : '.'));
            }
            printf("\n");
        }
    } else {
        printf("Usage: m <hex_addr>\n");
    }
}
static void Cmd_MemEdit(const char* args) {
    uint32_t addr = 0;
    int n_read = 0;
    // 解析起始地址
    if (sscanf(args, "%x%n", &addr, &n_read) == 1) {
        const char* ptr = args + n_read;
        unsigned int byte_val;
        int count = 0;
        // 循环解析后续字节
        while (sscanf(ptr, "%x%n", &byte_val, &n_read) == 1) {
            MemWrite(addr + count, (uint8_t)byte_val, 1);
            count++;
            ptr += n_read;
        }
        printf("Wrote %d bytes to 0x%08X\n", count, addr);
    } else {
        printf("Usage: e <addr> <byte1> <byte2> ...\n");
    }
}
static void Cmd_RegWrite(CPU_Context* ctx, const char* args) {
    char target[32];
    uint32_t val;
    if (sscanf(args, "%s %x", target, &val) == 2) {
        if (SetCpuState(ctx, target, val)) {
            printf("Updated %s = 0x%X\n", target, val);
        } else {
            printf("Error: Unknown register or flag '%s'\n", target);
        }
    } else {
        printf("Usage: w <reg/flag> <hex_value>\n");
    }
}
// ============================================================================
// 辅助与显示函数实现
// ============================================================================
void PrintFlags(CPU_Context* ctx) {
    printf("FLAGS: [ ");
    if (ctx->EFLAGS.CF) printf("CF ");
    if (ctx->EFLAGS.PF) printf("PF ");
    if (ctx->EFLAGS.AF) printf("AF ");
    if (ctx->EFLAGS.ZF) printf("ZF ");
    if (ctx->EFLAGS.SF) printf("SF ");
    if (ctx->EFLAGS.OF) printf("OF ");
    if (ctx->EFLAGS.DF) printf("DF ");
    if (ctx->EFLAGS.IF) printf("IF ");
    printf("] (Raw: 0x%08X)\n", ctx->EFLAGS.Value);
}
void PrintFPU(CPU_Context* ctx) {
    int top = ctx->FPU.SW.TOP;
    printf("FPU SW: TOP=%d  C3=%d C2=%d C1=%d C0=%d\n",
        top, ctx->FPU.SW.C3, ctx->FPU.SW.C2, ctx->FPU.SW.C1, ctx->FPU.SW.C0);
    printf("   Reg |      Double Value      | Logical ST(i)\n");
    printf("   ----+------------------------+--------------\n");
    for (int i = 0; i < 8; i++) {
        int logical_idx = (i - top + 8) & 7;
        char logical_str[16] = "";
        if (i == top) sprintf(logical_str, "<- ST(0) TOP");
        else sprintf(logical_str, "   ST(%d)", logical_idx);
        printf("   R%d  | %22.10f | %s\n", i, ctx->FPU.Regs[i], logical_str);
    }
}
void ShowState(CPU_Context* ctx) {
    printf("\n--- CPU STATE ---------------------------------------------------\n");
    printf("EAX: 0x%08X  EBX: 0x%08X  ECX: 0x%08X  EDX: 0x%08X\n",
        ctx->EAX.I32, ctx->EBX.I32, ctx->ECX.I32, ctx->EDX.I32);
    printf("ESI: 0x%08X  EDI: 0x%08X  EBP: 0x%08X  ESP: 0x%08X\n",
        ctx->ESI.I32, ctx->EDI.I32, ctx->EBP.I32, ctx->ESP.I32);
    printf("EIP: 0x%08X  ", ctx->EIP);
    // 简单显示段寄存器
    printf("CS:%04X DS:%04X ES:%04X SS:%04X\n",
        ctx->CS, ctx->DS, ctx->ES, ctx->SS);
    PrintFlags(ctx);
    printf("--- FPU ---------------------------------------------------------\n");
    PrintFPU(ctx);
    printf("-----------------------------------------------------------------\n");
}
static bool IsBreakpoint(uint32_t eip) {
    for (int i = 0; i < g_BpCount; i++) {
        if (g_Breakpoints[i] == eip) return true;
    }
    return false;
}
static void ToUpperStr(char* str) {
    for (; *str; ++str) *str = toupper((unsigned char)*str);
}
// ============================================================================
// SetCpuState (高性能Switch版本)
// ============================================================================
bool SetCpuState(CPU_Context* ctx, const char* name_in, uint32_t val) {
    char name[32];
    strncpy(name, name_in, 31);
    name[31] = '\0';
    ToUpperStr(name);
    switch (name[0]) {
        case 'E': // EAX..EIP, ES
            if (strcmp(name, "EAX") == 0) ctx->EAX.I32 = val;
            else if (strcmp(name, "EBX") == 0) ctx->EBX.I32 = val;
            else if (strcmp(name, "ECX") == 0) ctx->ECX.I32 = val;
            else if (strcmp(name, "EDX") == 0) ctx->EDX.I32 = val;
            else if (strcmp(name, "ESI") == 0) ctx->ESI.I32 = val;
            else if (strcmp(name, "EDI") == 0) ctx->EDI.I32 = val;
            else if (strcmp(name, "EBP") == 0) ctx->EBP.I32 = val;
            else if (strcmp(name, "ESP") == 0) ctx->ESP.I32 = val;
            else if (strcmp(name, "EIP") == 0) ctx->EIP = val;
            else if (strcmp(name, "ES") == 0)  ctx->ES = (uint16_t)val;
            else return false;
            break;
        case 'A': // AX, AF
            if (strcmp(name, "AX") == 0) ctx->EAX.I16 = (uint16_t)val;
            else if (strcmp(name, "AF") == 0) ctx->EFLAGS.AF = (val ? 1 : 0);
            else return false;
            break;
        case 'B': // BX, BP
            if (strcmp(name, "BX") == 0) ctx->EBX.I16 = (uint16_t)val;
            else if (strcmp(name, "BP") == 0) ctx->EBP.I16 = (uint16_t)val;
            else return false;
            break;
        case 'C': // CX, CS, CF
            if (strcmp(name, "CX") == 0) ctx->ECX.I16 = (uint16_t)val;
            else if (strcmp(name, "CS") == 0) ctx->CS = (uint16_t)val;
            else if (strcmp(name, "CF") == 0) ctx->EFLAGS.CF = (val ? 1 : 0);
            else return false;
            break;
        case 'D': // DX, DI, DS, DF
            if (strcmp(name, "DX") == 0) ctx->EDX.I16 = (uint16_t)val;
            else if (strcmp(name, "DI") == 0) ctx->EDI.I16 = (uint16_t)val;
            else if (strcmp(name, "DS") == 0) ctx->DS = (uint16_t)val;
            else if (strcmp(name, "DF") == 0) ctx->EFLAGS.DF = (val ? 1 : 0);
            else return false;
            break;
        case 'S': // SI, SP, SS, SF
            if (strcmp(name, "SI") == 0) ctx->ESI.I16 = (uint16_t)val;
            else if (strcmp(name, "SP") == 0) ctx->ESP.I16 = (uint16_t)val;
            else if (strcmp(name, "SS") == 0) ctx->SS = (uint16_t)val;
            else if (strcmp(name, "SF") == 0) ctx->EFLAGS.SF = (val ? 1 : 0);
            else return false;
            break;
        case 'F': // FS
            if (strcmp(name, "FS") == 0) ctx->FS = (uint16_t)val;
            else return false;
            break;
        case 'G': // GS
            if (strcmp(name, "GS") == 0) ctx->GS = (uint16_t)val;
            else return false;
            break;
        case 'I': // IF
            if (strcmp(name, "IF") == 0) ctx->EFLAGS.IF = (val ? 1 : 0);
            else return false;
            break;
        case 'O': // OF
            if (strcmp(name, "OF") == 0) ctx->EFLAGS.OF = (val ? 1 : 0);
            else return false;
            break;
        case 'P': // PF
            if (strcmp(name, "PF") == 0) ctx->EFLAGS.PF = (val ? 1 : 0);
            else return false;
            break;
        case 'Z': // ZF
            if (strcmp(name, "ZF") == 0) ctx->EFLAGS.ZF = (val ? 1 : 0);
            else return false;
            break;
        case 'T': // TOP
            if (strcmp(name, "TOP") == 0) ctx->FPU.SW.TOP = (val & 7);
            else return false;
            break;
        default:
            return false;
    }
    return true;
}
// 检查当前地址是否是 INT 3 指令
static bool IsSoftwareBreakpoint(uint32_t eip) {
    uint8_t opcode = MemRead(eip, 1);
    return (opcode == 0xCC);
}
// ============================================================================
// [新增] 堆栈查看功能
// ============================================================================
static void Cmd_StackDump(CPU_Context* ctx, const char* args) {
    int count = 8; // 默认显示栈顶以下的 8 个 DWORD
    if (*args) {
        int input_cnt = 0;
        if (sscanf(args, "%d", &input_cnt) == 1 && input_cnt > 0) {
            count = input_cnt;
        }
    }
    // 我们显示一点点 ESP "上方" (低地址) 的内容，作为上下文 (比如 2 个 DWORD)
    // 注意：x86 栈是向低地址增长的。
    // [High Addr] <--- 栈底
    // ...
    // [ESP]       <--- 栈顶 (当前数据)
    // [ESP - 4]   <--- 垃圾数据 / 下一次 PUSH 的位置
    int lines_above = 2;
    uint32_t esp = ctx->ESP.I32;
    uint32_t ebp = ctx->EBP.I32;
    printf("Stack Dump (ESP=0x%08X):\n", esp);
    printf("Address     Value       ASCII   Annotation\n");
    printf("----------  ----------  -----   ------------------\n");
    for (int i = -lines_above; i < count; i++) {
        uint32_t addr = esp + (i * 4);
        // ========================================================
        // 【核心修改】边界检查
        // ========================================================
        // 检查 addr 是否落分配的堆栈区间内
        // 注意：addr 是要读 4 字节，所以 addr+3 也不能越界
        if (addr < (uint32_t)g_StackMemory || addr + 4 > (uint32_t)g_StackMemory + 1024 * 64) {
            // 如果越界了，打印一个提示，然后跳过
            // 这样你就看到了堆栈的尽头，而不是程序崩溃
            if (i == -lines_above) continue; // 如果是上方越界（还没入栈的地方），直接不显示
            printf("0x%08X  [Out of Stack Range] \n", addr);
            break; // 下方越界（栈底之外），直接停止后续打印
        }
        uint32_t val = MemRead(addr, 4);
        // 简单的 ASCII 可视化
        char ascii[5];
        for (int b = 0; b < 4; b++) {
            uint8_t byte = (val >> (b * 8)) & 0xFF;
            ascii[b] = (isprint(byte)) ? byte : '.';
        }
        ascii[4] = '\0';
        // 打印地址和十六进制值
        printf("0x%08X  0x%08X  %s  ", addr, val, ascii);
        // 打印箭头和标记
        int has_note = 0;
        if (addr == esp) {
            printf("<--- ESP (Top)");
            has_note = 1;
        }
        if (addr == ebp) {
            printf("%s<--- EBP (Frame)", has_note ? " / " : "");
            has_note = 1;
        }
        // 简单的智能分析：如果值看起来像个栈内的指针（指向栈附近）
        // 这里的判断比较粗糙，仅作为示例
        if (val > esp && val < esp + 0x1000) {
            printf("%s(Ptr to Stack?)", has_note ? " " : "");
        }
        printf("\n");
    }
}
// 判断当前指令是否值得 "Step Over" (步过)
// 包括: CALL, LOOP, REP前缀, INT n
static bool IsStepOverCandidate(DecodeContext* d_ctx) {
    uint8_t op = d_ctx->opcode;
    // 1. CALL 系列
    // 0xE8: CALL rel, 0x9A: CALL ptr
    if (op == 0xE8 || op == 0x9A) return true;
    // 0xFF /2: CALL r/m
    if (op == 0xFF && d_ctx->reg == 2) return true;
    // 2. LOOP 系列 (0xE0 ~ 0xE2)
    // 0xE0: LOOPNE, 0xE1: LOOPE, 0xE2: LOOP
    if (op >= 0xE0 && op <= 0xE2) return true;
    // 3. REP 前缀系列 (用于字符串操作 MOVS, STOS, SCAS 等)
    // 0xF2: REPNE, 0xF3: REP/REPE
    // 注意: 在反汇编引擎中，F2/F3 被识别为前缀  (通常是这样)，则应该步过
    if (d_ctx->pfx_rep || d_ctx->pfx_repne) return true;
    // 4. 软中断 (INT n)
    // 0xCD: INT n (如 INT 21h, INT 80h)
    // 通常调试时不希望进入中断处理程序，而是直接看结果
    if (op == 0xCD) return true;
    return false;
}
static void Cmd_MemWriteVal(const char* args, int byte_width) {
    uint32_t addr = 0;
    uint32_t val = 0;
    // 解析格式: ed 00401000 12345678
    if (sscanf(args, "%x %x", &addr, &val) == 2) {
        MemWrite(addr, val, byte_width);
        printf("Memory Write: [0x%08X] = 0x%X (%d bytes)\n", addr, val, byte_width);
    } else {
        printf("Usage: e%c <hex_addr> <hex_value>\n", (byte_width == 4) ? 'd' : 'w');
    }
}