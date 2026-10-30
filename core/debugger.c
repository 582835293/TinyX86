#define _CRT_SECURE_NO_WARNINGS
#include "debugger.h"
#include "TinyX86.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>

extern void* g_StackMemory;
extern uint32_t g_StackGuestBase;
extern uint32_t g_StackSize;

#define MAX_BREAKPOINTS 16
static uint32_t g_Breakpoints[MAX_BREAKPOINTS];
static int g_BpCount = 0;

static void ToUpperStr(char* str);
static bool IsBreakpoint(uint32_t eip);
static bool IsSoftwareBreakpoint(uint32_t eip);
static bool IsStepOverCandidate(DecodeContext* d_ctx);

static void Cmd_StepInto(CPU_Context* ctx, const char* args);
static void Cmd_StepOver(CPU_Context* ctx, DecodeContext* d_ctx);
static void Cmd_Continue(CPU_Context* ctx);
static void Cmd_AddBreakpoint(const char* args);
static void Cmd_ListBreakpoints(void);
static void Cmd_ClearBreakpoints(void);
static void Cmd_ShowRegs(CPU_Context* ctx);
static void Cmd_Disasm(CPU_Context* ctx, const char* args);
static void Cmd_MemDump(const char* args);
static void Cmd_MemEdit(const char* args);
static void Cmd_MemWriteVal(const char* args, int byte_width);
static void Cmd_RegWrite(CPU_Context* ctx, const char* args);
static void Cmd_StackDump(CPU_Context* ctx, const char* args);
static void PrintHelp(void);

