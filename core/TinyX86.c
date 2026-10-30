// x86-emulator_v1.0.cpp : 此文件包含 "main" 函数。程序执行将在此处开始并结束。
// 跨平台增强版：支持 Windows (x86/x64)、Linux (x86_64/ARM64)、macOS (Intel/Apple Silicon)

#include "TinyX86.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

// ============================================================================
// 跨平台虚拟内存映射表 (Guest 32-bit VA <-> Host Pointer)
// 兼容 32位 Host-Passthrough 与 64位 Linux/macOS/Windows 宿主环境
// ============================================================================
#define MAX_MEM_REGIONS 16
typedef struct {
    uint32_t guest_base;
    uint32_t size;
    uint8_t* host_ptr;
    bool active;
} MemRegion;

static MemRegion g_Regions[MAX_MEM_REGIONS];

void TinyX86_ClearRegions(void) {
    memset(g_Regions, 0, sizeof(g_Regions));
}

void TinyX86_RegisterRegion(uint32_t guest_base, void* host_ptr, uint32_t size) {
    if (!host_ptr || size == 0) return;
    for (int i = 0; i < MAX_MEM_REGIONS; i++) {
        if (g_Regions[i].active && g_Regions[i].guest_base == guest_base) {
            g_Regions[i].host_ptr = (uint8_t*)host_ptr;
            g_Regions[i].size = size;
            return;
        }
    }
    for (int i = 0; i < MAX_MEM_REGIONS; i++) {
        if (!g_Regions[i].active) {
            g_Regions[i].guest_base = guest_base;
            g_Regions[i].size = size;
            g_Regions[i].host_ptr = (uint8_t*)host_ptr;
            g_Regions[i].active = true;
            return;
        }
    }
}

uint8_t* TinyX86_TranslateAddress(uint32_t guest_addr) {
    for (int i = 0; i < MAX_MEM_REGIONS; i++) {
        if (g_Regions[i].active) {
            uint32_t base = g_Regions[i].guest_base;
            uint32_t end = base + g_Regions[i].size;
            if (guest_addr >= base && guest_addr < end) {
                return g_Regions[i].host_ptr + (guest_addr - base);
            }
        }
    }
    // 32位宿主环境下支持直接 Host-Passthrough 回退
    return (uint8_t*)(uintptr_t)guest_addr;
}

// === 执行与调度 ===
static bool ExecuteInstruction(CPU_Context* ctx, DecodeContext* d_ctx); // 执行指令，返回是否跳转

// === 寄存器/内存与操作数辅助 ===
static uint32_t ReadGPR(CPU_Context* ctx, int reg_index, int size); // 读取通用寄存器
static bool WriteGPR(CPU_Context* ctx, int reg_index, int size, uint32_t value); // 写入通用寄存器
static uint32_t GetEffectiveAddress(CPU_Context* ctx, DecodeContext* d_ctx); // 计算有效地址
static uint32_t GetOperandValue(CPU_Context* ctx, DecodeContext* d_ctx, int op_idx); // 获取操作数值
static void SetOperandValue(CPU_Context* ctx, DecodeContext* d_ctx, int op_idx, uint32_t value); // 写入操作数值
static int GetOperandBitSize(CPU_Context* ctx, DecodeContext* d_ctx, OperandType type); // 获取操作数位宽

// === 标志位与算术逻辑 ===
static int CalcPF(uint8_t res); // 计算奇偶校验位
static void UpdateEFLAGS(CPU_Context* ctx, uint32_t res, uint32_t dest, uint32_t src, int size, ALU_Op op); // 更新 EFLAGS
static void UpdateLogicFlags(CPU_Context* ctx, uint32_t res, int size); // 更新逻辑标志位
static void Exec_ALU_Generic(CPU_Context* ctx, DecodeContext* d_ctx, ALU_Op op, bool is_compare); // 通用 ALU
static void Exec_Group1(CPU_Context* ctx, DecodeContext* d_ctx); // Group 1 立即数 ALU
static void Exec_Group2(CPU_Context* ctx, DecodeContext* d_ctx); // Group 2 移位/旋转
static uint32_t GetGroup3Source(CPU_Context* ctx, DecodeContext* d_ctx); // Group 3 源操作数
static void SetGroup3Dest(CPU_Context* ctx, DecodeContext* d_ctx, uint32_t val); // Group 3 写回
static void Exec_Group3(CPU_Context* ctx, DecodeContext* d_ctx); // Group 3 乘除类
static void Exec_Group4(CPU_Context* ctx, DecodeContext* d_ctx); // Group 4 INC/DEC
static bool Exec_Group5(CPU_Context* ctx, DecodeContext* d_ctx); // Group 5 控制/栈
static void Exec_IMUL_2_Op(CPU_Context* ctx, DecodeContext* d_ctx); // IMUL 双操作数
static void Exec_INC(CPU_Context* ctx, DecodeContext* d_ctx); // INC
static void Exec_DEC(CPU_Context* ctx, DecodeContext* d_ctx); // DEC

// === 数据传送与扩展 ===
static void Exec_MOV_Generic(CPU_Context* ctx, DecodeContext* d_ctx); // MOV r, imm
static void Exec_MOVZX(CPU_Context* ctx, DecodeContext* d_ctx, int src_bits); // MOVZX 零扩展
static void Exec_MOVSX(CPU_Context* ctx, DecodeContext* d_ctx, int src_bits); // MOVSX 符号扩展
static void Exec_SignExtend(CPU_Context* ctx, DecodeContext* d_ctx); // CBW/CWDE/CWD/CDQ
static void Exec_LEA(CPU_Context* ctx, DecodeContext* d_ctx); // LEA
static void Exec_XCHG(CPU_Context* ctx, DecodeContext* d_ctx); // XCHG

// === 栈与标志位栈操作 ===
static void Exec_PUSH(CPU_Context* ctx, DecodeContext* d_ctx); // PUSH
static void Exec_POP(CPU_Context* ctx, DecodeContext* d_ctx); // POP
static void Exec_PUSHA(CPU_Context* ctx, DecodeContext* d_ctx); // PUSHA
static void Exec_POPA(CPU_Context* ctx, DecodeContext* d_ctx); // POPA
static void Exec_PUSHF(CPU_Context* ctx); // PUSHF
static void Exec_POPF(CPU_Context* ctx); // POPF
static void Exec_ENTER(CPU_Context* ctx, DecodeContext* d_ctx); // ENTER
static void Exec_LEAVE(CPU_Context* ctx); // LEAVE
static uint32_t PackEFLAGS(CPU_Context* ctx); // 打包 EFLAGS
static void UnpackEFLAGS(CPU_Context* ctx, uint32_t val); // 解包 EFLAGS

// === 条件/控制流 ===
static bool CheckCondition(CPU_Context* ctx, uint8_t condition_code); // 检查条件码
static bool Exec_Branch(CPU_Context* ctx, DecodeContext* d_ctx, bool is_conditional); // 相对跳转
static bool Exec_CALL(CPU_Context* ctx, DecodeContext* d_ctx); // CALL
static bool Exec_RET(CPU_Context* ctx, DecodeContext* d_ctx); // RET
static bool Exec_LOOP(CPU_Context* ctx, DecodeContext* d_ctx); // LOOP
static void Exec_SETcc(CPU_Context* ctx, DecodeContext* d_ctx); // SETcc
static void Exec_CMOVcc(CPU_Context* ctx, DecodeContext* d_ctx); // CMOVcc
static void Exec_INT(CPU_Context* ctx, DecodeContext* d_ctx); // INT
static void Exec_SAHF(CPU_Context* ctx); // SAHF
static void Exec_LAHF(CPU_Context* ctx); // LAHF

// === 串操作 ===
static void Exec_StringOp(CPU_Context* ctx, DecodeContext* d_ctx); // 串操作
static bool Exec_REP_StringOp(CPU_Context* ctx, DecodeContext* d_ctx); // REP 串操作

// === FPU ===
static void FPU_PUSH(CPU_Context* ctx, double val); // FPU 压栈
static double FPU_POP(CPU_Context* ctx); // FPU 弹栈
static double MemReadF(uint32_t addr, int size); // 读取浮点内存
static void MemWriteF(uint32_t addr, double val, int size); // 写入浮点内存
static void UpdateFPUFlags(CPU_Context* ctx, double v1, double v2); // 更新 FPU 比较标志
static double MemReadI(uint32_t addr, int size); // 读取整数内存为 double
static void MemWriteI(uint32_t addr, double val, int size); // 写入 double 为整数
static void Exec_FPU(CPU_Context* ctx, DecodeContext* d_ctx); // FPU 指令执行器

int OffsetByte[] = {
    0 + 4 * 0,
    0 + 4 * 1,
    0 + 4 * 2,
    0 + 4 * 3,
    1 + 4 * 0,
    1 + 4 * 1,
    1 + 4 * 2,
    1 + 4 * 3
};

int runcpu(CPU_Context* p, int step) {
    DecodeContext d_ctx;
    int instr_len = 0;
    for (int i = 0; i < step; i++) {
        // 0. 检查是否停机
        if (p->Halted) {
            printf("[System] CPU Halted.\n");
            break;
        }
        // 1. 取指与解码 (Fetch & Decode)
        uint8_t* code_ptr = TinyX86_TranslateAddress(p->EIP);
        instr_len = ParseInstuction(code_ptr, p->EIP, &d_ctx);
        FormatInstruction(code_ptr, &d_ctx);
        if (instr_len <= 0) {
            printf("Error: Decoding failed at EIP: 0x%08X\n", p->EIP);
            return -1;
        }
        // 2. 执行指令 (Execute)
        bool jumped = ExecuteInstruction(p, &d_ctx);
        // 3. 更新 EIP 指向下一条指令
        if (!jumped) p->EIP += instr_len;
    }
    return 0;
}

