#include "disasm.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static const char* kGPR8[8]  = { "al", "cl", "dl", "bl", "ah", "ch", "dh", "bh" };
static const char* kGPR16[8] = { "ax", "cx", "dx", "bx", "sp", "bp", "si", "di" };
static const char* kGPR32[8] = { "eax", "ecx", "edx", "ebx", "esp", "ebp", "esi", "edi" };
static const char* kSeg[6]   = { "es", "cs", "ss", "ds", "fs", "gs" };
static const char* kCC[16]   = {
    "o", "no", "b", "nb", "z", "nz", "be", "a",
    "s", "ns", "p", "np", "l", "ge", "le", "g"
};
static const char* kGrp1[8]  = { "add", "or", "adc", "sbb", "and", "sub", "xor", "cmp" };
static const char* kGrp2[8]  = { "rol", "ror", "rcl", "rcr", "shl", "shr", "sal", "sar" };
static const char* kGrp3[8]  = { "test", "test", "not", "neg", "mul", "imul", "div", "idiv" };

static OpcodeEntry make_entry(const char* m, OperandType o1, OperandType o2, OperandType o3, bool modrm) {
    OpcodeEntry e;
    e.mnemonic = m;
    e.op1 = o1;
    e.op2 = o2;
    e.op3 = o3;
    e.has_modrm = modrm;
    return e;
}

static OpcodeEntry lookup_two_byte(uint8_t op) {
    static const char* kCmov[16] = {
        "cmovo","cmovno","cmovb","cmovnb","cmovz","cmovnz","cmovbe","cmova",
        "cmovs","cmovns","cmovp","cmovnp","cmovl","cmovge","cmovle","cmovg"
    };
    static const char* kJcc[16] = {
        "jo","jno","jb","jnb","jz","jnz","jbe","ja",
        "js","jns","jp","jnp","jl","jge","jle","jg"
    };
    static const char* kSetcc[16] = {
        "seto","setno","setb","setnb","setz","setnz","setbe","seta",
        "sets","setns","setp","setnp","setl","setge","setle","setg"
    };
    if (op >= 0x40 && op <= 0x4F) return make_entry(kCmov[op & 0xF], Gv, Ev, OP_NONE, true);
    if (op >= 0x80 && op <= 0x8F) return make_entry(kJcc[op & 0xF], Jz, OP_NONE, OP_NONE, false);
    if (op >= 0x90 && op <= 0x9F) return make_entry(kSetcc[op & 0xF], Eb, OP_NONE, OP_NONE, true);
    switch (op) {
        case 0xA0: return make_entry("push", OP_FS, OP_NONE, OP_NONE, false);
        case 0xA1: return make_entry("pop", OP_FS, OP_NONE, OP_NONE, false);
        case 0xA8: return make_entry("push", OP_GS, OP_NONE, OP_NONE, false);
        case 0xA9: return make_entry("pop", OP_GS, OP_NONE, OP_NONE, false);
        case 0xAF: return make_entry("imul", Gv, Ev, OP_NONE, true);
        case 0xB6: return make_entry("movzx", Gv, Eb, OP_NONE, true);
        case 0xB7: return make_entry("movzx", Gv, Ew, OP_NONE, true);
        case 0xBE: return make_entry("movsx", Gv, Eb, OP_NONE, true);
        case 0xBF: return make_entry("movsx", Gv, Ew, OP_NONE, true);
        default:   return make_entry("db", OP_NONE, OP_NONE, OP_NONE, false);
    }
}