void Debugger(CPU_Context* ctx) {
    char input[128];
    DecodeContext d_ctx;
    printf("\n=== TinyX86 Debugger Initialized ===\n");
    printf("Type 'h' for help.\n");
    while (!ctx->Halted) {
        if (IsBreakpoint(ctx->EIP)) {
            printf("\n[Debugger] Hit Breakpoint at 0x%08X\n", ctx->EIP);
        }
        uint8_t* code_ptr = TinyX86_TranslateAddress(ctx->EIP);
        ParseInstuction(code_ptr, ctx->EIP, &d_ctx);
        FormatInstruction(code_ptr, &d_ctx);

        printf("\n(0x%08X)  %-30s  ; Next\n", ctx->EIP, d_ctx.asm_str);
        printf("(dbg) > ");

        if (!fgets(input, sizeof(input), stdin)) break;
        input[strcspn(input, "\r\n")] = 0;
        if (strlen(input) == 0) continue;

        char cmd = (char)tolower((unsigned char)input[0]);
        char* args = input + 1;
        while (*args == ' ') args++;

        switch (cmd) {
            case 's':
                Cmd_StepInto(ctx, args);
                break;
            case 'n':
                Cmd_StepOver(ctx, &d_ctx);
                break;
            case 'c':
                Cmd_Continue(ctx);
                break;
            case 'b':
                if (strncmp(args, "l", 1) == 0) Cmd_ListBreakpoints();
                else if (strncmp(args, "c", 1) == 0) Cmd_ClearBreakpoints();
                else Cmd_AddBreakpoint(args);
                break;
            case 'r':
                Cmd_ShowRegs(ctx);
                break;
            case 'u':
                Cmd_Disasm(ctx, args);
                break;
            case 'm':
                Cmd_MemDump(args);
                break;
            case 'k':
                Cmd_StackDump(ctx, args);
                break;
            case 'e':
                if (strncmp(args, "d", 1) == 0) Cmd_MemWriteVal(args + 1, 4);
                else if (strncmp(args, "w", 1) == 0) Cmd_MemWriteVal(args + 1, 2);
                else Cmd_MemEdit(args);
                break;
            case 'w':
                Cmd_RegWrite(ctx, args);
                break;
            case 'h':
            case '?':
                PrintHelp();
                break;
            case 'q':
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

static void PrintHelp(void) {
    printf("\n--- Command Help ---\n");
    printf(" Execution:\n");
    printf("  s [N]     : Step Into (execute N instructions, default 1)\n");
    printf("  n         : Step Over (skip CALL, LOOP, REP... instructions)\n");
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
    printf("  w <reg> <val>     : Write Register (e.g., w EAX 1234, w ZF 1)\n");
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
        if (IsSoftwareBreakpoint(ctx->EIP)) {
            printf("[Step] Trapped by INT 3 at 0x%08X (Skipping instruction)\n", ctx->EIP);
            ctx->EIP++;
            break;
        }
        runcpu(ctx, 1);
        if (i < count - 1 && IsBreakpoint(ctx->EIP)) {
            printf("[Step] Stopped at breakpoint 0x%08X\n", ctx->EIP);
            break;
        }
    }
}

static void Cmd_StepOver(CPU_Context* ctx, DecodeContext* d_ctx) {
    if (IsStepOverCandidate(d_ctx)) {
        uint32_t target_eip = ctx->EIP + (uint32_t)d_ctx->instr_len;
        printf("[Debugger] Stepping over Block/Call... (Target EIP: 0x%08X)\n", target_eip);
        int max_steps = 5000000;
        while (ctx->EIP != target_eip && !ctx->Halted && max_steps-- > 0) {
            if (IsSoftwareBreakpoint(ctx->EIP)) {
                printf("\n[Debugger] Hit INT 3 inside StepOver at 0x%08X\n", ctx->EIP);
                break;
            }
            runcpu(ctx, 1);
            if (IsBreakpoint(ctx->EIP)) {
                printf("\n[Debugger] Hit User Breakpoint inside StepOver at 0x%08X\n", ctx->EIP);
                break;
            }
        }
        if (max_steps <= 0) {
            printf("[Warn] StepOver timeout (Loop too long or infinite?). Stopped.\n");
        }
    } else {
        Cmd_StepInto(ctx, "");
    }
}

static void Cmd_Continue(CPU_Context* ctx) {
    printf("Running... (Press Ctrl+C if stuck loop)\n");
    if (IsBreakpoint(ctx->EIP)) {
        runcpu(ctx, 1);
    }
    while (!ctx->Halted) {
        if (IsSoftwareBreakpoint(ctx->EIP)) {
            printf("\n[Debugger] Hit Software Breakpoint (INT 3) at 0x%08X\n", ctx->EIP);
            break;
        }
        runcpu(ctx, 1);
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

static void Cmd_ListBreakpoints(void) {
    if (g_BpCount == 0) {
        printf("No breakpoints set.\n");
    } else {
        printf("Breakpoints:\n");
        for (int i = 0; i < g_BpCount; i++) {
            printf("  [%d] 0x%08X\n", i, g_Breakpoints[i]);
        }
    }
}

static void Cmd_ClearBreakpoints(void) {
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
        if (lines > 50) lines = 50;
    }
    uint32_t temp_eip = ctx->EIP;
    DecodeContext temp_dctx;
    printf("Disassembly (%d lines):\n", lines);
    for (int i = 0; i < lines; i++) {
        uint8_t* code_ptr = TinyX86_TranslateAddress(temp_eip);
        ParseInstuction(code_ptr, temp_eip, &temp_dctx);
        FormatInstruction(code_ptr, &temp_dctx);
        printf("  0x%08X: ", temp_eip);
        for (int j = 0; j < temp_dctx.instr_len; j++) {
            printf("%02X", code_ptr[j]);
        }
        for (int j = temp_dctx.instr_len; j < 8; j++) printf("  ");
        printf(" | %s\n", temp_dctx.asm_str);
        temp_eip += (uint32_t)temp_dctx.instr_len;
    }
}

static void Cmd_MemDump(const char* args) {
    uint32_t addr = 0;
    if (sscanf(args, "%x", &addr) == 1) {
        printf("Memory at 0x%08X:\n", addr);
        for (int i = 0; i < 4; i++) {
            uint32_t line_addr = addr + (uint32_t)(i * 16);
            printf("  %08X: ", line_addr);
            for (int j = 0; j < 16; j++) {
                printf("%02X ", MemRead(line_addr + (uint32_t)j, 1));
            }
            printf(" | ");
            for (int j = 0; j < 16; j++) {
                uint8_t val = (uint8_t)MemRead(line_addr + (uint32_t)j, 1);
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
    if (sscanf(args, "%x%n", &addr, &n_read) == 1) {
        const char* ptr = args + n_read;
        unsigned int byte_val;
        int count = 0;
        while (sscanf(ptr, "%x%n", &byte_val, &n_read) == 1) {
            MemWrite(addr + (uint32_t)count, (uint8_t)byte_val, 1);
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
    if (sscanf(args, "%31s %x", target, &val) == 2) {
        if (SetCpuState(ctx, target, val)) {
            printf("Updated %s = 0x%X\n", target, val);
        } else {
            printf("Error: Unknown register or flag '%s'\n", target);
        }
    } else {
        printf("Usage: w <reg/flag> <hex_value>\n");
    }
}

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
        if (i == top) snprintf(logical_str, sizeof(logical_str), "<- ST(0) TOP");
        else snprintf(logical_str, sizeof(logical_str), "   ST(%d)", logical_idx);
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
    for (; *str; ++str) *str = (char)toupper((unsigned char)*str);
}

bool SetCpuState(CPU_Context* ctx, const char* name_in, uint32_t val) {
    char name[32];
    strncpy(name, name_in, 31);
    name[31] = '\0';
    ToUpperStr(name);
    switch (name[0]) {
        case 'E':
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
        case 'A':
            if (strcmp(name, "AX") == 0) ctx->EAX.I16 = (uint16_t)val;
            else if (strcmp(name, "AF") == 0) ctx->EFLAGS.AF = (val ? 1 : 0);
            else return false;
            break;
        case 'B':
            if (strcmp(name, "BX") == 0) ctx->EBX.I16 = (uint16_t)val;
            else if (strcmp(name, "BP") == 0) ctx->EBP.I16 = (uint16_t)val;
            else return false;
            break;
        case 'C':
            if (strcmp(name, "CX") == 0) ctx->ECX.I16 = (uint16_t)val;
            else if (strcmp(name, "CS") == 0) ctx->CS = (uint16_t)val;
            else if (strcmp(name, "CF") == 0) ctx->EFLAGS.CF = (val ? 1 : 0);
            else return false;
            break;
        case 'D':
            if (strcmp(name, "DX") == 0) ctx->EDX.I16 = (uint16_t)val;
            else if (strcmp(name, "DI") == 0) ctx->EDI.I16 = (uint16_t)val;
            else if (strcmp(name, "DS") == 0) ctx->DS = (uint16_t)val;
            else if (strcmp(name, "DF") == 0) ctx->EFLAGS.DF = (val ? 1 : 0);
            else return false;
            break;
        case 'S':
            if (strcmp(name, "SI") == 0) ctx->ESI.I16 = (uint16_t)val;
            else if (strcmp(name, "SP") == 0) ctx->ESP.I16 = (uint16_t)val;
            else if (strcmp(name, "SS") == 0) ctx->SS = (uint16_t)val;
            else if (strcmp(name, "SF") == 0) ctx->EFLAGS.SF = (val ? 1 : 0);
            else return false;
            break;
        case 'F':
            if (strcmp(name, "FS") == 0) ctx->FS = (uint16_t)val;
            else return false;
            break;
        case 'G':
            if (strcmp(name, "GS") == 0) ctx->GS = (uint16_t)val;
            else return false;
            break;
        case 'I':
            if (strcmp(name, "IF") == 0) ctx->EFLAGS.IF = (val ? 1 : 0);
            else return false;
            break;
        case 'O':
            if (strcmp(name, "OF") == 0) ctx->EFLAGS.OF = (val ? 1 : 0);
            else return false;
            break;
        case 'P':
            if (strcmp(name, "PF") == 0) ctx->EFLAGS.PF = (val ? 1 : 0);
            else return false;
            break;
        case 'Z':
            if (strcmp(name, "ZF") == 0) ctx->EFLAGS.ZF = (val ? 1 : 0);
            else return false;
            break;
        case 'T':
            if (strcmp(name, "TOP") == 0) ctx->FPU.SW.TOP = (val & 7);
            else return false;
            break;
        default:
            return false;
    }
    return true;
}

static bool IsSoftwareBreakpoint(uint32_t eip) {
    uint8_t opcode = (uint8_t)MemRead(eip, 1);
    return (opcode == 0xCC);
}

static void Cmd_StackDump(CPU_Context* ctx, const char* args) {
    int count = 8;
    if (*args) {
        int input_cnt = 0;
        if (sscanf(args, "%d", &input_cnt) == 1 && input_cnt > 0) {
            count = input_cnt;
        }
    }
    int lines_above = 2;
    uint32_t esp = ctx->ESP.I32;
    uint32_t ebp = ctx->EBP.I32;
    printf("Stack Dump (ESP=0x%08X):\n", esp);
    printf("Address     Value       ASCII   Annotation\n");
    printf("----------  ----------  -----   ------------------\n");
    for (int i = -lines_above; i < count; i++) {
        uint32_t addr = (uint32_t)((int64_t)esp + (int64_t)i * 4);
        if (g_StackMemory && (addr < g_StackGuestBase || addr + 4 > g_StackGuestBase + g_StackSize)) {
            if (i == -lines_above) continue;
            printf("0x%08X  [Out of Stack Range]\n", addr);
            break;
        }
        uint32_t val = MemRead(addr, 4);
        char ascii[5];
        for (int b = 0; b < 4; b++) {
            uint8_t byte = (uint8_t)((val >> (b * 8)) & 0xFF);
            ascii[b] = (isprint(byte)) ? (char)byte : '.';
        }
        ascii[4] = '\0';
        printf("0x%08X  0x%08X  %s  ", addr, val, ascii);
        int has_note = 0;
        if (addr == esp) {
            printf("<--- ESP (Top)");
            has_note = 1;
        }
        if (addr == ebp) {
            printf("%s<--- EBP (Frame)", has_note ? " / " : "");
            has_note = 1;
        }
        if (val > esp && val < esp + 0x1000) {
            printf("%s(Ptr to Stack?)", has_note ? " " : "");
        }
        printf("\n");
    }
}

static bool IsStepOverCandidate(DecodeContext* d_ctx) {
    uint8_t op = d_ctx->opcode;
    if (op == 0xE8 || op == 0x9A) return true;
    if (op == 0xFF && d_ctx->reg == 2) return true;
    if (op >= 0xE0 && op <= 0xE2) return true;
    if (d_ctx->pfx_rep || d_ctx->pfx_repne) return true;
    if (op == 0xCD) return true;
    return false;
}

static void Cmd_MemWriteVal(const char* args, int byte_width) {
    uint32_t addr = 0;
    uint32_t val = 0;
    if (sscanf(args, "%x %x", &addr, &val) == 2) {
        MemWrite(addr, val, byte_width);
        printf("Memory Write: [0x%08X] = 0x%X (%d bytes)\n", addr, val, byte_width);
    } else {
        printf("Usage: e%c <hex_addr> <hex_value>\n", (byte_width == 4) ? 'd' : 'w');
    }
}