// 执行分发器
static bool ExecuteInstruction(CPU_Context* ctx, DecodeContext* d_ctx) {
    bool jumped = false;

    // ========================================================
    // 分支 A: 处理双字节指令 (0x0F ...)
    // ========================================================
    if (d_ctx->is_two_byte_opcode) {
        switch (d_ctx->opcode) {
            // --- CMOVcc (0x40 - 0x4F) ---
            case 0x40: case 0x41: case 0x42: case 0x43:
            case 0x44: case 0x45: case 0x46: case 0x47:
            case 0x48: case 0x49: case 0x4A: case 0x4B:
            case 0x4C: case 0x4D: case 0x4E: case 0x4F:
                Exec_CMOVcc(ctx, d_ctx); break;

            // Long Conditional Jumps (0x80 - 0x8F)
            case 0x80: case 0x81: case 0x82: case 0x83:
            case 0x84: case 0x85: case 0x86: case 0x87:
            case 0x88: case 0x89: case 0x8A: case 0x8B:
            case 0x8C: case 0x8D: case 0x8E: case 0x8F:
                jumped = Exec_Branch(ctx, d_ctx, true);
                break;

            // SETcc (0x90 - 0x9F)
            case 0x90: case 0x91: case 0x92: case 0x93:
            case 0x94: case 0x95: case 0x96: case 0x97:
            case 0x98: case 0x99: case 0x9A: case 0x9B:
            case 0x9C: case 0x9D: case 0x9E: case 0x9F:
                Exec_SETcc(ctx, d_ctx); break;

            // FS/GS PUSH/POP (0x0F A0 - 0x0F A9)
            case 0xA0: case 0xA8:
                Exec_PUSH(ctx, d_ctx); break;
            case 0xA1: case 0xA9:
                Exec_POP(ctx, d_ctx); break;

            // IMUL (双操作数 Gv, Ev)
            case 0xAF:
                Exec_IMUL_2_Op(ctx, d_ctx); break;
            // MOVZX (0xB6, 0xB7)
            case 0xB6: Exec_MOVZX(ctx, d_ctx, 8);  break;
            case 0xB7: Exec_MOVZX(ctx, d_ctx, 16); break;
            // MOVSX (0xBE, 0xBF)
            case 0xBE: Exec_MOVSX(ctx, d_ctx, 8);  break;
            case 0xBF: Exec_MOVSX(ctx, d_ctx, 16); break;

            default:
                printf("[CPU Error] Unimplemented 2-byte opcode: 0x0F %02X\n", d_ctx->opcode);
                break;
        }
        return jumped;
    }

    // ========================================================
    // 分支 B: 处理单字节指令
    // ========================================================
    switch (d_ctx->opcode) {
        // Group A: 数据传送 (Data Transfer)
        case 0x88: case 0x89: case 0x8A: case 0x8B:
        case 0x8C:
        case 0x8E:
        case 0xB0: case 0xB1: case 0xB2: case 0xB3:
        case 0xB4: case 0xB5: case 0xB6: case 0xB7:
        case 0xB8: case 0xB9: case 0xBA: case 0xBB:
        case 0xBC: case 0xBD: case 0xBE: case 0xBF:
        case 0xC6: case 0xC7:
            Exec_MOV_Generic(ctx, d_ctx); break;

        case 0xA0: // MOV AL, moffs8
            ctx->GPR[0].I8.L = (uint8_t)MemRead((uint32_t)d_ctx->imm, 1);
            break;

        case 0xA1: // MOV EAX, moffs16/32
        {
            int size = (d_ctx->pfx_op_size == 0x66) ? 2 : 4;
            if (size == 2) ctx->GPR[0].I16 = (uint16_t)MemRead((uint32_t)d_ctx->imm, 2);
            else ctx->GPR[0].I32 = MemRead((uint32_t)d_ctx->imm, 4);
        }
        break;
        case 0xA2: // MOV moffs8, AL
            MemWrite((uint32_t)d_ctx->imm, ctx->GPR[0].I8.L, 1);
            break;
        case 0xA3: // MOV moffs16/32, EAX
        {
            int size = (d_ctx->pfx_op_size == 0x66) ? 2 : 4;
            if (size == 2) MemWrite((uint32_t)d_ctx->imm, ctx->GPR[0].I16, 2);
            else MemWrite((uint32_t)d_ctx->imm, ctx->GPR[0].I32, 4);
        }
        break;

        case 0x8D: Exec_LEA(ctx, d_ctx); break;
        case 0x90: break; // NOP
        case 0x86: case 0x87:
        case 0x91: case 0x92: case 0x93: case 0x94: case 0x95: case 0x96: case 0x97:
            Exec_XCHG(ctx, d_ctx); break;

        case 0x98: // CBW / CWDE
        case 0x99: // CWD / CDQ
            Exec_SignExtend(ctx, d_ctx); break;

        // Group B: 栈操作 (Stack)
        case 0x50: case 0x51: case 0x52: case 0x53: case 0x54: case 0x55: case 0x56: case 0x57:
        case 0x68: case 0x6A:
        case 0x06: case 0x0E: case 0x16: case 0x1E:
            Exec_PUSH(ctx, d_ctx); break;
        case 0x58: case 0x59: case 0x5A: case 0x5B: case 0x5C: case 0x5D: case 0x5E: case 0x5F:
        case 0x07: case 0x17: case 0x1F:
            Exec_POP(ctx, d_ctx); break;
        case 0x60:
            Exec_PUSHA(ctx, d_ctx); break;
        case 0x61:
            Exec_POPA(ctx, d_ctx); break;
        case 0x9C:
            Exec_PUSHF(ctx); break;
        case 0x9D:
            Exec_POPF(ctx); break;
        case 0xC8:
            Exec_ENTER(ctx, d_ctx); break;
        case 0xC9:
            Exec_LEAVE(ctx); break;

        // Group C: 算术运算 (ALU)
        case 0x00: case 0x01: case 0x02: case 0x03: case 0x04: case 0x05:
            Exec_ALU_Generic(ctx, d_ctx, ALU_ADD, false); break;
        case 0x08: case 0x09: case 0x0A: case 0x0B: case 0x0C: case 0x0D:
            Exec_ALU_Generic(ctx, d_ctx, ALU_OR, false); break;
        case 0x10: case 0x11: case 0x12: case 0x13: case 0x14: case 0x15:
            Exec_ALU_Generic(ctx, d_ctx, ALU_ADC, false); break;
        case 0x18: case 0x19: case 0x1A: case 0x1B: case 0x1C: case 0x1D:
            Exec_ALU_Generic(ctx, d_ctx, ALU_SBB, false); break;
        case 0x20: case 0x21: case 0x22: case 0x23: case 0x24: case 0x25:
            Exec_ALU_Generic(ctx, d_ctx, ALU_AND, false); break;
        case 0x28: case 0x29: case 0x2A: case 0x2B: case 0x2C: case 0x2D:
            Exec_ALU_Generic(ctx, d_ctx, ALU_SUB, false); break;
        case 0x30: case 0x31: case 0x32: case 0x33: case 0x34: case 0x35:
            Exec_ALU_Generic(ctx, d_ctx, ALU_XOR, false); break;
        case 0x38: case 0x39: case 0x3A: case 0x3B: case 0x3C: case 0x3D:
            Exec_ALU_Generic(ctx, d_ctx, ALU_CMP, true); break;
        case 0x80: case 0x81: case 0x82: case 0x83:
            Exec_Group1(ctx, d_ctx); break;
        case 0x84: case 0x85: case 0xA8: case 0xA9:
            Exec_ALU_Generic(ctx, d_ctx, ALU_AND, true); break;
        case 0x40: case 0x41: case 0x42: case 0x43: case 0x44: case 0x45: case 0x46: case 0x47:
            Exec_INC(ctx, d_ctx); break;
        case 0x48: case 0x49: case 0x4A: case 0x4B: case 0x4C: case 0x4D: case 0x4E: case 0x4F:
            Exec_DEC(ctx, d_ctx); break;
        case 0xF6: case 0xF7:
            Exec_Group3(ctx, d_ctx); break;
        case 0xFE:
            Exec_Group4(ctx, d_ctx); break;

        // Group D: 移位操作 (Shift/Rotate)
        case 0xC0: case 0xC1:
        case 0xD0: case 0xD1:
        case 0xD2: case 0xD3:
            Exec_Group2(ctx, d_ctx); break;

        // Group E: 串操作 (String)
        case 0xA4: case 0xA5:
        case 0xA6: case 0xA7:
        case 0xAA: case 0xAB:
        case 0xAC: case 0xAD:
        case 0xAE: case 0xAF:
            if (d_ctx->pfx_rep || d_ctx->pfx_repne) { return Exec_REP_StringOp(ctx, d_ctx); }
            else { Exec_StringOp(ctx, d_ctx); return false; }

        // Group F: 控制流 (Control Flow)
        case 0x70: case 0x71: case 0x72: case 0x73:
        case 0x74: case 0x75: case 0x76: case 0x77:
        case 0x78: case 0x79: case 0x7A: case 0x7B:
        case 0x7C: case 0x7D: case 0x7E: case 0x7F:
            jumped = Exec_Branch(ctx, d_ctx, true);
            break;
        case 0xE9:
        case 0xEB:
            jumped = Exec_Branch(ctx, d_ctx, false);
            break;
        case 0xE3:
            if (ctx->ECX.I32 == 0) {
                int8_t offset = (int8_t)d_ctx->imm;
                ctx->EIP += d_ctx->instr_len + offset;
                jumped = true;
            }
            break;
        case 0xE8:
            jumped = Exec_CALL(ctx, d_ctx);
            break;
        case 0xC3:
        case 0xC2:
            jumped = Exec_RET(ctx, d_ctx);
            break;
        case 0xFF:
            jumped = Exec_Group5(ctx, d_ctx); break;
        case 0xE0: case 0xE1: case 0xE2:
            jumped = Exec_LOOP(ctx, d_ctx);
            break;

        // Group G: 系统与 FPU (System & FPU)
        case 0xCC:
        case 0xCD:
            Exec_INT(ctx, d_ctx); break;
        case 0xD8: case 0xD9: case 0xDA: case 0xDB:
        case 0xDC: case 0xDD: case 0xDE: case 0xDF:
            Exec_FPU(ctx, d_ctx);
            break;
        case 0x9E:
            Exec_SAHF(ctx); break;
        case 0x9F:
            Exec_LAHF(ctx); break;
        case 0xF5:
            ctx->EFLAGS.CF ^= 1; break; // CMC
        case 0xF8:
            ctx->EFLAGS.CF = 0; break;  // CLC
        case 0xF9:
            ctx->EFLAGS.CF = 1; break;  // STC
        case 0xFA:
            ctx->EFLAGS.IF = 0; break;  // CLI
        case 0xFB:
            ctx->EFLAGS.IF = 1; break;  // STI
        case 0xFC:
            ctx->EFLAGS.DF = 0; break;  // CLD
        case 0xFD:
            ctx->EFLAGS.DF = 1; break;  // STD
        case 0xF4: // HLT
            ctx->Halted = true; break;

        default:
            printf("[CPU] Unimplemented Opcode: 0x%02X\n", d_ctx->opcode);
            ctx->Halted = true;
            break;
    }
    return jumped;
}

