// ============================================================================
// core/wasm_bridge.c
// WebAssembly 导出桥接层：直接调用 core/TinyX86.c, core/disasm.c,
// core/PELoader.c, core/PETools.c, core/debugger.c 的原生 C11 实现
// ============================================================================

#include "TinyX86.h"
#include "PELoader.h"
#include "PETools.h"
#include "debugger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __wasm__
#define WASM_EXPORT(name) __attribute__((export_name(name)))
#else
#define WASM_EXPORT(name)
#endif

static CPU_Context g_WasmCpu;
static uint32_t g_CurrentCodeBase = 0x00400000U;
static PEHeaderInfo g_LastPEInfo;
static int g_LastPEValid = 0;
static uint16_t g_LastPEMachine = 0;
static PIMAGE_SECTION_HEADER g_LastSections = NULL;
static int g_LastSectionCount = 0;

// Preset 0: core/main.c 内置递归幂运算 power(2.0, 3) 真实机器码 (83 bytes)
static const uint8_t g_PresetPowerCode[] = {
    0x6A, 0x03,                         // 0x00 | push 3
    0x68, 0x00, 0x00, 0x00, 0x40,       // 0x02 | push 0x40000000 (2.0)
    0x6A, 0x00,                         // 0x07 | push 0x00000000
    0xE8, 0x06, 0x00, 0x00, 0x00,       // 0x09 | call +6 (0x14)
    0x83, 0xC4, 0x0C,                   // 0x0E | add esp, 12
    0xCC,                               // 0x11 | int3
    0x90, 0x90,                         // 0x12 | nop
    0x55,                               // 0x14 | push ebp
    0x8B, 0xEC,                         // 0x15 | mov ebp, esp
    0x83, 0xEC, 0x40,                   // 0x17 | sub esp, 0x40
    0x8D, 0x7D, 0xC0,                   // 0x1A | lea edi, [ebp-40h]
    0xB9, 0x10, 0x00, 0x00, 0x00,       // 0x1D | mov ecx, 10h
    0xB8, 0xCC, 0xCC, 0xCC, 0xCC,       // 0x22 | mov eax, 0xCCCCCCCC
    0xF3, 0xAB,                         // 0x27 | rep stos dword ptr [edi]
    0x53,                               // 0x29 | push ebx
    0x56,                               // 0x2A | push esi
    0x57,                               // 0x2B | push edi
    0x83, 0x7D, 0x10, 0x00,             // 0x2C | cmp dword ptr [ebp+16], 0
    0x7F, 0x04,                         // 0x30 | jg +4 (0x36)
    0xD9, 0xE8,                         // 0x32 | fld1
    0xEB, 0x16,                         // 0x34 | jmp +22 (0x4C)
    0x8B, 0x45, 0x10,                   // 0x36 | mov eax, [ebp+16]
    0x48,                               // 0x39 | dec eax
    0x50,                               // 0x3A | push eax
    0xFF, 0x75, 0x0C,                   // 0x3B | push dword ptr [ebp+12]
    0xFF, 0x75, 0x08,                   // 0x3E | push dword ptr [ebp+8]
    0xE8, 0xCE, 0xFF, 0xFF, 0xFF,       // 0x41 | call -50 (0x14)
    0x83, 0xC4, 0x0C,                   // 0x46 | add esp, 12
    0xDC, 0x4D, 0x08,                   // 0x49 | fmul qword ptr [ebp+8]
    0x5F,                               // 0x4C | pop edi
    0x5E,                               // 0x4D | pop esi
    0x5B,                               // 0x4E | pop ebx
    0x8B, 0xE5,                         // 0x4F | mov esp, ebp
    0x5D,                               // 0x51 | pop ebp
    0xC3                                // 0x52 | ret
};

// Preset 1: 1..10 累加循环 (ALU + LOOP -> EAX = EBX = 55)
static const uint8_t g_PresetSumLoopCode[] = {
    0x31, 0xC0,                         // xor eax, eax
    0xB9, 0x0A, 0x00, 0x00, 0x00,       // mov ecx, 10
    0x01, 0xC8,                         // add eax, ecx
    0xE2, 0xFC,                         // loop -4
    0x89, 0xC3,                         // mov ebx, eax
    0xCC,                               // int3
    0xF4                                // hlt
};