static OpcodeEntry lookup_one_byte(uint8_t op, uint8_t reg, uint8_t mod, uint8_t pfx_op_size) {
    static const char* kJcc[16] = {
        "jo","jno","jb","jnb","jz","jnz","jbe","ja",
        "js","jns","jp","jnp","jl","jge","jle","jg"
    };
    if (op <= 0x3D && (op & 7) <= 5) {
        const char* m = kGrp1[(op >> 3) & 7];
        switch (op & 7) {
            case 0: return make_entry(m, Eb, Gb, OP_NONE, true);
            case 1: return make_entry(m, Ev, Gv, OP_NONE, true);
            case 2: return make_entry(m, Gb, Eb, OP_NONE, true);
            case 3: return make_entry(m, Gv, Ev, OP_NONE, true);
            case 4: return make_entry(m, OP_AL, Ib, OP_NONE, false);
            case 5: return make_entry(m, OP_rAX, Iz, OP_NONE, false);
        }
    }
    switch (op) {
        case 0x06: return make_entry("push", OP_ES, OP_NONE, OP_NONE, false);
        case 0x07: return make_entry("pop", OP_ES, OP_NONE, OP_NONE, false);
        case 0x0E: return make_entry("push", OP_CS, OP_NONE, OP_NONE, false);
        case 0x16: return make_entry("push", OP_SS, OP_NONE, OP_NONE, false);
        case 0x17: return make_entry("pop", OP_SS, OP_NONE, OP_NONE, false);
        case 0x1E: return make_entry("push", OP_DS, OP_NONE, OP_NONE, false);
        case 0x1F: return make_entry("pop", OP_DS, OP_NONE, OP_NONE, false);
    }
    if (op >= 0x40 && op <= 0x47) return make_entry("inc", (OperandType)(OP_rAX + (op - 0x40)), OP_NONE, OP_NONE, false);
    if (op >= 0x48 && op <= 0x4F) return make_entry("dec", (OperandType)(OP_rAX + (op - 0x48)), OP_NONE, OP_NONE, false);
    if (op >= 0x50 && op <= 0x57) return make_entry("push", (OperandType)(OP_rAX + (op - 0x50)), OP_NONE, OP_NONE, false);
    if (op >= 0x58 && op <= 0x5F) return make_entry("pop", (OperandType)(OP_rAX + (op - 0x58)), OP_NONE, OP_NONE, false);
    if (op == 0x60) return make_entry(pfx_op_size == 0x66 ? "pusha" : "pushad", OP_NONE, OP_NONE, OP_NONE, false);
    if (op == 0x61) return make_entry(pfx_op_size == 0x66 ? "popa" : "popad", OP_NONE, OP_NONE, OP_NONE, false);
    if (op == 0x68) return make_entry("push", Iz, OP_NONE, OP_NONE, false);
    if (op == 0x6A) return make_entry("push", Ib, OP_NONE, OP_NONE, false);
    if (op >= 0x70 && op <= 0x7F) return make_entry(kJcc[op & 0xF], Jb, OP_NONE, OP_NONE, false);

    if (op == 0x80 || op == 0x82) return make_entry(kGrp1[reg], Eb, Ib, OP_NONE, true);
    if (op == 0x81) return make_entry(kGrp1[reg], Ev, Iz, OP_NONE, true);
    if (op == 0x83) return make_entry(kGrp1[reg], Ev, Ib, OP_NONE, true);
    if (op == 0x84) return make_entry("test", Eb, Gb, OP_NONE, true);
    if (op == 0x85) return make_entry("test", Ev, Gv, OP_NONE, true);
    if (op == 0x86) return make_entry("xchg", Eb, Gb, OP_NONE, true);
    if (op == 0x87) return make_entry("xchg", Ev, Gv, OP_NONE, true);
    if (op == 0x88) return make_entry("mov", Eb, Gb, OP_NONE, true);
    if (op == 0x89) return make_entry("mov", Ev, Gv, OP_NONE, true);
    if (op == 0x8A) return make_entry("mov", Gb, Eb, OP_NONE, true);
    if (op == 0x8B) return make_entry("mov", Gv, Ev, OP_NONE, true);
    if (op == 0x8C) return make_entry("mov", Ew, Sw, OP_NONE, true);
    if (op == 0x8D) return make_entry("lea", Gv, M, OP_NONE, true);
    if (op == 0x8E) return make_entry("mov", Sw, Ew, OP_NONE, true);
    if (op == 0x90) return make_entry("nop", OP_NONE, OP_NONE, OP_NONE, false);
    if (op >= 0x91 && op <= 0x97) return make_entry("xchg", OP_rAX, (OperandType)(OP_rAX + (op - 0x90)), OP_NONE, false);
    if (op == 0x98) return make_entry(pfx_op_size == 0x66 ? "cbw" : "cwde", OP_NONE, OP_NONE, OP_NONE, false);
    if (op == 0x99) return make_entry(pfx_op_size == 0x66 ? "cwd" : "cdq", OP_NONE, OP_NONE, OP_NONE, false);
    if (op == 0x9C) return make_entry("pushf", OP_NONE, OP_NONE, OP_NONE, false);
    if (op == 0x9D) return make_entry("popf", OP_NONE, OP_NONE, OP_NONE, false);
    if (op == 0x9E) return make_entry("sahf", OP_NONE, OP_NONE, OP_NONE, false);
    if (op == 0x9F) return make_entry("lahf", OP_NONE, OP_NONE, OP_NONE, false);

    if (op == 0xA0) return make_entry("mov", OP_AL, Ob, OP_NONE, false);
    if (op == 0xA1) return make_entry("mov", OP_rAX, Ov, OP_NONE, false);
    if (op == 0xA2) return make_entry("mov", Ob, OP_AL, OP_NONE, false);
    if (op == 0xA3) return make_entry("mov", Ov, OP_rAX, OP_NONE, false);
    if (op == 0xA4) return make_entry("movsb", OP_NONE, OP_NONE, OP_NONE, false);
    if (op == 0xA5) return make_entry(pfx_op_size == 0x66 ? "movsw" : "movsd", OP_NONE, OP_NONE, OP_NONE, false);
    if (op == 0xA6) return make_entry("cmpsb", OP_NONE, OP_NONE, OP_NONE, false);
    if (op == 0xA7) return make_entry(pfx_op_size == 0x66 ? "cmpsw" : "cmpsd", OP_NONE, OP_NONE, OP_NONE, false);
    if (op == 0xA8) return make_entry("test", OP_AL, Ib, OP_NONE, false);
    if (op == 0xA9) return make_entry("test", OP_rAX, Iz, OP_NONE, false);
    if (op == 0xAA) return make_entry("stosb", OP_NONE, OP_NONE, OP_NONE, false);
    if (op == 0xAB) return make_entry(pfx_op_size == 0x66 ? "stosw" : "stosd", OP_NONE, OP_NONE, OP_NONE, false);
    if (op == 0xAC) return make_entry("lodsb", OP_NONE, OP_NONE, OP_NONE, false);
    if (op == 0xAD) return make_entry(pfx_op_size == 0x66 ? "lodsw" : "lodsd", OP_NONE, OP_NONE, OP_NONE, false);
    if (op == 0xAE) return make_entry("scasb", OP_NONE, OP_NONE, OP_NONE, false);
    if (op == 0xAF) return make_entry(pfx_op_size == 0x66 ? "scasw" : "scasd", OP_NONE, OP_NONE, OP_NONE, false);

    if (op >= 0xB0 && op <= 0xB7) return make_entry("mov", (OperandType)(OP_AL + (op - 0xB0)), Ib, OP_NONE, false);
    if (op >= 0xB8 && op <= 0xBF) return make_entry("mov", (OperandType)(OP_rAX + (op - 0xB8)), Iv, OP_NONE, false);

    if (op == 0xC0) return make_entry(kGrp2[reg], Eb, Ib, OP_NONE, true);
    if (op == 0xC1) return make_entry(kGrp2[reg], Ev, Ib, OP_NONE, true);
    if (op == 0xC2) return make_entry("ret", Iw, OP_NONE, OP_NONE, false);
    if (op == 0xC3) return make_entry("ret", OP_NONE, OP_NONE, OP_NONE, false);
    if (op == 0xC6) return make_entry("mov", Eb, Ib, OP_NONE, true);
    if (op == 0xC7) return make_entry("mov", Ev, Iz, OP_NONE, true);
    if (op == 0xC8) return make_entry("enter", Iw, Ib, OP_NONE, false);
    if (op == 0xC9) return make_entry("leave", OP_NONE, OP_NONE, OP_NONE, false);
    if (op == 0xCC) return make_entry("int3", OP_NONE, OP_NONE, OP_NONE, false);
    if (op == 0xCD) return make_entry("int", Ib, OP_NONE, OP_NONE, false);

    if (op == 0xD0) return make_entry(kGrp2[reg], Eb, OP_1, OP_NONE, true);
    if (op == 0xD1) return make_entry(kGrp2[reg], Ev, OP_1, OP_NONE, true);
    if (op == 0xD2) return make_entry(kGrp2[reg], Eb, OP_CL, OP_NONE, true);
    if (op == 0xD3) return make_entry(kGrp2[reg], Ev, OP_CL, OP_NONE, true);

    if (op >= 0xD8 && op <= 0xDF) {
        return make_entry("fpu", mod != 3 ? M : OP_NONE, OP_NONE, OP_NONE, true);
    }

    if (op == 0xE0) return make_entry("loopne", Jb, OP_NONE, OP_NONE, false);
    if (op == 0xE1) return make_entry("loope", Jb, OP_NONE, OP_NONE, false);
    if (op == 0xE2) return make_entry("loop", Jb, OP_NONE, OP_NONE, false);
    if (op == 0xE3) return make_entry("jecxz", Jb, OP_NONE, OP_NONE, false);
    if (op == 0xE8) return make_entry("call", Jz, OP_NONE, OP_NONE, false);
    if (op == 0xE9) return make_entry("jmp", Jz, OP_NONE, OP_NONE, false);
    if (op == 0xEB) return make_entry("jmp", Jb, OP_NONE, OP_NONE, false);

    if (op == 0xF4) return make_entry("hlt", OP_NONE, OP_NONE, OP_NONE, false);
    if (op == 0xF5) return make_entry("cmc", OP_NONE, OP_NONE, OP_NONE, false);
    if (op == 0xF6) {
        if (reg <= 1) return make_entry("test", Eb, Ib, OP_NONE, true);
        if (reg <= 3) return make_entry(kGrp3[reg], Eb, OP_NONE, OP_NONE, true);
        return make_entry(kGrp3[reg], OP_AL, Eb, OP_NONE, true);
    }
    if (op == 0xF7) {
        if (reg <= 1) return make_entry("test", Ev, Iz, OP_NONE, true);
        if (reg <= 3) return make_entry(kGrp3[reg], Ev, OP_NONE, OP_NONE, true);
        return make_entry(kGrp3[reg], OP_rAX, Ev, OP_NONE, true);
    }
    if (op == 0xF8) return make_entry("clc", OP_NONE, OP_NONE, OP_NONE, false);
    if (op == 0xF9) return make_entry("stc", OP_NONE, OP_NONE, OP_NONE, false);
    if (op == 0xFA) return make_entry("cli", OP_NONE, OP_NONE, OP_NONE, false);
    if (op == 0xFB) return make_entry("sti", OP_NONE, OP_NONE, OP_NONE, false);
    if (op == 0xFC) return make_entry("cld", OP_NONE, OP_NONE, OP_NONE, false);
    if (op == 0xFD) return make_entry("std", OP_NONE, OP_NONE, OP_NONE, false);
    if (op == 0xFE) return make_entry(reg == 0 ? "inc" : "dec", Eb, OP_NONE, OP_NONE, true);
    if (op == 0xFF) {
        static const char* kGrp5[8] = { "inc", "dec", "call", "call", "jmp", "jmp", "push", "grp5" };
        return make_entry(kGrp5[reg], Ev, OP_NONE, OP_NONE, true);
    }
    return make_entry("db", OP_NONE, OP_NONE, OP_NONE, false);
}