static uint32_t ReadGPR(CPU_Context* ctx, int reg_index, int size) {
    if (reg_index < 0 || reg_index >= 8) return 0;

    if (size == 32) {
        return ctx->GPR[reg_index].I32;
    } else if (size == 16) {
        return ctx->GPR[reg_index].I16;
    } else if (size == 8) {
        if (reg_index < 4) {
            return ctx->GPR[reg_index].I8.L;
        } else {
            return ctx->GPR[reg_index - 4].I8.H;
        }
    }
    return 0;
}

static bool WriteGPR(CPU_Context* ctx, int reg_index, int size, uint32_t value) {
    if (reg_index < 0 || reg_index >= 8) return false;

    if (size == 32) {
        ctx->GPR[reg_index].I32 = value;
    } else if (size == 16) {
        ctx->GPR[reg_index].I16 = (uint16_t)value;
    } else if (size == 8) {
        if (reg_index < 4) {
            ctx->GPR[reg_index].I8.L = (uint8_t)value;
        } else {
            ctx->GPR[reg_index - 4].I8.H = (uint8_t)value;
        }
    }
    return true;
}

static uint32_t GetEffectiveAddress(CPU_Context* ctx, DecodeContext* d_ctx) {
    uint32_t base = 0;
    uint32_t index = 0;
    if (d_ctx->has_sib) {
        if (!(d_ctx->base == 5 && d_ctx->mod == 0)) {
            base = ReadGPR(ctx, d_ctx->base, 32);
        }
        if (d_ctx->index != 4) {
            index = ReadGPR(ctx, d_ctx->index, 32);
        }
    } else {
        if (d_ctx->mod == 0 && d_ctx->rm == 5) {
            base = 0;
        } else {
            base = ReadGPR(ctx, d_ctx->rm, 32);
        }
    }
    return base + index * (1U << d_ctx->scale) + (uint32_t)d_ctx->disp;
}

static uint32_t GetOperandValue(CPU_Context* ctx, DecodeContext* d_ctx, int op_idx) {
    OperandType type;
    if (op_idx == 0) type = d_ctx->entry.op1;
    else if (op_idx == 1) type = d_ctx->entry.op2;
    else type = d_ctx->entry.op3;

    int op_size = (d_ctx->pfx_op_size == 0x66) ? 16 : 32;
    switch (type) {
        case Gb: return ReadGPR(ctx, d_ctx->reg, 8);
        case Gw: return ReadGPR(ctx, d_ctx->reg, 16);
        case Gv: return ReadGPR(ctx, d_ctx->reg, op_size);
        case OP_AL: return ReadGPR(ctx, 0, 8);
        case OP_CL: return ReadGPR(ctx, 1, 8);
        case OP_DL: return ReadGPR(ctx, 2, 8);
        case OP_BL: return ReadGPR(ctx, 3, 8);
        case OP_AH: return ReadGPR(ctx, 4, 8);
        case OP_CH: return ReadGPR(ctx, 5, 8);
        case OP_DH: return ReadGPR(ctx, 6, 8);
        case OP_BH: return ReadGPR(ctx, 7, 8);
        case OP_DX: return ReadGPR(ctx, 2, 16);
        case OP_rAX: return ReadGPR(ctx, 0, op_size);
        case OP_rCX: return ReadGPR(ctx, 1, op_size);
        case OP_rDX: return ReadGPR(ctx, 2, op_size);
        case OP_rBX: return ReadGPR(ctx, 3, op_size);
        case OP_rSP: return ReadGPR(ctx, 4, op_size);
        case OP_rBP: return ReadGPR(ctx, 5, op_size);
        case OP_rSI: return ReadGPR(ctx, 6, op_size);
        case OP_rDI: return ReadGPR(ctx, 7, op_size);

        case Sw:
            if (d_ctx->reg < 6) return ctx->seg[d_ctx->reg];
            return 0;

        case OP_ES: return ctx->ES;
        case OP_CS: return ctx->CS;
        case OP_SS: return ctx->SS;
        case OP_DS: return ctx->DS;
        case OP_FS: return ctx->FS;
        case OP_GS: return ctx->GS;

        case Eb:
        case Ew:
        case Ev: {
            if (d_ctx->mod == 3) {
                int size_E = (type == Eb) ? 8 : ((type == Ew) ? 16 : op_size);
                return ReadGPR(ctx, d_ctx->rm, size_E);
            } else {
                uint32_t addr = GetEffectiveAddress(ctx, d_ctx);
                int uint8_t_len = (type == Eb) ? 1 : ((type == Ew) ? 2 : (op_size / 8));
                return MemRead(addr, uint8_t_len);
            }
        }
        case Ib: case Iw: case Iv: case Iz:
            return (uint32_t)d_ctx->imm;

        default:
            return 0;
    }
}

static void SetOperandValue(CPU_Context* ctx, DecodeContext* d_ctx, int op_idx, uint32_t value) {
    OperandType type = (op_idx == 0) ? d_ctx->entry.op1 : d_ctx->entry.op2;
    int op_size = (d_ctx->pfx_op_size == 0x66) ? 16 : 32;
    switch (type) {
        case Gb: WriteGPR(ctx, d_ctx->reg, 8, value); break;
        case Gw: WriteGPR(ctx, d_ctx->reg, 16, value); break;
        case Gv: WriteGPR(ctx, d_ctx->reg, op_size, value); break;
        case Eb:
        case Ew:
        case Ev: {
            if (d_ctx->mod == 3) {
                int size_E = (type == Eb) ? 8 : ((type == Ew) ? 16 : op_size);
                WriteGPR(ctx, d_ctx->rm, size_E, value);
            } else {
                uint32_t addr = GetEffectiveAddress(ctx, d_ctx);
                int uint8_t_len = (type == Eb) ? 1 : ((type == Ew) ? 2 : (op_size / 8));
                MemWrite(addr, value, uint8_t_len);
            }
            break;
        }
        case OP_AL: WriteGPR(ctx, 0, 8, value); break;
        case OP_CL: WriteGPR(ctx, 1, 8, value); break;
        case OP_DL: WriteGPR(ctx, 2, 8, value); break;
        case OP_BL: WriteGPR(ctx, 3, 8, value); break;
        case OP_AH: WriteGPR(ctx, 4, 8, value); break;
        case OP_CH: WriteGPR(ctx, 5, 8, value); break;
        case OP_DH: WriteGPR(ctx, 6, 8, value); break;
        case OP_BH: WriteGPR(ctx, 7, 8, value); break;
        case OP_DX: WriteGPR(ctx, 2, 16, value); break;
        case OP_rAX: WriteGPR(ctx, 0, op_size, value); break;
        case OP_rCX: WriteGPR(ctx, 1, op_size, value); break;
        case OP_rDX: WriteGPR(ctx, 2, op_size, value); break;
        case OP_rBX: WriteGPR(ctx, 3, op_size, value); break;
        case OP_rSP: WriteGPR(ctx, 4, op_size, value); break;
        case OP_rBP: WriteGPR(ctx, 5, op_size, value); break;
        case OP_rSI: WriteGPR(ctx, 6, op_size, value); break;
        case OP_rDI: WriteGPR(ctx, 7, op_size, value); break;
        case Sw:
            if (d_ctx->reg < 6) ctx->seg[d_ctx->reg] = (uint16_t)value;
            break;
        case OP_ES: ctx->ES = (uint16_t)value; break;
        case OP_CS: ctx->CS = (uint16_t)value; break;
        case OP_SS: ctx->SS = (uint16_t)value; break;
        case OP_DS: ctx->DS = (uint16_t)value; break;
        case OP_FS: ctx->FS = (uint16_t)value; break;
        case OP_GS: ctx->GS = (uint16_t)value; break;
        default: break;
    }
}

static void Exec_MOV_Generic(CPU_Context* ctx, DecodeContext* d_ctx) {
    uint32_t src = GetOperandValue(ctx, d_ctx, 1);
    SetOperandValue(ctx, d_ctx, 0, src);
}

static int CalcPF(uint8_t res) {
    int count = 0;
    for (int i = 0; i < 8; i++) {
        if (res & (1 << i)) count++;
    }
    return (count % 2 == 0) ? 1 : 0;
}