// Preset 2: DOS INT 21h 高层模拟 (AH=09h 字符串输出 + AH=4Ch 退出)
static const uint8_t g_PresetDosInt21Code[] = {
    0xBA, 0x10, 0x00, 0x40, 0x00,       // mov edx, 0x00400010
    0xB4, 0x09,                         // mov ah, 0x09
    0xCD, 0x21,                         // int 0x21
    0xB8, 0x00, 0x4C, 0x00, 0x00,       // mov eax, 0x4C00
    0xCD, 0x21,                         // int 0x21
    0xF4,                               // hlt
    // 0x00400010: "Hello from TinyX86 C11 Engine!$"
    'H','e','l','l','o',' ','f','r','o','m',' ','T','i','n','y','X','8','6',' ','C','1','1',' ','E','n','g','i','n','e','!','$'
};

WASM_EXPORT("wasm_alloc")
void* wasm_alloc(uint32_t size) {
    return malloc(size);
}

WASM_EXPORT("wasm_free")
void wasm_free(void* ptr) {
    free(ptr);
}

WASM_EXPORT("wasm_load_preset")
int wasm_load_preset(int preset_id) {
    UnloadExe(&g_WasmCpu);
    memset(&g_LastPEInfo, 0, sizeof(g_LastPEInfo));
    g_LastPEValid = 0;
    g_LastPEMachine = 0;
    g_CurrentCodeBase = 0x00400000U;
    bool ok = false;
    if (preset_id == 1) {
        ok = LoadMem(g_PresetSumLoopCode, sizeof(g_PresetSumLoopCode), &g_WasmCpu, 0x00400000U);
    } else if (preset_id == 2) {
        ok = LoadMem(g_PresetDosInt21Code, sizeof(g_PresetDosInt21Code), &g_WasmCpu, 0x00400000U);
    } else {
        ok = LoadMem(g_PresetPowerCode, sizeof(g_PresetPowerCode), &g_WasmCpu, 0x00400000U);
    }
    fflush(stdout);
    return ok ? 1 : 0;
}

WASM_EXPORT("wasm_load_bin")
int wasm_load_bin(const uint8_t* data, uint32_t size, uint32_t base_addr) {
    UnloadExe(&g_WasmCpu);
    memset(&g_LastPEInfo, 0, sizeof(g_LastPEInfo));
    g_LastPEValid = 0;
    g_LastPEMachine = 0;
    g_CurrentCodeBase = base_addr ? base_addr : 0x00400000U;
    bool ok = LoadMem(data, size, &g_WasmCpu, g_CurrentCodeBase);
    fflush(stdout);
    return ok ? 1 : 0;
}

WASM_EXPORT("wasm_load_pe")
int wasm_load_pe(const uint8_t* data, uint32_t size) {
    memset(&g_LastPEInfo, 0, sizeof(g_LastPEInfo));
    g_LastPEValid = 0;
    g_LastPEMachine = 0;
    if (g_LastSections) {
        free(g_LastSections);
        g_LastSections = NULL;
    }
    g_LastSectionCount = 0;

    if (ParsePEHeaders((void*)data, &g_LastPEInfo)) {
        g_LastPEValid = 1;
        g_CurrentCodeBase = g_LastPEInfo.dwImageBase ? g_LastPEInfo.dwImageBase : 0x00400000U;
        PIMAGE_DOS_HEADER pDos = (PIMAGE_DOS_HEADER)data;
        PIMAGE_NT_HEADERS32 pNt = (PIMAGE_NT_HEADERS32)(data + pDos->e_lfanew);
        g_LastPEMachine = pNt->FileHeader.Machine;
        PIMAGE_SECTION_HEADER pSec = IMAGE_FIRST_SECTION(pNt);
        g_LastSectionCount = g_LastPEInfo.wNumberOfSections;
        if (g_LastSectionCount > 0 && g_LastSectionCount < 64) {
            g_LastSections = (PIMAGE_SECTION_HEADER)calloc((size_t)g_LastSectionCount, sizeof(IMAGE_SECTION_HEADER));
            if (g_LastSections) {
                memcpy(g_LastSections, pSec, (size_t)g_LastSectionCount * sizeof(IMAGE_SECTION_HEADER));
            }
        }
    }

    bool ok = LoadExeFromMemory(data, size, &g_WasmCpu);
    fflush(stdout);
    return ok ? 1 : 0;
}

