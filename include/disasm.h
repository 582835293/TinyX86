#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    OP_NONE = 0,
    Eb,
    Ew,
    Ev,
    Gb,
    Gw,
    Gv,
    Gz,
    Sw,
    Ib,
    Iw,
    Iv,
    Iz,
    Jb,
    Jz,
    Ob,
    Ov,
    M,
    OP_AL,
    OP_CL,
    OP_DL,
    OP_BL,
    OP_AH,
    OP_CH,
    OP_DH,
    OP_BH,
    OP_DX,
    OP_rAX,
    OP_rCX,
    OP_rDX,
    OP_rBX,
    OP_rSP,
    OP_rBP,
    OP_rSI,
    OP_rDI,
    OP_ES,
    OP_CS,
    OP_SS,
    OP_DS,
    OP_FS,
    OP_GS,
    OP_1
} OperandType;

typedef struct {
    const char* mnemonic;
    OperandType op1;
    OperandType op2;
    OperandType op3;
    bool has_modrm;
} OpcodeEntry;

typedef struct {
    uint8_t pfx_rep;
    uint8_t pfx_repne;
    uint8_t pfx_op_size;
    uint8_t pfx_addr_size;
    uint8_t pfx_seg;
    bool is_two_byte_opcode;
    uint8_t opcode;
    bool has_modrm;
    uint8_t modrm;
    uint8_t mod;
    uint8_t reg;
    uint8_t rm;
    bool has_sib;
    uint8_t sib;
    uint8_t scale;
    uint8_t index;
    uint8_t base;
    int32_t disp;
    int disp_len;
    int32_t imm;
    int32_t imm2;
    int instr_len;
    OpcodeEntry entry;
    char asm_str[128];
} DecodeContext;

int ParseInstuction(const uint8_t* code, uint32_t eip, DecodeContext* d_ctx);
void FormatInstruction(const uint8_t* code, DecodeContext* d_ctx);

#ifdef __cplusplus
}
#endif