static void UpdateEFLAGS(CPU_Context* ctx, uint32_t res, uint32_t dest, uint32_t src, int size, ALU_Op op) {
    uint32_t sign_mask = (size == 8) ? 0x80U : (size == 16) ? 0x8000U : 0x80000000U;
    uint32_t truncated_res = res;
    if (size == 8) truncated_res &= 0xFFU;
    else if (size == 16) truncated_res &= 0xFFFFU;

    ctx->EFLAGS.ZF = (truncated_res == 0) ? 1 : 0;
    ctx->EFLAGS.SF = (truncated_res & sign_mask) ? 1 : 0;
    ctx->EFLAGS.PF = CalcPF((uint8_t)truncated_res);

    if (op == ALU_AND || op == ALU_OR || op == ALU_XOR) {
        ctx->EFLAGS.CF = 0;
        ctx->EFLAGS.OF = 0;
        ctx->EFLAGS.AF = 0;
    } else if (op == ALU_ADD || op == ALU_ADC) {
        if (size == 32) ctx->EFLAGS.CF = (truncated_res < dest);
        else ctx->EFLAGS.CF = ((res & (sign_mask << 1)) != 0);
        ctx->EFLAGS.OF = (((dest ^ truncated_res) & (src ^ truncated_res) & sign_mask) != 0);
        ctx->EFLAGS.AF = (((dest ^ src ^ truncated_res) & 0x10U) != 0);
    } else if (op == ALU_SUB || op == ALU_CMP || op == ALU_SBB) {
        ctx->EFLAGS.CF = (dest < src);
        ctx->EFLAGS.OF = (((dest ^ src) & (dest ^ truncated_res) & sign_mask) != 0);
        ctx->EFLAGS.AF = (((dest ^ src ^ truncated_res) & 0x10U) != 0);
    } else if (op == ALU_INC) {
        ctx->EFLAGS.OF = (((dest ^ truncated_res) & (src ^ truncated_res) & sign_mask) != 0);
        ctx->EFLAGS.AF = (((dest ^ src ^ truncated_res) & 0x10U) != 0);
    } else if (op == ALU_DEC) {
        ctx->EFLAGS.OF = (((dest ^ src) & (dest ^ truncated_res) & sign_mask) != 0);
        ctx->EFLAGS.AF = (((dest ^ src ^ truncated_res) & 0x10U) != 0);
    }
}

static int GetOperandBitSize(CPU_Context* ctx, DecodeContext* d_ctx, OperandType type) {
    (void)ctx;
    int def_op_size = (d_ctx->pfx_op_size == 0x66) ? 16 : 32;
    switch (type) {
        case Eb: case Gb:
        case OP_AL: case OP_CL: case OP_DL: case OP_BL:
        case OP_AH: case OP_CH: case OP_DH: case OP_BH:
            return 8;
        case Ew: case Gw:
        case OP_DX:
            return 16;
        case Ev: case Gv: case Gz:
        case OP_rAX: case OP_rCX: case OP_rDX: case OP_rBX:
        case OP_rSP: case OP_rBP: case OP_rSI: case OP_rDI:
            return def_op_size;
        case Ib: return 8;
        case Iw: return 16;
        case Iv: case Iz: return def_op_size;
        default: return def_op_size;
    }
}

static void Exec_ALU_Generic(CPU_Context* ctx, DecodeContext* d_ctx, ALU_Op op, bool is_compare) {
    int size_d = GetOperandBitSize(ctx, d_ctx, d_ctx->entry.op1);
    int size_s = GetOperandBitSize(ctx, d_ctx, d_ctx->entry.op2);
    uint32_t dest = GetOperandValue(ctx, d_ctx, 0);
    uint32_t src = GetOperandValue(ctx, d_ctx, 1);

    uint32_t mask_d = (size_d == 8) ? 0xFFU : (size_d == 16 ? 0xFFFFU : 0xFFFFFFFFU);
    uint32_t mask_s = (size_s == 8) ? 0xFFU : (size_s == 16 ? 0xFFFFU : 0xFFFFFFFFU);
    dest &= mask_d;
    src &= mask_s;
    uint32_t res = 0;

    uint32_t cf_in = ctx->EFLAGS.CF;
    switch (op) {
        case ALU_ADD: res = dest + src; break;
        case ALU_OR:  res = dest | src; break;
        case ALU_ADC: res = dest + src + cf_in; break;
        case ALU_SBB: res = dest - src - cf_in; break;
        case ALU_AND: res = dest & src; break;
        case ALU_CMP:
        case ALU_SUB: res = dest - src; break;
        case ALU_XOR: res = dest ^ src; break;
        case ALU_INC: res = dest + 1; break;
        case ALU_DEC: res = dest - 1; break;
    }
    uint32_t truncated_res = res & mask_d;
    UpdateEFLAGS(ctx, truncated_res, dest, src, size_d, op);
    if (!is_compare) {
        SetOperandValue(ctx, d_ctx, 0, truncated_res);
    }
}

static void Exec_Group1(CPU_Context* ctx, DecodeContext* d_ctx) {
    switch (d_ctx->reg) {
        case 0: Exec_ALU_Generic(ctx, d_ctx, ALU_ADD, false); break;
        case 1: Exec_ALU_Generic(ctx, d_ctx, ALU_OR, false); break;
        case 2: Exec_ALU_Generic(ctx, d_ctx, ALU_ADC, false); break;
        case 3: Exec_ALU_Generic(ctx, d_ctx, ALU_SBB, false); break;
        case 4: Exec_ALU_Generic(ctx, d_ctx, ALU_AND, false); break;
        case 5: Exec_ALU_Generic(ctx, d_ctx, ALU_SUB, false); break;
        case 6: Exec_ALU_Generic(ctx, d_ctx, ALU_XOR, false); break;
        case 7: Exec_ALU_Generic(ctx, d_ctx, ALU_CMP, true); break;
    }
}

static void Exec_PUSH(CPU_Context* ctx, DecodeContext* d_ctx) {
    uint32_t value = GetOperandValue(ctx, d_ctx, 0);
    int size = (d_ctx->pfx_op_size == 0x66) ? 2 : 4;
    ctx->ESP.I32 -= (uint32_t)size;
    MemWrite(ctx->ESP.I32, value, size);
}

static void Exec_POP(CPU_Context* ctx, DecodeContext* d_ctx) {
    int size = (d_ctx->pfx_op_size == 0x66) ? 2 : 4;
    uint32_t val = MemRead(ctx->ESP.I32, size);
    ctx->ESP.I32 += (uint32_t)size;
    SetOperandValue(ctx, d_ctx, 0, val);
}

static void Exec_PUSHA(CPU_Context* ctx, DecodeContext* d_ctx) {
    int size = (d_ctx->pfx_op_size == 0x66) ? 2 : 4;
    uint32_t old_esp = ctx->ESP.I32;

    if (size == 2) {
        for (int i = 0; i < 8; i++) {
            ctx->ESP.I32 -= 2;
            uint16_t v = (i == 4) ? (uint16_t)old_esp : ctx->GPR[i].I16;
            MemWrite(ctx->ESP.I32, v, 2);
        }
    } else {
        for (int i = 0; i < 8; i++) {
            ctx->ESP.I32 -= 4;
            uint32_t v = (i == 4) ? old_esp : ctx->GPR[i].I32;
            MemWrite(ctx->ESP.I32, v, 4);
        }
    }
}

static void Exec_POPA(CPU_Context* ctx, DecodeContext* d_ctx) {
    int size = (d_ctx->pfx_op_size == 0x66) ? 2 : 4;

    if (size == 2) {
        ctx->EDI.I16 = (uint16_t)MemRead(ctx->ESP.I32, 2); ctx->ESP.I32 += 2;
        ctx->ESI.I16 = (uint16_t)MemRead(ctx->ESP.I32, 2); ctx->ESP.I32 += 2;
        ctx->EBP.I16 = (uint16_t)MemRead(ctx->ESP.I32, 2); ctx->ESP.I32 += 2;
        ctx->ESP.I32 += 2; // 丢弃 SP
        ctx->EBX.I16 = (uint16_t)MemRead(ctx->ESP.I32, 2); ctx->ESP.I32 += 2;
        ctx->EDX.I16 = (uint16_t)MemRead(ctx->ESP.I32, 2); ctx->ESP.I32 += 2;
        ctx->ECX.I16 = (uint16_t)MemRead(ctx->ESP.I32, 2); ctx->ESP.I32 += 2;
        ctx->EAX.I16 = (uint16_t)MemRead(ctx->ESP.I32, 2); ctx->ESP.I32 += 2;
    } else {
        ctx->EDI.I32 = MemRead(ctx->ESP.I32, 4); ctx->ESP.I32 += 4;
        ctx->ESI.I32 = MemRead(ctx->ESP.I32, 4); ctx->ESP.I32 += 4;
        ctx->EBP.I32 = MemRead(ctx->ESP.I32, 4); ctx->ESP.I32 += 4;
        ctx->ESP.I32 += 4; // 丢弃 ESP
        ctx->EBX.I32 = MemRead(ctx->ESP.I32, 4); ctx->ESP.I32 += 4;
        ctx->EDX.I32 = MemRead(ctx->ESP.I32, 4); ctx->ESP.I32 += 4;
        ctx->ECX.I32 = MemRead(ctx->ESP.I32, 4); ctx->ESP.I32 += 4;
        ctx->EAX.I32 = MemRead(ctx->ESP.I32, 4); ctx->ESP.I32 += 4;
    }
}

static bool CheckCondition(CPU_Context* ctx, uint8_t condition_code) {
    switch (condition_code & 0xF) {
        case 0x0: return ctx->EFLAGS.OF == 1;
        case 0x1: return ctx->EFLAGS.OF == 0;
        case 0x2: return ctx->EFLAGS.CF == 1;
        case 0x3: return ctx->EFLAGS.CF == 0;
        case 0x4: return ctx->EFLAGS.ZF == 1;
        case 0x5: return ctx->EFLAGS.ZF == 0;
        case 0x6: return (ctx->EFLAGS.CF | ctx->EFLAGS.ZF) == 1;
        case 0x7: return (ctx->EFLAGS.CF | ctx->EFLAGS.ZF) == 0;
        case 0x8: return ctx->EFLAGS.SF == 1;
        case 0x9: return ctx->EFLAGS.SF == 0;
        case 0xA: return ctx->EFLAGS.PF == 1;
        case 0xB: return ctx->EFLAGS.PF == 0;
        case 0xC: return ctx->EFLAGS.SF != ctx->EFLAGS.OF;
        case 0xD: return ctx->EFLAGS.SF == ctx->EFLAGS.OF;
        case 0xE: return ctx->EFLAGS.ZF == 1 || (ctx->EFLAGS.SF != ctx->EFLAGS.OF);
        case 0xF: return ctx->EFLAGS.ZF == 0 && (ctx->EFLAGS.SF == ctx->EFLAGS.OF);
    }
    return false;
}