// === PE32 头部解析结果读取 (供前端 PE32 标签页直接从 C 结构体读取) ===
WASM_EXPORT("wasm_pe_is_valid")
int wasm_pe_is_valid(void) { return g_LastPEValid; }
WASM_EXPORT("wasm_pe_machine")
uint32_t wasm_pe_machine(void) { return g_LastPEMachine; }
WASM_EXPORT("wasm_pe_num_sections")
uint32_t wasm_pe_num_sections(void) { return g_LastPEInfo.wNumberOfSections; }
WASM_EXPORT("wasm_pe_entry_rva")
uint32_t wasm_pe_entry_rva(void) { return g_LastPEInfo.dwEntryPoint; }
WASM_EXPORT("wasm_pe_image_base")
uint32_t wasm_pe_image_base(void) { return g_LastPEInfo.dwImageBase; }
WASM_EXPORT("wasm_pe_size_of_image")
uint32_t wasm_pe_size_of_image(void) { return g_LastPEInfo.dwSizeOfImage; }
WASM_EXPORT("wasm_pe_size_of_headers")
uint32_t wasm_pe_size_of_headers(void) { return g_LastPEInfo.dwSizeOfHeaders; }

WASM_EXPORT("wasm_pe_get_section")
int wasm_pe_get_section(int idx, char* out_name, uint32_t* out_fields) {
    if (!g_LastSections || idx < 0 || idx >= g_LastSectionCount) return 0;
    if (out_name) {
        memcpy(out_name, g_LastSections[idx].Name, 8);
        out_name[8] = '\0';
    }
    if (out_fields) {
        out_fields[0] = g_LastSections[idx].VirtualAddress;
        out_fields[1] = g_LastSections[idx].Misc.VirtualSize;
        out_fields[2] = g_LastSections[idx].PointerToRawData;
        out_fields[3] = g_LastSections[idx].SizeOfRawData;
        out_fields[4] = g_LastSections[idx].Characteristics;
    }
    return 1;
}

WASM_EXPORT("wasm_get_code_base")
uint32_t wasm_get_code_base(void) {
    return g_CurrentCodeBase;
}

WASM_EXPORT("wasm_exec_command")
int wasm_exec_command(const char* cmd_line) {
    bool quit = DebuggerExecuteCommand(&g_WasmCpu, cmd_line);
    fflush(stdout);
    return quit ? 1 : 0;
}

WASM_EXPORT("wasm_toggle_breakpoint")
int wasm_toggle_breakpoint(uint32_t addr) {
    bool added = DebuggerToggleBreakpoint(addr);
    fflush(stdout);
    return added ? 1 : 0;
}

WASM_EXPORT("wasm_get_breakpoints")
int wasm_get_breakpoints(uint32_t* out_addrs, int max_count) {
    return DebuggerGetBreakpoints(out_addrs, max_count);
}

WASM_EXPORT("wasm_clear_breakpoints")
void wasm_clear_breakpoints(void) {
    DebuggerClearBreakpoints();
}

// === CPU_Context 原生 C 结构体读取与修改接口 ===
WASM_EXPORT("wasm_get_gpr")
uint32_t wasm_get_gpr(int idx) {
    if (idx < 0 || idx >= 8) return 0;
    return g_WasmCpu.GPR[idx].I32;
}

WASM_EXPORT("wasm_get_eip")
uint32_t wasm_get_eip(void) {
    return g_WasmCpu.EIP;
}