int ParseInstuction(const uint8_t* code, uint32_t eip, DecodeContext* d_ctx) {
    (void)eip;
    if (!code || !d_ctx) return -1;
    memset(d_ctx, 0, sizeof(*d_ctx));

    int pos = 0;
    while (pos < 15) {
        uint8_t b = code[pos];
        if (b == 0xF3) { d_ctx->pfx_rep = b; pos++; }
        else if (b == 0xF2) { d_ctx->pfx_repne = b; pos++; }
        else if (b == 0x66) { d_ctx->pfx_op_size = b; pos++; }
        else if (b == 0x67) { d_ctx->pfx_addr_size = b; pos++; }
        else if (b == 0x26 || b == 0x2E || b == 0x36 || b == 0x3E || b == 0x64 || b == 0x65) {
            d_ctx->pfx_seg = b; pos++;
        } else if (b == 0xF0) { pos++; }
        else break;
    }

    uint8_t op = code[pos++];
    if (op == 0x0F) {
        d_ctx->is_two_byte_opcode = true;
        op = code[pos++];
    }
    d_ctx->opcode = op;

    uint8_t peek = code[pos];
    uint8_t peek_mod = (peek >> 6) & 3;
    uint8_t peek_reg = (peek >> 3) & 7;
    d_ctx->entry = d_ctx->is_two_byte_opcode
        ? lookup_two_byte(op)
        : lookup_one_byte(op, peek_reg, peek_mod, d_ctx->pfx_op_size);

    if (d_ctx->entry.has_modrm) {
        d_ctx->has_modrm = true;
        d_ctx->modrm = code[pos++];
        d_ctx->mod = (d_ctx->modrm >> 6) & 3;
        d_ctx->reg = (d_ctx->modrm >> 3) & 7;
        d_ctx->rm  = d_ctx->modrm & 7;

        if (d_ctx->mod != 3) {
            if (d_ctx->rm == 4) {
                d_ctx->has_sib = true;
                d_ctx->sib = code[pos++];
                d_ctx->scale = (d_ctx->sib >> 6) & 3;
                d_ctx->index = (d_ctx->sib >> 3) & 7;
                d_ctx->base  = d_ctx->sib & 7;
            }
            if (d_ctx->mod == 0 && (d_ctx->rm == 5 || (d_ctx->has_sib && d_ctx->base == 5))) {
                memcpy(&d_ctx->disp, &code[pos], 4);
                pos += 4;
                d_ctx->disp_len = 4;
            } else if (d_ctx->mod == 1) {
                d_ctx->disp = (int8_t)code[pos++];
                d_ctx->disp_len = 1;
            } else if (d_ctx->mod == 2) {
                memcpy(&d_ctx->disp, &code[pos], 4);
                pos += 4;
                d_ctx->disp_len = 4;
            }
        }
    }

    OperandType ops[3] = { d_ctx->entry.op1, d_ctx->entry.op2, d_ctx->entry.op3 };
    int imm_idx = 0;
    for (int i = 0; i < 3; i++) {
        OperandType t = ops[i];
        if (t == OP_NONE) continue;
        int32_t val = 0;
        bool has_imm = false;
        if (t == Ib || t == Jb) {
            val = (int8_t)code[pos++];
            has_imm = true;
            if (!d_ctx->is_two_byte_opcode && (d_ctx->opcode >= 0xE0 && d_ctx->opcode <= 0xE2)) {
                d_ctx->disp_len = pos;
            }
        } else if (t == Iw) {
            uint16_t u16 = 0;
            memcpy(&u16, &code[pos], 2);
            pos += 2;
            val = u16;
            has_imm = true;
        } else if (t == Iv || t == Iz || t == Jz) {
            if (d_ctx->pfx_op_size == 0x66) {
                int16_t i16 = 0;
                memcpy(&i16, &code[pos], 2);
                pos += 2;
                val = i16;
            } else {
                memcpy(&val, &code[pos], 4);
                pos += 4;
            }
            has_imm = true;
        } else if (t == Ob || t == Ov) {
            memcpy(&val, &code[pos], 4);
            pos += 4;
            has_imm = true;
        }
        if (has_imm) {
            if (imm_idx == 0) d_ctx->imm = val;
            else d_ctx->imm2 = val;
            imm_idx++;
        }
    }

    d_ctx->instr_len = pos;
    return pos;
}