static bool Exec_Branch(CPU_Context* ctx, DecodeContext* d_ctx, bool is_conditional) {
    bool take_jump = true;
    if (is_conditional) {
        take_jump = CheckCondition(ctx, d_ctx->opcode);
    }
    if (take_jump) {
        uint32_t offset = (uint32_t)d_ctx->imm;
        ctx->EIP += (uint32_t)d_ctx->instr_len + offset;
        return true;
    }
    return false;
}

static bool Exec_CALL(CPU_Context* ctx, DecodeContext* d_ctx) {
    uint32_t ret_addr = ctx->EIP + (uint32_t)d_ctx->instr_len;
    ctx->ESP.I32 -= 4;
    MemWrite(ctx->ESP.I32, ret_addr, 4);

    uint32_t offset = (uint32_t)d_ctx->imm;
    ctx->EIP += (uint32_t)d_ctx->instr_len + offset;
    return true;
}

static bool Exec_RET(CPU_Context* ctx, DecodeContext* d_ctx) {
    uint32_t ret_addr = MemRead(ctx->ESP.I32, 4);
    ctx->ESP.I32 += 4;
    if (d_ctx->opcode == 0xC2 || d_ctx->opcode == 0xCA) {
        ctx->ESP.I32 += (uint32_t)d_ctx->imm;
    }
    ctx->EIP = ret_addr;
    return true;
}

static void Exec_DEC(CPU_Context* ctx, DecodeContext* d_ctx) {
    int reg = d_ctx->opcode - 0x48;
    uint32_t val = ReadGPR(ctx, reg, 32);
    uint32_t res = val - 1;
    UpdateEFLAGS(ctx, res, val, 1, 32, ALU_DEC);
    WriteGPR(ctx, reg, 32, res);
}

static void Exec_INC(CPU_Context* ctx, DecodeContext* d_ctx) {
    int reg = d_ctx->opcode - 0x40;
    uint32_t val = ReadGPR(ctx, reg, 32);
    uint32_t res = val + 1;
    UpdateEFLAGS(ctx, res, val, 1, 32, ALU_INC);
    WriteGPR(ctx, reg, 32, res);
}

static void UpdateLogicFlags(CPU_Context* ctx, uint32_t res, int size) {
    uint32_t truncated_res = res;
    uint32_t sign_mask = 0;

    if (size == 8) { truncated_res &= 0xFFU; sign_mask = 0x80U; }
    else if (size == 16) { truncated_res &= 0xFFFFU; sign_mask = 0x8000U; }
    else { sign_mask = 0x80000000U; }

    ctx->EFLAGS.ZF = (truncated_res == 0) ? 1 : 0;
    ctx->EFLAGS.SF = (truncated_res & sign_mask) ? 1 : 0;
    ctx->EFLAGS.PF = CalcPF((uint8_t)truncated_res);
}

static void Exec_Group2(CPU_Context* ctx, DecodeContext* d_ctx) {
    int size = GetOperandBitSize(ctx, d_ctx, d_ctx->entry.op1);
    uint32_t val = GetOperandValue(ctx, d_ctx, 0);

    uint32_t mask = (size == 32) ? 0xFFFFFFFFU : ((1U << size) - 1U);
    val &= mask;
    uint32_t msb_mask = 1U << (size - 1);

    uint32_t count = 0;
    if (d_ctx->opcode == 0xC0 || d_ctx->opcode == 0xC1) { count = (uint32_t)d_ctx->imm; }
    else if (d_ctx->opcode == 0xD0 || d_ctx->opcode == 0xD1) { count = 1; }
    else if (d_ctx->opcode == 0xD2 || d_ctx->opcode == 0xD3) { count = ReadGPR(ctx, 1, 8); }
    count &= 0x1F;
    if (count == 0) return;

    uint32_t res = val;
    uint32_t original_msb = val & msb_mask;

    switch (d_ctx->reg) {
        case 0: { // ROL
            for (uint32_t i = 0; i < count; i++) {
                uint32_t bit = (res & msb_mask) ? 1 : 0;
                ctx->EFLAGS.CF = bit;
                res <<= 1;
                res |= bit;
            }
            if (count == 1) {
                ctx->EFLAGS.OF = ((res & msb_mask) ? 1 : 0) ^ ctx->EFLAGS.CF;
            }
            break;
        }
        case 1: { // ROR
            for (uint32_t i = 0; i < count; i++) {
                uint32_t bit = res & 1;
                res >>= 1;
                ctx->EFLAGS.CF = bit;
                if (bit) res |= msb_mask;
            }
            if (count == 1) {
                uint32_t msb = (res >> (size - 1)) & 1;
                uint32_t msb_minus_1 = (res >> (size - 2)) & 1;
                ctx->EFLAGS.OF = msb ^ msb_minus_1;
            }
            break;
        }
        case 2: { // RCL
            for (uint32_t i = 0; i < count; i++) {
                uint32_t bit = (res & msb_mask) ? 1 : 0;
                res <<= 1;
                if (ctx->EFLAGS.CF) res |= 1;
                ctx->EFLAGS.CF = bit;
            }
            if (count == 1) {
                ctx->EFLAGS.OF = ((res & msb_mask) ? 1 : 0) ^ ctx->EFLAGS.CF;
            }
            break;
        }
        case 3: { // RCR
            for (uint32_t i = 0; i < count; i++) {
                uint32_t bit = res & 1;
                res >>= 1;
                if (ctx->EFLAGS.CF) res |= msb_mask;
                ctx->EFLAGS.CF = bit;
            }
            if (count == 1) {
                uint32_t msb = (res >> (size - 1)) & 1;
                uint32_t msb_minus_1 = (res >> (size - 2)) & 1;
                ctx->EFLAGS.OF = msb ^ msb_minus_1;
            }
            break;
        }
        case 4:
        case 6: { // SHL / SAL
            for (uint32_t i = 0; i < count; i++) {
                ctx->EFLAGS.CF = (res & msb_mask) ? 1 : 0;
                res <<= 1;
            }
            if (count == 1) {
                ctx->EFLAGS.OF = ctx->EFLAGS.CF ^ ((res & msb_mask) ? 1 : 0);
            }
            break;
        }
        case 5: { // SHR
            for (uint32_t i = 0; i < count; i++) {
                ctx->EFLAGS.CF = res & 1;
                res >>= 1;
            }
            if (count == 1) {
                ctx->EFLAGS.OF = (original_msb) ? 1 : 0;
            }
            break;
        }
        case 7: { // SAR
            for (uint32_t i = 0; i < count; i++) {
                ctx->EFLAGS.CF = res & 1;
                res >>= 1;
                if (original_msb) res |= msb_mask;
            }
            if (count == 1) {
                ctx->EFLAGS.OF = 0;
            }
            break;
        }
        default:
            printf("Unimplemented Group 2(S/R) op: %d\n", d_ctx->reg);
            return;
    }

    res &= mask;
    UpdateLogicFlags(ctx, res, size);
    SetOperandValue(ctx, d_ctx, 0, res);
}

static uint32_t GetGroup3Source(CPU_Context* ctx, DecodeContext* d_ctx) {
    if (d_ctx->reg >= 4) {
        return GetOperandValue(ctx, d_ctx, 1);
    }
    return GetOperandValue(ctx, d_ctx, 0);
}

static void SetGroup3Dest(CPU_Context* ctx, DecodeContext* d_ctx, uint32_t val) {
    SetOperandValue(ctx, d_ctx, 0, val);
}