WASM_EXPORT("wasm_get_eflags")
uint32_t wasm_get_eflags(void) {
    uint32_t raw = 0x00000002U;
    if (g_WasmCpu.EFLAGS.CF) raw |= (1U << 0);
    if (g_WasmCpu.EFLAGS.PF) raw |= (1U << 2);
    if (g_WasmCpu.EFLAGS.AF) raw |= (1U << 4);
    if (g_WasmCpu.EFLAGS.ZF) raw |= (1U << 6);
    if (g_WasmCpu.EFLAGS.SF) raw |= (1U << 7);
    if (g_WasmCpu.EFLAGS.TF) raw |= (1U << 8);
    if (g_WasmCpu.EFLAGS.IF) raw |= (1U << 9);
    if (g_WasmCpu.EFLAGS.DF) raw |= (1U << 10);
    if (g_WasmCpu.EFLAGS.OF) raw |= (1U << 11);
    raw |= ((uint32_t)(g_WasmCpu.EFLAGS.IOPL & 3U) << 12);
    if (g_WasmCpu.EFLAGS.NT) raw |= (1U << 14);
    return raw;
}

WASM_EXPORT("wasm_get_seg")
uint32_t wasm_get_seg(int idx) {
    if (idx < 0 || idx >= 6) return 0;
    return g_WasmCpu.seg[idx];
}

WASM_EXPORT("wasm_get_fpu_reg")
double wasm_get_fpu_reg(int idx) {
    if (idx < 0 || idx >= 8) return 0.0;
    return g_WasmCpu.FPU.Regs[idx];
}

WASM_EXPORT("wasm_get_fpu_sw")
uint32_t wasm_get_fpu_sw(void) {
    return g_WasmCpu.FPU.SW.Value;
}

WASM_EXPORT("wasm_get_fpu_top")
uint32_t wasm_get_fpu_top(void) {
    return g_WasmCpu.FPU.SW.TOP;
}

WASM_EXPORT("wasm_get_fpu_cw")
uint32_t wasm_get_fpu_cw(void) {
    return g_WasmCpu.FPU.CW.Value;
}

WASM_EXPORT("wasm_get_halted")
int wasm_get_halted(void) {
    return g_WasmCpu.Halted ? 1 : 0;
}

WASM_EXPORT("wasm_set_cpu_state")
int wasm_set_cpu_state(const char* name, uint32_t val) {
    bool ok = SetCpuState(&g_WasmCpu, name, val);
    fflush(stdout);
    return ok ? 1 : 0;
}

WASM_EXPORT("wasm_mem_read")
uint32_t wasm_mem_read(uint32_t addr, int byte_len) {
    return MemRead(addr, byte_len);
}

WASM_EXPORT("wasm_mem_write")
void wasm_mem_write(uint32_t addr, uint32_t val, int byte_len) {
    MemWrite(addr, val, byte_len);
}

// === 原生 C 反汇编接口 (直接调用 core/disasm.c 的 ParseInstuction & FormatInstruction) ===
WASM_EXPORT("wasm_disasm_at")
int wasm_disasm_at(uint32_t addr, char* out_asm, int max_asm_len, uint8_t* out_bytes, int max_bytes) {
    DecodeContext d_ctx;
    uint8_t* code_ptr = TinyX86_TranslateAddress(addr);
    int len = ParseInstuction(code_ptr, addr, &d_ctx);
    FormatInstruction(code_ptr, &d_ctx);
    if (len <= 0) len = 1;

    if (out_asm && max_asm_len > 0) {
        strncpy(out_asm, d_ctx.asm_str, (size_t)(max_asm_len - 1));
        out_asm[max_asm_len - 1] = '\0';
    }
    if (out_bytes && max_bytes > 0) {
        int copy_n = len < max_bytes ? len : max_bytes;
        for (int i = 0; i < copy_n; i++) {
            out_bytes[i] = code_ptr[i];
        }
    }
    return len;
}

// === 原生 C 结构体大小与元信息查询 (验证真实 C11 编译环境) ===
WASM_EXPORT("wasm_get_sizeof_cpu_context")
uint32_t wasm_get_sizeof_cpu_context(void) {
    return (uint32_t)sizeof(CPU_Context);
}

