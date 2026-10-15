#include "TinyX86.h"
#include <stdio.h>
#include <string.h>

static int failures;
static int checks;
#define CHECK(expr) do { ++checks; if (!(expr)) { ++failures; printf("FAIL line %d: %s\n", __LINE__, #expr); } } while (0)
static uint32_t stack[64];
static void init(CPU_Context* cpu, uint8_t* code) {
    memset(cpu, 0, sizeof(*cpu));
    cpu->EIP = (uint32_t)(uintptr_t)code;
    cpu->ESP.I32 = (uint32_t)(uintptr_t)(stack + 64);
}
int main(void) {
    /* Extra bytes make the current decoder's 15-byte lookahead readable. */
    static uint8_t alu[64] = {0xB8,40,0,0,0, 0x83,0xC0,2, 0x50,0x5B, 0x31,0xC9, 0x83,0xFB,42};
    static uint8_t branch[64] = {0xEB,5, 0xB8,99,0,0,0, 0xB8,42,0,0,0};
    static uint8_t fpu[64] = {0xD9,0xE8, 0xD9,0xE8, 0xDE,0xC1};
    CPU_Context cpu;
    DecodeContext decoded;
    init(&cpu, alu);
    CHECK(ParseInstuction(alu, cpu.EIP, &decoded) == 5);
    CHECK(decoded.instr_len == 5);
    CHECK(runcpu(&cpu, 6) == 0);
    CHECK(cpu.EAX.I32 == 42);
    CHECK(cpu.EBX.I32 == 42);
    CHECK(cpu.ECX.I32 == 0);
    CHECK(cpu.EFLAGS.ZF == 1);
    CHECK(cpu.ESP.I32 == (uint32_t)(uintptr_t)(stack + 64));
    CHECK(cpu.EIP == (uint32_t)(uintptr_t)(alu + 15));
    init(&cpu, branch);
    CHECK(runcpu(&cpu, 2) == 0);
    CHECK(cpu.EAX.I32 == 42);
    CHECK(cpu.EIP == (uint32_t)(uintptr_t)(branch + 12));
    init(&cpu, fpu);
    CHECK(runcpu(&cpu, 3) == 0);
    CHECK(cpu.FPU.Regs[cpu.FPU.SW.TOP] == 2.0);
    printf("TinyX86 + shared decoder: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