static void Exec_Group3(CPU_Context* ctx, DecodeContext* d_ctx) {
    int size = GetOperandBitSize(ctx, d_ctx, d_ctx->entry.op1);
    uint32_t src = GetGroup3Source(ctx, d_ctx);
    if (size == 8) src &= 0xFFU;
    else if (size == 16) src &= 0xFFFFU;

    switch (d_ctx->reg) {
        case 0:
        case 1:
            Exec_ALU_Generic(ctx, d_ctx, ALU_AND, true);
            break;
        case 2: // NOT
            SetGroup3Dest(ctx, d_ctx, ~src);
            break;
        case 3: // NEG
            SetGroup3Dest(ctx, d_ctx, (0U - src));
            UpdateEFLAGS(ctx, (0U - src), 0, src, size, ALU_SUB);
            break;
        case 4: { // MUL
            if (size == 8) {
                uint32_t al = ctx->EAX.I8.L;
                uint32_t res = al * src;
                ctx->EAX.I16 = (uint16_t)res;
                ctx->EFLAGS.CF = ctx->EFLAGS.OF = (res & 0xFF00U) ? 1 : 0;
            } else if (size == 16) {
                uint32_t ax = ctx->EAX.I16;
                uint32_t res = ax * src;
                ctx->EDX.I16 = (uint16_t)(res >> 16);
                ctx->EAX.I16 = (uint16_t)(res & 0xFFFFU);
                ctx->EFLAGS.CF = ctx->EFLAGS.OF = (ctx->EDX.I16 != 0);
            } else {
                uint64_t eax = ctx->EAX.I32;
                uint64_t res = eax * (uint64_t)src;
                ctx->EDX.I32 = (uint32_t)(res >> 32);
                ctx->EAX.I32 = (uint32_t)(res & 0xFFFFFFFFULL);
                ctx->EFLAGS.CF = ctx->EFLAGS.OF = (ctx->EDX.I32 != 0);
            }
            break;
        }
        case 5: { // IMUL
            if (size == 32) {
                int64_t eax = (int32_t)ctx->EAX.I32;
                int64_t val = (int32_t)src;
                int64_t res = eax * val;
                ctx->EDX.I32 = (uint32_t)((uint64_t)res >> 32);
                ctx->EAX.I32 = (uint32_t)((uint64_t)res & 0xFFFFFFFFULL);
                bool overflow = (res != (int64_t)(int32_t)res);
                ctx->EFLAGS.CF = ctx->EFLAGS.OF = overflow;
            } else if (size == 16) {
                int32_t ax = (int16_t)ctx->EAX.I16;
                int32_t val = (int16_t)src;
                int32_t res = ax * val;
                ctx->EDX.I16 = (uint16_t)((uint32_t)res >> 16);
                ctx->EAX.I16 = (uint16_t)((uint32_t)res & 0xFFFFU);
                bool overflow = (res != (int32_t)(int16_t)res);
                ctx->EFLAGS.CF = ctx->EFLAGS.OF = overflow;
            } else {
                int16_t al = (int8_t)ctx->EAX.I8.L;
                int16_t val = (int8_t)src;
                int16_t res = (int16_t)(al * val);
                ctx->EAX.I16 = (uint16_t)res;
                bool overflow = (res != (int16_t)(int8_t)res);
                ctx->EFLAGS.CF = ctx->EFLAGS.OF = overflow;
            }
            break;
        }
        case 6: { // DIV
            if (src == 0) {
                printf("[CPU Error] Division by Zero at EIP=0x%08X\n", ctx->EIP);
                return;
            }
            if (size == 32) {
                uint64_t dividend = ((uint64_t)ctx->EDX.I32 << 32) | ctx->EAX.I32;
                uint64_t quo = dividend / src;
                uint64_t rem = dividend % src;
                if (quo > 0xFFFFFFFFULL) {
                    printf("[CPU Error] quotient overflow at EIP=0x%08X\n", ctx->EIP);
                    return;
                }
                ctx->EAX.I32 = (uint32_t)quo;
                ctx->EDX.I32 = (uint32_t)rem;
            } else if (size == 16) {
                uint32_t dividend = ((uint32_t)ctx->EDX.I16 << 16) | ctx->EAX.I16;
                uint32_t quo = dividend / src;
                uint32_t rem = dividend % src;
                if (quo > 0xFFFFU) return;
                ctx->EAX.I16 = (uint16_t)quo;
                ctx->EDX.I16 = (uint16_t)rem;
            } else {
                uint16_t dividend = ctx->EAX.I16;
                uint16_t quo = (uint16_t)(dividend / src);
                uint16_t rem = (uint16_t)(dividend % src);
                if (quo > 0xFFU) return;
                ctx->EAX.I8.L = (uint8_t)quo;
                ctx->EAX.I8.H = (uint8_t)rem;
            }
            break;
        }
        case 7: { // IDIV
            if (src == 0) {
                printf("[CPU Error] Division by Zero at EIP=0x%08X\n", ctx->EIP);
                return;
            }
            if (size == 8) {
                int16_t dividend = (int16_t)ctx->EAX.I16;
                int8_t divisor = (int8_t)src;
                if (dividend == -128 && divisor == -1) goto overflow_label;
                int16_t quo = (int16_t)(dividend / divisor);
                int16_t rem = (int16_t)(dividend % divisor);
                if (quo > 127 || quo < -128) goto overflow_label;
                ctx->EAX.I8.L = (uint8_t)(int8_t)quo;
                ctx->EAX.I8.H = (uint8_t)(int8_t)rem;
            } else if (size == 16) {
                int32_t dividend = (int32_t)(((uint32_t)ctx->EDX.I16 << 16) | (uint16_t)ctx->EAX.I16);
                int16_t divisor = (int16_t)src;
                if (dividend == (int32_t)0x80000000U && divisor == -1) goto overflow_label;
                int32_t quo = dividend / divisor;
                int32_t rem = dividend % divisor;
                if (quo > 32767 || quo < -32768) goto overflow_label;
                ctx->EAX.I16 = (uint16_t)(int16_t)quo;
                ctx->EDX.I16 = (uint16_t)(int16_t)rem;
            } else if (size == 32) {
                int64_t dividend = (int64_t)(((uint64_t)ctx->EDX.I32 << 32) | (uint32_t)ctx->EAX.I32);
                int32_t divisor = (int32_t)src;
                if ((uint64_t)dividend == 0x8000000000000000ULL && divisor == -1) goto overflow_label;
                int64_t quo = dividend / divisor;
                int64_t rem = dividend % divisor;
                if (quo > 2147483647LL || quo < -2147483648LL) goto overflow_label;
                ctx->EAX.I32 = (uint32_t)quo;
                ctx->EDX.I32 = (uint32_t)rem;
            }
            break;

        overflow_label:
            printf("[CPU Error] IDIV Quotient Overflow at EIP=0x%08X\n", ctx->EIP);
            return;
        }
        default:
            printf("Group 3 error opcode: %d\n", d_ctx->reg);
            break;
    }
}

uint32_t MemRead(uint32_t addr, int uint8_t_len) {
    uint8_t* ptr = TinyX86_TranslateAddress(addr);
    if (uint8_t_len == 1) {
        return *ptr;
    } else if (uint8_t_len == 2) {
        uint16_t v = 0;
        memcpy(&v, ptr, 2);
        return v;
    } else {
        uint32_t v = 0;
        memcpy(&v, ptr, 4);
        return v;
    }
}

void MemWrite(uint32_t addr, uint32_t val, int uint8_t_len) {
    uint8_t* ptr = TinyX86_TranslateAddress(addr);
    if (uint8_t_len == 1) {
        *ptr = (uint8_t)val;
    } else if (uint8_t_len == 2) {
        uint16_t v = (uint16_t)val;
        memcpy(ptr, &v, 2);
    } else {
        memcpy(ptr, &val, 4);
    }
}

static void Exec_StringOp(CPU_Context* ctx, DecodeContext* d_ctx) {
    int bit_sz = (d_ctx->opcode & 1) ? ((d_ctx->pfx_op_size == 0x66) ? 16 : 32) : 8;
    int uint8_t_len = bit_sz / 8;
    int step = (ctx->EFLAGS.DF == 0) ? uint8_t_len : -uint8_t_len;

    uint32_t esi = ctx->GPR[6].I32;
    uint32_t edi = ctx->GPR[7].I32;

    switch (d_ctx->opcode & 0xFE) {
        case 0xA4: { // MOVS
            MemWrite(edi, MemRead(esi, uint8_t_len), uint8_t_len);
            ctx->ESI.I32 += (uint32_t)step;
            ctx->EDI.I32 += (uint32_t)step;
            break;
        }
        case 0xA6: { // CMPS
            uint32_t val_src = MemRead(esi, uint8_t_len);
            uint32_t val_dest = MemRead(edi, uint8_t_len);
            uint32_t res = val_dest - val_src;
            UpdateEFLAGS(ctx, res, val_dest, val_src, bit_sz, ALU_SUB);
            ctx->EDI.I32 += (uint32_t)step;
            ctx->ESI.I32 += (uint32_t)step;
            break;
        }
        case 0xAA: { // STOS
            uint32_t val = 0;
            if (bit_sz == 8) val = ctx->EAX.I8.L;
            else if (bit_sz == 16) val = ctx->EAX.I16;
            else if (bit_sz == 32) val = ctx->EAX.I32;
            MemWrite(edi, val, uint8_t_len);
            ctx->EDI.I32 += (uint32_t)step;
            break;
        }
        case 0xAC: { // LODS
            uint32_t val = MemRead(esi, uint8_t_len);
            if (bit_sz == 8) ctx->EAX.I8.L = (uint8_t)val;
            else if (bit_sz == 16) ctx->EAX.I16 = (uint16_t)val;
            else if (bit_sz == 32) ctx->EAX.I32 = val;
            ctx->ESI.I32 += (uint32_t)step;
            break;
        }
        case 0xAE: { // SCAS
            uint32_t val_dest = MemRead(edi, uint8_t_len);
            uint32_t val_src = 0;
            if (bit_sz == 8) val_src = ctx->EAX.I8.L;
            else if (bit_sz == 16) val_src = ctx->EAX.I16;
            else if (bit_sz == 32) val_src = ctx->EAX.I32;
            uint32_t res = val_dest - val_src;
            UpdateEFLAGS(ctx, res, val_dest, val_src, bit_sz, ALU_SUB);
            ctx->EDI.I32 += (uint32_t)step;
            break;
        }
    }
}

static bool Exec_REP_StringOp(CPU_Context* ctx, DecodeContext* d_ctx) {
    if (ctx->ECX.I32 == 0) return false;
    Exec_StringOp(ctx, d_ctx);
    ctx->ECX.I32--;
    bool should_continue = (ctx->ECX.I32 > 0);
    bool is_compare_op = (d_ctx->opcode & 0xA6) == 0xA6;
    if (is_compare_op && should_continue) {
        if (d_ctx->pfx_rep) {
            should_continue = (ctx->EFLAGS.ZF == 1);
        } else if (d_ctx->pfx_repne) {
            should_continue = (ctx->EFLAGS.ZF == 0);
        }
    }
    return should_continue;
}

static void Exec_LEA(CPU_Context* ctx, DecodeContext* d_ctx) {
    uint32_t addr = GetEffectiveAddress(ctx, d_ctx);
    SetOperandValue(ctx, d_ctx, 0, addr);
}

static void Exec_XCHG(CPU_Context* ctx, DecodeContext* d_ctx) {
    uint32_t op1 = GetOperandValue(ctx, d_ctx, 0);
    uint32_t op2 = GetOperandValue(ctx, d_ctx, 1);
    SetOperandValue(ctx, d_ctx, 1, op1);
    SetOperandValue(ctx, d_ctx, 0, op2);
}