WASM_EXPORT("wasm_get_sizeof_decode_context")
uint32_t wasm_get_sizeof_decode_context(void) {
    return (uint32_t)sizeof(DecodeContext);
}

// === 直接在 C 语言中运行 tests/cpu_integration_tests.c 的完整集成测试 ===
WASM_EXPORT("wasm_run_c_integration_tests")
int wasm_run_c_integration_tests(int* out_checks, int* out_failures) {
    static uint32_t test_stack[64];
    static uint8_t alu[64] = {0xB8,40,0,0,0, 0x83,0xC0,2, 0x50,0x5B, 0x31,0xC9, 0x83,0xFB,42};
    static uint8_t branch[64] = {0xEB,5, 0xB8,99,0,0,0, 0xB8,42,0,0,0};
    static uint8_t fpu[64] = {0xD9,0xE8, 0xD9,0xE8, 0xDE,0xC1};

    CPU_Context cpu;
    DecodeContext decoded;
    int checks = 0;
    int failures = 0;

    #define C_CHECK(expr) do { ++checks; if (!(expr)) { ++failures; } } while (0)

    // Suite 1: Decode + ALU + Stack + EFLAGS
    memset(&cpu, 0, sizeof(cpu));
    memset(test_stack, 0, sizeof(test_stack));
    TinyX86_ClearRegions();
    TinyX86_RegisterRegion(0x00400000U, alu, sizeof(alu));
    TinyX86_RegisterRegion(0x00100000U, test_stack, sizeof(test_stack));
    cpu.EIP = 0x00400000U;
    cpu.ESP.I32 = 0x00100000U + (uint32_t)sizeof(test_stack);
    cpu.FPU.CW.Value = 0x037F;

    C_CHECK(ParseInstuction(alu, cpu.EIP, &decoded) == 5);
    C_CHECK(decoded.instr_len == 5);
    C_CHECK(runcpu(&cpu, 6) == 0);
    C_CHECK(cpu.EAX.I32 == 42);
    C_CHECK(cpu.EBX.I32 == 42);
    C_CHECK(cpu.ECX.I32 == 0);
    C_CHECK(cpu.EFLAGS.ZF == 1);
    C_CHECK(cpu.ESP.I32 == 0x00100000U + (uint32_t)sizeof(test_stack));
    C_CHECK(cpu.EIP == 0x00400000U + 15U);

    // Suite 2: Relative Branch (JMP rel8)
    memset(&cpu, 0, sizeof(cpu));
    memset(test_stack, 0, sizeof(test_stack));
    TinyX86_ClearRegions();
    TinyX86_RegisterRegion(0x00400000U, branch, sizeof(branch));
    TinyX86_RegisterRegion(0x00100000U, test_stack, sizeof(test_stack));
    cpu.EIP = 0x00400000U;
    cpu.ESP.I32 = 0x00100000U + (uint32_t)sizeof(test_stack);
    cpu.FPU.CW.Value = 0x037F;

    C_CHECK(runcpu(&cpu, 2) == 0);
    C_CHECK(cpu.EAX.I32 == 42);
    C_CHECK(cpu.EIP == 0x00400000U + 12U);

    // Suite 3: x87 FPU Ring Stack (FLD1 + FLD1 + FADDP)
    memset(&cpu, 0, sizeof(cpu));
    memset(test_stack, 0, sizeof(test_stack));
    TinyX86_ClearRegions();
    TinyX86_RegisterRegion(0x00400000U, fpu, sizeof(fpu));
    TinyX86_RegisterRegion(0x00100000U, test_stack, sizeof(test_stack));
    cpu.EIP = 0x00400000U;
    cpu.ESP.I32 = 0x00100000U + (uint32_t)sizeof(test_stack);
    cpu.FPU.CW.Value = 0x037F;

    C_CHECK(runcpu(&cpu, 3) == 0);
    C_CHECK(cpu.FPU.Regs[cpu.FPU.SW.TOP] == 2.0);

    #undef C_CHECK

    if (out_checks) *out_checks = checks;
    if (out_failures) *out_failures = failures;
    return failures == 0 ? 1 : 0;
}

int main(void) {
    return 0;
}