static void format_operand(const DecodeContext* d_ctx, OperandType t, char* out, size_t out_sz) {
    bool is16 = (d_ctx->pfx_op_size == 0x66);
    const char** rnames = is16 ? kGPR16 : kGPR32;
    switch (t) {
        case Gb: snprintf(out, out_sz, "%s", kGPR8[d_ctx->reg]); break;
        case Gw: snprintf(out, out_sz, "%s", kGPR16[d_ctx->reg]); break;
        case Gv: case Gz: snprintf(out, out_sz, "%s", rnames[d_ctx->reg]); break;
        case Sw: snprintf(out, out_sz, "%s", kSeg[d_ctx->reg % 6]); break;
        case Eb:
            if (d_ctx->mod == 3) snprintf(out, out_sz, "%s", kGPR8[d_ctx->rm]);
            else snprintf(out, out_sz, "byte ptr [%s%+d]", kGPR32[d_ctx->rm], d_ctx->disp);
            break;
        case Ew:
            if (d_ctx->mod == 3) snprintf(out, out_sz, "%s", kGPR16[d_ctx->rm]);
            else snprintf(out, out_sz, "word ptr [%s%+d]", kGPR32[d_ctx->rm], d_ctx->disp);
            break;
        case Ev:
            if (d_ctx->mod == 3) snprintf(out, out_sz, "%s", rnames[d_ctx->rm]);
            else snprintf(out, out_sz, "dword ptr [%s%+d]", kGPR32[d_ctx->rm], d_ctx->disp);
            break;
        case M:
            snprintf(out, out_sz, "[%s%+d]", kGPR32[d_ctx->rm], d_ctx->disp);
            break;
        case Ib: case Iw: case Iv: case Iz:
            snprintf(out, out_sz, "0x%X", (uint32_t)d_ctx->imm);
            break;
        case Jb: case Jz:
            snprintf(out, out_sz, "%+d", d_ctx->imm);
            break;
        default:
            if (t >= OP_AL && t <= OP_BH) snprintf(out, out_sz, "%s", kGPR8[t - OP_AL]);
            else if (t >= OP_rAX && t <= OP_rDI) snprintf(out, out_sz, "%s", rnames[t - OP_rAX]);
            else if (t >= OP_ES && t <= OP_GS) snprintf(out, out_sz, "%s", kSeg[t - OP_ES]);
            else if (t == OP_1) snprintf(out, out_sz, "1");
            else out[0] = '\0';
            break;
    }
}

void FormatInstruction(const uint8_t* code, DecodeContext* d_ctx) {
    (void)code;
    if (!d_ctx) return;
    char o1[48] = {0}, o2[48] = {0};
    if (d_ctx->entry.op1 != OP_NONE) format_operand(d_ctx, d_ctx->entry.op1, o1, sizeof(o1));
    if (d_ctx->entry.op2 != OP_NONE) format_operand(d_ctx, d_ctx->entry.op2, o2, sizeof(o2));

    const char* pfx = d_ctx->pfx_rep ? "rep " : (d_ctx->pfx_repne ? "repne " : "");
    if (o1[0] && o2[0]) {
        snprintf(d_ctx->asm_str, sizeof(d_ctx->asm_str), "%s%s %s, %s", pfx, d_ctx->entry.mnemonic, o1, o2);
    } else if (o1[0]) {
        snprintf(d_ctx->asm_str, sizeof(d_ctx->asm_str), "%s%s %s", pfx, d_ctx->entry.mnemonic, o1);
    } else {
        snprintf(d_ctx->asm_str, sizeof(d_ctx->asm_str), "%s%s", pfx, d_ctx->entry.mnemonic);
    }
}