static uint32_t PackEFLAGS(CPU_Context* ctx) {
    uint32_t raw = ctx->EFLAGS.Value;
    raw |= 0x00000002U;
    raw &= ~0x00008028U;
    return raw;
}

static void UnpackEFLAGS(CPU_Context* ctx, uint32_t val) {
    ctx->EFLAGS.Value = val;
    ctx->EFLAGS.Value |= 0x00000002U;
}

static void Exec_PUSHF(CPU_Context* ctx) {
    uint32_t eflags = PackEFLAGS(ctx);
    ctx->ESP.I32 -= 4;
    MemWrite(ctx->ESP.I32, eflags, 4);
}

static void Exec_POPF(CPU_Context* ctx) {
    uint32_t eflags = MemRead(ctx->ESP.I32, 4);
    ctx->ESP.I32 += 4;
    UnpackEFLAGS(ctx, eflags);
}

static void Exec_LEAVE(CPU_Context* ctx) {
    ctx->ESP.I32 = ctx->EBP.I32;
    ctx->EBP.I32 = MemRead(ctx->ESP.I32, 4);
    ctx->ESP.I32 += 4;
}

static void Exec_ENTER(CPU_Context* ctx, DecodeContext* d_ctx) {
    uint16_t size = (uint16_t)d_ctx->imm;
    uint8_t level = (uint8_t)(d_ctx->imm2 & 0x1F);
    ctx->ESP.I32 -= 4;
    MemWrite(ctx->ESP.I32, ctx->EBP.I32, 4);
    uint32_t fp = ctx->ESP.I32;
    if (level > 0) {
        uint32_t temp_fp = ctx->EBP.I32;
        for (int i = 1; i < level; i++) {
            temp_fp -= 4;
            ctx->ESP.I32 -= 4;
            uint32_t parent_fp = MemRead(temp_fp, 4);
            MemWrite(ctx->ESP.I32, parent_fp, 4);
        }
        ctx->ESP.I32 -= 4;
        MemWrite(ctx->ESP.I32, fp, 4);
    }
    ctx->EBP.I32 = fp;
    ctx->ESP.I32 -= size;
}

static void Exec_Group4(CPU_Context* ctx, DecodeContext* d_ctx) {
    switch (d_ctx->reg) {
        case 0: Exec_ALU_Generic(ctx, d_ctx, ALU_INC, false); break;
        case 1: Exec_ALU_Generic(ctx, d_ctx, ALU_DEC, false); break;
        default:
            printf("[Error] Group 4 unknown reg: %d (Only 0/1 defined)\n", d_ctx->reg);
            break;
    }
}

static bool Exec_Group5(CPU_Context* ctx, DecodeContext* d_ctx) {
    uint32_t val = GetOperandValue(ctx, d_ctx, 0);
    bool jumped = false;
    switch (d_ctx->reg) {
        case 0:
            Exec_ALU_Generic(ctx, d_ctx, ALU_INC, false); break;
        case 1:
            Exec_ALU_Generic(ctx, d_ctx, ALU_DEC, false); break;
        case 2: {
            uint32_t retaddr = ctx->EIP + (uint32_t)d_ctx->instr_len;
            ctx->ESP.I32 -= 4;
            MemWrite(ctx->ESP.I32, retaddr, 4);
            ctx->EIP = val;
            jumped = true;
            break;
        }
        case 3:
            printf("[Warning] CALL FAR not supported yet.\n");
            break;
        case 4: {
            ctx->EIP = val;
            jumped = true;
            break;
        }
        case 5:
            printf("[Warning] JMP FAR not supported yet.\n");
            break;
        case 6:
            ctx->ESP.I32 -= 4;
            MemWrite(ctx->ESP.I32, val, 4);
            break;
        default:
            printf("[Error] Group 5 unknown reg: %d\n", d_ctx->reg);
            break;
    }
    return jumped;
}

static void Exec_MOVZX(CPU_Context* ctx, DecodeContext* d_ctx, int src_bits) {
    uint32_t val = GetOperandValue(ctx, d_ctx, 1);
    if (src_bits == 8) val &= 0xFFU;
    else if (src_bits == 16) val &= 0xFFFFU;
    SetOperandValue(ctx, d_ctx, 0, val);
}

static void Exec_MOVSX(CPU_Context* ctx, DecodeContext* d_ctx, int src_bits) {
    uint32_t val = GetOperandValue(ctx, d_ctx, 1);
    int32_t res = 0;
    if (src_bits == 8) res = (int32_t)(int8_t)val;
    else if (src_bits == 16) res = (int32_t)(int16_t)val;
    SetOperandValue(ctx, d_ctx, 0, (uint32_t)res);
}

static void Exec_SETcc(CPU_Context* ctx, DecodeContext* d_ctx) {
    bool match = CheckCondition(ctx, d_ctx->opcode);
    SetOperandValue(ctx, d_ctx, 0, match ? 1 : 0);
}

static void Exec_CMOVcc(CPU_Context* ctx, DecodeContext* d_ctx) {
    bool match = CheckCondition(ctx, d_ctx->opcode);
    if (match) {
        uint32_t val = GetOperandValue(ctx, d_ctx, 1);
        SetOperandValue(ctx, d_ctx, 0, val);
    }
}

static void Exec_IMUL_2_Op(CPU_Context* ctx, DecodeContext* d_ctx) {
    int size = (d_ctx->pfx_op_size == 0x66) ? 16 : 32;
    uint32_t val_dest = GetOperandValue(ctx, d_ctx, 0);
    uint32_t val_src = GetOperandValue(ctx, d_ctx, 1);

    if (size == 16) {
        int16_t d = (int16_t)val_dest;
        int16_t s = (int16_t)val_src;
        int32_t res = (int32_t)d * (int32_t)s;
        SetOperandValue(ctx, d_ctx, 0, (uint16_t)res);
        ctx->EFLAGS.CF = ctx->EFLAGS.OF = (res != (int32_t)(int16_t)res) ? 1 : 0;
    } else {
        int32_t d = (int32_t)val_dest;
        int32_t s = (int32_t)val_src;
        int64_t res = (int64_t)d * (int64_t)s;
        SetOperandValue(ctx, d_ctx, 0, (uint32_t)res);
        ctx->EFLAGS.CF = ctx->EFLAGS.OF = (res != (int64_t)(int32_t)res) ? 1 : 0;
    }
}

static void Exec_SignExtend(CPU_Context* ctx, DecodeContext* d_ctx) {
    int op_size = (d_ctx->pfx_op_size == 0x66) ? 16 : 32;
    if (d_ctx->opcode == 0x98) {
        if (op_size == 16) {
            ctx->EAX.I16 = (uint16_t)(int16_t)(int8_t)ctx->EAX.I8.L;
        } else {
            ctx->EAX.I32 = (uint32_t)(int32_t)(int16_t)ctx->EAX.I16;
        }
    } else if (d_ctx->opcode == 0x99) {
        if (op_size == 16) {
            ctx->EDX.I16 = ((int16_t)ctx->EAX.I16 < 0) ? 0xFFFFU : 0x0000U;
        } else {
            ctx->EDX.I32 = ((int32_t)ctx->EAX.I32 < 0) ? 0xFFFFFFFFU : 0x00000000U;
        }
    }
}

static void Exec_INT(CPU_Context* ctx, DecodeContext* d_ctx) {
    uint8_t int_num = (d_ctx->opcode == 0xCC) ? 3 : (uint8_t)d_ctx->imm;

    if (int_num == 0x21) {
        uint8_t ah = ctx->EAX.I8.H;
        switch (ah) {
            case 0x02: {
                putchar(ctx->EDX.I8.L);
                fflush(stdout);
                break;
            }
            case 0x09: {
                uint32_t addr = ctx->EDX.I32;
                char ch = (char)MemRead(addr, 1);
                while (ch != '$' && ch != 0) {
                    putchar(ch);
                    addr++;
                    ch = (char)MemRead(addr, 1);
                }
                break;
            }
            case 0x4C: {
                uint8_t exit_code = ctx->EAX.I8.L;
                printf("\n[Program exited with code %d]\n", exit_code);
                ctx->Halted = true;
                break;
            }
            default:
                printf("[TinyX86] Unhandled INT 21h service: AH=0x%02X\n", ah);
                break;
        }
    } else if (int_num == 3) {
        printf("\n[TinyX86] Breakpoint Hit (INT 3) at EIP=0x%08X\n", ctx->EIP);
        ctx->Halted = true;
    } else {
        printf("[TinyX86] Unhandled Interrupt: 0x%02X\n", int_num);
    }
}

static bool Exec_LOOP(CPU_Context* ctx, DecodeContext* d_ctx) {
    bool condition = false;
    uint32_t counter;
    if (d_ctx->pfx_addr_size == 0x67) {
        counter = --(ctx->ECX.I16);
    } else {
        counter = --(ctx->ECX.I32);
    }
    bool counter_not_zero = (counter != 0);
    if (d_ctx->opcode == 0xE2) {
        condition = counter_not_zero;
    } else if (d_ctx->opcode == 0xE1) {
        condition = counter_not_zero && (ctx->EFLAGS.ZF == 1);
    } else if (d_ctx->opcode == 0xE0) {
        condition = counter_not_zero && (ctx->EFLAGS.ZF == 0);
    }
    if (condition) {
        ctx->EIP += (uint32_t)d_ctx->disp_len + (uint32_t)(int32_t)(int8_t)d_ctx->imm;
        return true;
    }
    return false;
}

static void FPU_PUSH(CPU_Context* ctx, double val) {
    ctx->FPU.SW.TOP = (ctx->FPU.SW.TOP - 1) & 0x7;
    ST(0) = val;
}

static double FPU_POP(CPU_Context* ctx) {
    double val = ST(0);
    ctx->FPU.SW.TOP = (ctx->FPU.SW.TOP + 1) & 0x7;
    return val;
}

static double MemReadF(uint32_t addr, int size) {
    if (size == 4) {
        uint32_t raw = MemRead(addr, 4);
        float f = 0.0f;
        memcpy(&f, &raw, 4);
        return (double)f;
    } else {
        uint32_t low = MemRead(addr, 4);
        uint32_t high = MemRead(addr + 4, 4);
        uint64_t raw = ((uint64_t)high << 32) | low;
        double d = 0.0;
        memcpy(&d, &raw, 8);
        return d;
    }
}

static void MemWriteF(uint32_t addr, double val, int size) {
    if (size == 4) {
        float f = (float)val;
        uint32_t raw = 0;
        memcpy(&raw, &f, 4);
        MemWrite(addr, raw, 4);
    } else {
        uint64_t raw = 0;
        memcpy(&raw, &val, 8);
        MemWrite(addr, (uint32_t)(raw & 0xFFFFFFFFULL), 4);
        MemWrite(addr + 4, (uint32_t)(raw >> 32), 4);
    }
}

static void UpdateFPUFlags(CPU_Context* ctx, double v1, double v2) {
    ctx->FPU.SW.C0 = 0;
    ctx->FPU.SW.C2 = 0;
    ctx->FPU.SW.C3 = 0;

    if (isnan(v1) || isnan(v2)) {
        ctx->FPU.SW.C0 = 1;
        ctx->FPU.SW.C2 = 1;
        ctx->FPU.SW.C3 = 1;
    } else if (v1 > v2) {
        // all 0
    } else if (v1 == v2) {
        ctx->FPU.SW.C3 = 1;
    } else if (v1 < v2) {
        ctx->FPU.SW.C0 = 1;
    }
}

static void Exec_SAHF(CPU_Context* ctx) {
    uint8_t ah = ctx->EAX.I8.H;
    ctx->EFLAGS.SF = (ah >> 7) & 1;
    ctx->EFLAGS.ZF = (ah >> 6) & 1;
    ctx->EFLAGS.AF = (ah >> 4) & 1;
    ctx->EFLAGS.PF = (ah >> 2) & 1;
    ctx->EFLAGS.CF = (ah >> 0) & 1;
}

static void Exec_LAHF(CPU_Context* ctx) {
    uint8_t ah = 0x02;
    ah |= (uint8_t)((ctx->EFLAGS.SF & 1) << 7);
    ah |= (uint8_t)((ctx->EFLAGS.ZF & 1) << 6);
    ah |= (uint8_t)((ctx->EFLAGS.AF & 1) << 4);
    ah |= (uint8_t)((ctx->EFLAGS.PF & 1) << 2);
    ah |= (uint8_t)((ctx->EFLAGS.CF & 1) << 0);
    ctx->EAX.I8.H = ah;
}

static double MemReadI(uint32_t addr, int size) {
    uint32_t raw = MemRead(addr, size);
    if (size == 2) {
        return (double)(int16_t)raw;
    } else {
        return (double)(int32_t)raw;
    }
}

static void MemWriteI(uint32_t addr, double val, int size) {
    if (size == 2) {
        int16_t i16 = (int16_t)val;
        MemWrite(addr, (uint16_t)i16, 2);
    } else {
        int32_t i32 = (int32_t)val;
        MemWrite(addr, (uint32_t)i32, 4);
    }
}

static void Exec_FPU(CPU_Context* ctx, DecodeContext* d_ctx) {
    uint8_t op_group = d_ctx->opcode;
    uint8_t sub_op = d_ctx->reg;
    bool is_mem = d_ctx->mod != 3;

    if (is_mem) {
        uint32_t addr = GetEffectiveAddress(ctx, d_ctx);
        if (op_group == 0xD8 || op_group == 0xDA || op_group == 0xDC || op_group == 0xDE) {
            double src = 0.0;
            if (op_group == 0xD8) src = MemReadF(addr, 4);
            else if (op_group == 0xDC) src = MemReadF(addr, 8);
            else if (op_group == 0xDA) src = MemReadI(addr, 4);
            else if (op_group == 0xDE) src = MemReadI(addr, 2);

            double* dst = &ST(0);
            switch (sub_op) {
                case 0: *dst += src; break;
                case 1: *dst *= src; break;
                case 2: UpdateFPUFlags(ctx, *dst, src); break;
                case 3: UpdateFPUFlags(ctx, *dst, src); FPU_POP(ctx); break;
                case 4: *dst -= src; break;
                case 5: *dst = src - *dst; break;
                case 6: *dst /= src; break;
                case 7: *dst = src / *dst; break;
            }
        } else {
            int size = 0;
            bool is_int = false;
            if (op_group == 0xD9) { size = 4; }
            else if (op_group == 0xDD) { size = 8; }
            else if (op_group == 0xDB) { size = 4; is_int = true; }
            else if (op_group == 0xDF) { size = 2; is_int = true; }

            switch (sub_op) {
                case 0:
                    if (is_int) FPU_PUSH(ctx, MemReadI(addr, size));
                    else        FPU_PUSH(ctx, MemReadF(addr, size));
                    break;
                case 2:
                    if (is_int) MemWriteI(addr, ST(0), size);
                    else        MemWriteF(addr, ST(0), size);
                    break;
                case 3:
                    if (is_int) MemWriteI(addr, FPU_POP(ctx), size);
                    else        MemWriteF(addr, FPU_POP(ctx), size);
                    break;
                case 5:
                    if (op_group == 0xDF) {
                        uint32_t low = MemRead(addr, 4);
                        uint32_t high = MemRead(addr + 4, 4);
                        int64_t val64 = ((int64_t)high << 32) | low;
                        FPU_PUSH(ctx, (double)val64);
                    } else if (op_group == 0xD9) {
                        ctx->FPU.CW.Value = (uint16_t)MemRead(addr, 2);
                    }
                    break;
                case 7:
                    if (op_group == 0xDF) {
                        int64_t val64 = (int64_t)FPU_POP(ctx);
                        MemWrite(addr, (uint32_t)((uint64_t)val64 & 0xFFFFFFFFULL), 4);
                        MemWrite(addr + 4, (uint32_t)((uint64_t)val64 >> 32), 4);
                    } else if (op_group == 0xD9) {
                        MemWrite(addr, ctx->FPU.CW.Value, 2);
                    } else if (op_group == 0xDD) {
                        MemWrite(addr, ctx->FPU.SW.Value, 2);
                    }
                    break;
                default:
                    printf("[FPU] Unimpl Mem Op: %02X /%d\n", op_group, sub_op);
                    return;
            }
        }
    } else {
        int i = d_ctx->rm;
        if (op_group == 0xD8) {
            switch (sub_op) {
                case 0: ST(0) += ST(i); break;
                case 1: ST(0) *= ST(i); break;
                case 2: UpdateFPUFlags(ctx, ST(0), ST(i)); break;
                case 3: UpdateFPUFlags(ctx, ST(0), ST(i)); FPU_POP(ctx); break;
                case 4: ST(0) -= ST(i); break;
                case 5: ST(0) = ST(i) - ST(0); break;
                case 6: ST(0) /= ST(i); break;
                case 7: ST(0) = ST(i) / ST(0); break;
            }
        } else if (op_group == 0xDC) {
            switch (sub_op) {
                case 0: ST(i) += ST(0); break;
                case 1: ST(i) *= ST(0); break;
                case 4: ST(i) = ST(0) - ST(i); break;
                case 5: ST(i) = ST(i) - ST(0); break;
                case 6: ST(i) = ST(0) / ST(i); break;
                case 7: ST(i) = ST(i) / ST(0); break;
            }
        } else if (op_group == 0xDE) {
            switch (sub_op) {
                case 0: ST(i) += ST(0); FPU_POP(ctx); break;
                case 1: ST(i) *= ST(0); FPU_POP(ctx); break;
                case 3:
                    if (i == 1) {
                        UpdateFPUFlags(ctx, ST(0), ST(1));
                        FPU_POP(ctx); FPU_POP(ctx);
                    }
                    break;
                case 4: ST(i) = ST(0) - ST(i); FPU_POP(ctx); break;
                case 5: ST(i) = ST(i) - ST(0); FPU_POP(ctx); break;
                case 6: ST(i) = ST(0) / ST(i); FPU_POP(ctx); break;
                case 7: ST(i) = ST(i) / ST(0); FPU_POP(ctx); break;
            }
        } else if (op_group == 0xDA) {
            if (sub_op == 5 && i == 1) {
                UpdateFPUFlags(ctx, ST(0), ST(1));
                FPU_POP(ctx); FPU_POP(ctx);
            }
        } else if (op_group == 0xDB) {
            if (sub_op == 4) {
                if (i == 2) {
                    ctx->FPU.SW.Value &= 0x7F00;
                } else if (i == 3) {
                    ctx->FPU.SW.TOP = 0;
                    ctx->FPU.SW.Value = 0;
                    ctx->FPU.CW.Value = 0x037F;
                }
            }
        } else if (op_group == 0xDF && sub_op == 4 && i == 0) {
            ctx->EAX.I16 = ctx->FPU.SW.Value;
        } else if (op_group == 0xD9) {
            if (sub_op == 4) {
                switch (i) {
                    case 0: ST(0) = -ST(0); break;
                    case 1: ST(0) = fabs(ST(0)); break;
                }
            } else if (sub_op == 5) {
                switch (i) {
                    case 0: FPU_PUSH(ctx, 1.0); break;
                    case 1: FPU_PUSH(ctx, log2(10.0)); break;
                    case 2: FPU_PUSH(ctx, log2(2.718281828)); break;
                    case 3: FPU_PUSH(ctx, 3.141592653589793); break;
                    case 4: FPU_PUSH(ctx, log10(2.0)); break;
                    case 5: FPU_PUSH(ctx, log(2.0)); break;
                    case 6: FPU_PUSH(ctx, 0.0); break;
                }
            }
        }
    }
}
