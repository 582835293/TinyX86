#include "pch.h"
#include "../TinyX86.h"
#include "../TinyX86.c"
extern "C" int ParseInstuction(uint8_t* buffer, uint32_t eip, DecodeContext* out_ctx) {
    (void)eip;
    if (!buffer || !out_ctx) {
        return -1;
    }
    *out_ctx = DecodeContext{};
    out_ctx->opcode = buffer[0];
    out_ctx->instr_len = 1;
    return 1;
}
extern "C" void FormatInstruction(uint8_t* buffer, DecodeContext* out_ctx) {
    (void)buffer;
    (void)out_ctx;
}
namespace {
DecodeContext MakeDecodeContext() {
    DecodeContext ctx{};
    ctx.pfx_op_size = 0;
    ctx.entry.op1 = NONE;
    ctx.entry.op2 = NONE;
    ctx.entry.op3 = NONE;
    return ctx;
}
}
TEST(CpuRegisterTests, ReadWriteGprHandlesSizesAndInvalid) {
    CPU_Context ctx{};
    EXPECT_TRUE(WriteGPR(&ctx, 0, 32, 0x12345678));
    EXPECT_EQ(0x12345678u, ReadGPR(&ctx, 0, 32));
    EXPECT_TRUE(WriteGPR(&ctx, 0, 16, 0xABCD));
    EXPECT_EQ(0xABCDu, ReadGPR(&ctx, 0, 16));
    EXPECT_TRUE(WriteGPR(&ctx, 4, 8, 0x12));
    EXPECT_EQ(0x12u, ReadGPR(&ctx, 4, 8));
    EXPECT_FALSE(WriteGPR(&ctx, 8, 32, 0x1));
    EXPECT_EQ(0u, ReadGPR(&ctx, 8, 32));
}
TEST(CpuAddressTests, GetEffectiveAddressHandlesSibAndDisp32) {
    CPU_Context ctx{};
    DecodeContext d_ctx = MakeDecodeContext();
    d_ctx.mod = 0;
    d_ctx.modrm = 5;
    d_ctx.rm = 5;
    d_ctx.disp = 0x20;
    d_ctx.has_sib = false;
    EXPECT_EQ(0x20u, GetEffectiveAddress(&ctx, &d_ctx));
    ctx.EAX.I32 = 0x1000;
    ctx.ECX.I32 = 3;
    d_ctx.has_sib = true;
    d_ctx.base = 0;
    d_ctx.index = 1;
    d_ctx.scale = 2;
    d_ctx.mod = 1;
    d_ctx.disp = 0x10;
    EXPECT_EQ(0x101Cu, GetEffectiveAddress(&ctx, &d_ctx));
}
TEST(CpuOperandTests, GetOperandValueReadsRegistersAndMemory) {
    CPU_Context ctx{};
    DecodeContext d_ctx = MakeDecodeContext();
    ctx.EAX.I32 = 0x11223344;
    d_ctx.entry.op1 = Gv;
    d_ctx.reg = 0;
    EXPECT_EQ(0x11223344u, GetOperandValue(&ctx, &d_ctx, 0));
    uint8_t memory[4] = { 0x7F, 0, 0, 0 };
    auto address = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(memory));
    d_ctx.entry.op1 = Eb;
    d_ctx.mod = 0;
    d_ctx.modrm = 5;
    d_ctx.rm = 5;
    d_ctx.disp = static_cast<int32_t>(address);
    d_ctx.has_sib = false;
    EXPECT_EQ(0x7Fu, GetOperandValue(&ctx, &d_ctx, 0));
}
TEST(CpuOperandTests, SetOperandValueWritesRegistersAndMemory) {
    CPU_Context ctx{};
    DecodeContext d_ctx = MakeDecodeContext();
    d_ctx.entry.op1 = OP_rAX;
    SetOperandValue(&ctx, &d_ctx, 0, 0x55667788);
    EXPECT_EQ(0x55667788u, ctx.EAX.I32);
    uint8_t memory[4] = { 0, 0, 0, 0 };
    auto address = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(memory));
    d_ctx.entry.op1 = Eb;
    d_ctx.mod = 0;
    d_ctx.modrm = 5;
    d_ctx.rm = 5;
    d_ctx.disp = static_cast<int32_t>(address);
    d_ctx.has_sib = false;
    SetOperandValue(&ctx, &d_ctx, 0, 0xAA);
    EXPECT_EQ(0xAAu, memory[0]);
}
TEST(CpuMoveTests, ExecMovRegImmWritesDestination) {
    CPU_Context ctx{};
    DecodeContext d_ctx = MakeDecodeContext();
    d_ctx.entry.op1 = OP_rAX;
    d_ctx.entry.op2 = Iv;
    d_ctx.imm = 0x12345678;
    Exec_MOV_Generic(&ctx, &d_ctx);
    EXPECT_EQ(0x12345678u, ctx.EAX.I32);
}
TEST(CpuFlagTests, CalcPfHandlesEvenParity) {
    EXPECT_EQ(1, CalcPF(0x00));
    EXPECT_EQ(0, CalcPF(0x01));
    EXPECT_EQ(1, CalcPF(0x03));
}
TEST(CpuFlagTests, UpdateEflagsHandlesAddAndLogic) {
    CPU_Context ctx{};
    // 初始化清零
        // --- Case 1: 8-bit ADD with Carry and Zero result ---
        // 0xFF (255/-1) + 0x01 (1) = 0x100 (256). Truncated to 8-bit: 0x00.
    UpdateEFLAGS(&ctx,
        0x100, 0xFF, 0x01, 8
        , ALU_ADD);
    EXPECT_EQ(
        1u, ctx.EFLAGS.CF); // 进位发生
    EXPECT_EQ(
        1u, ctx.EFLAGS.ZF); // 结果截断后为0
    EXPECT_EQ(
        0u, ctx.EFLAGS.OF); // 负+正不可能溢出
    // --- Case 2: 8-bit AND (Logic operation) ---
    // 0xAA (10101010) & 0x55 (01010101) = 0x00.
    // 逻辑运算应清除 CF/OF，并根据结果设置 ZF/SF/PF
    UpdateEFLAGS(&ctx,
        0x00, 0xAA, 0x55, 8
        , ALU_AND);
    EXPECT_EQ(
        0u, ctx.EFLAGS.CF); // 逻辑运算清除 CF
    EXPECT_EQ(
        0u, ctx.EFLAGS.OF); // 逻辑运算清除 OF
    EXPECT_EQ(
        1u, ctx.EFLAGS.ZF); // 结果为0，ZF应置位 (建议补充此断言)
}
TEST(CpuSizeTests, GetOperandBitSizeHonorsPrefixAndTypes) {
    CPU_Context ctx{};
    DecodeContext d_ctx = MakeDecodeContext();
    EXPECT_EQ(8, GetOperandBitSize(&ctx, &d_ctx, Eb));
    d_ctx.pfx_op_size = 0x66;
    EXPECT_EQ(16, GetOperandBitSize(&ctx, &d_ctx, Ev));
    d_ctx.pfx_op_size = 0;
    EXPECT_EQ(32, GetOperandBitSize(&ctx, &d_ctx, OP_rAX));
}
TEST(CpuAluTests, ExecAluGenericAddsAndCompares) {
    CPU_Context ctx{};
    DecodeContext d_ctx = MakeDecodeContext();
    ctx.EAX.I32 = 1;
    d_ctx.entry.op1 = Gv;
    d_ctx.entry.op2 = Iv;
    d_ctx.reg = 0;
    d_ctx.imm = 2;
    Exec_ALU_Generic(&ctx, &d_ctx, ALU_ADD, false);
    EXPECT_EQ(3u, ctx.EAX.I32);
    ctx.EAX.I32 = 5;
    d_ctx.imm = 5;
    Exec_ALU_Generic(&ctx, &d_ctx, ALU_CMP, true);
    EXPECT_EQ(5u, ctx.EAX.I32);
    EXPECT_EQ(1u, ctx.EFLAGS.ZF);
}
TEST(CpuAluTests, ExecGroup1SelectsOperationByReg) {
    CPU_Context ctx{};
    DecodeContext d_ctx = MakeDecodeContext();
    ctx.EAX.I32 = 1;
    d_ctx.entry.op1 = Gv;
    d_ctx.entry.op2 = Iv;
    d_ctx.reg = 0;
    d_ctx.imm = 4;
    Exec_Group1(&ctx, &d_ctx);
    EXPECT_EQ(5u, ctx.EAX.I32);
}
TEST(CpuStackTests, ExecPushWritesStackAndAdjustsEsp) {
    CPU_Context ctx{};
    DecodeContext d_ctx = MakeDecodeContext();
    uint8_t stack[8] = {};
    auto base = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(stack));
    ctx.ESP.I32 = base + 8;
    ctx.EAX.I32 = 0x12345678;
    d_ctx.entry.op1 = OP_rAX;
    Exec_PUSH(&ctx, &d_ctx);
    EXPECT_EQ(base + 4, ctx.ESP.I32);
    EXPECT_EQ(0x12345678u, *reinterpret_cast<uint32_t*>(ctx.ESP.I32));
}
TEST(CpuStackTests, ExecPopReadsStackAndWritesOperand) {
    CPU_Context ctx{};
    DecodeContext d_ctx = MakeDecodeContext();
    uint8_t stack[8] = {};
    auto base = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(stack));
    ctx.ESP.I32 = base;
    *reinterpret_cast<uint32_t*>(ctx.ESP.I32) = 0xAABBCCDD;
    d_ctx.entry.op1 = OP_rBX;
    Exec_POP(&ctx, &d_ctx);
    EXPECT_EQ(0xAABBCCDDu, ctx.EBX.I32);
    EXPECT_EQ(base + 4, ctx.ESP.I32);
}
TEST(CpuBranchTests, CheckConditionEvaluatesFlags) {
    CPU_Context ctx{};
    ctx.EFLAGS.ZF = 1;
    EXPECT_TRUE(CheckCondition(&ctx, 0x74));
    EXPECT_FALSE(CheckCondition(&ctx, 0x75));
}
TEST(CpuBranchTests, ExecBranchAdjustsEipWhenTaken) {
    CPU_Context ctx{};
    DecodeContext d_ctx = MakeDecodeContext();
    ctx.EIP = 100;
    d_ctx.instr_len = 2;
    d_ctx.imm = 5;
    EXPECT_TRUE(Exec_Branch(&ctx, &d_ctx, false));
    EXPECT_EQ(107u, ctx.EIP);
    ctx.EIP = 200;
    ctx.EFLAGS.ZF = 0;
    d_ctx.opcode = 0x74;
    d_ctx.imm = 10;
    EXPECT_FALSE(Exec_Branch(&ctx, &d_ctx, true));
    EXPECT_EQ(200u, ctx.EIP);
}
TEST(CpuBranchTests, ExecCallPushesReturnAndUpdatesEip) {
    CPU_Context ctx{};
    DecodeContext d_ctx = MakeDecodeContext();
    uint8_t stack[8] = {};
    auto base = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(stack));
    ctx.ESP.I32 = base + 8;
    ctx.EIP = 100;
    d_ctx.instr_len = 5;
    d_ctx.imm = 3;
    EXPECT_TRUE(Exec_CALL(&ctx, &d_ctx));
    EXPECT_EQ(base + 4, ctx.ESP.I32);
    EXPECT_EQ(105u, *reinterpret_cast<uint32_t*>(ctx.ESP.I32));
    EXPECT_EQ(108u, ctx.EIP);
}
TEST(CpuBranchTests, ExecRetRestoresEipAndStack) {
    CPU_Context ctx{};
    DecodeContext d_ctx = MakeDecodeContext();
    uint8_t stack[12] = {};
    auto base = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(stack));
    ctx.ESP.I32 = base;
    *reinterpret_cast<uint32_t*>(ctx.ESP.I32) = 0xDEADBEEF;
    d_ctx.opcode = 0xC2;
    d_ctx.imm = 4;
    EXPECT_TRUE(Exec_RET(&ctx, &d_ctx));
    EXPECT_EQ(0xDEADBEEFu, ctx.EIP);
    EXPECT_EQ(base + 8, ctx.ESP.I32);
}
TEST(CpuIncDecTests, ExecIncAndDecAdjustRegisters) {
    CPU_Context ctx{};
    DecodeContext d_ctx = MakeDecodeContext();
    ctx.EAX.I32 = 1;
    d_ctx.opcode = 0x40;
    Exec_INC(&ctx, &d_ctx);
    EXPECT_EQ(2u, ctx.EAX.I32);
    d_ctx.opcode = 0x48;
    Exec_DEC(&ctx, &d_ctx);
    EXPECT_EQ(1u, ctx.EAX.I32);
}
TEST(CpuFlagTests, UpdateLogicFlagsSetsZfSfPf) {
    CPU_Context ctx{};
    UpdateLogicFlags(&ctx, 0, 8);
    EXPECT_EQ(1u, ctx.EFLAGS.ZF);
    EXPECT_EQ(0u, ctx.EFLAGS.SF);
    EXPECT_EQ(1u, ctx.EFLAGS.PF);
}
TEST(CpuShiftTests, ExecGroup2ShiftLeftUpdatesFlags) {
    CPU_Context ctx{};
    DecodeContext d_ctx = MakeDecodeContext();
    ctx.EAX.I32 = 0x80000001;
    d_ctx.entry.op1 = Ev;
    d_ctx.reg = 4;
    d_ctx.opcode = 0xD1;
    d_ctx.mod = 3;
    d_ctx.rm = 0;
    Exec_Group2(&ctx, &d_ctx);
    EXPECT_EQ(0x00000002u, ctx.EAX.I32);
    EXPECT_EQ(1u, ctx.EFLAGS.CF);
    EXPECT_EQ(1u, ctx.EFLAGS.OF);
}
TEST(CpuGroup3Tests, GetGroup3SourceSelectsOperand) {
    CPU_Context ctx{};
    DecodeContext d_ctx = MakeDecodeContext();
    ctx.EAX.I32 = 0x34;
    d_ctx.entry.op1 = Eb;
    d_ctx.entry.op2 = Ib;
    d_ctx.mod = 3;
    d_ctx.rm = 0;
    d_ctx.imm = 0x12;
    d_ctx.reg = 4;
    EXPECT_EQ(0x12u, GetGroup3Source(&ctx, &d_ctx));
    d_ctx.reg = 2;
    EXPECT_EQ(0x34u, GetGroup3Source(&ctx, &d_ctx));
}
TEST(CpuGroup3Tests, SetGroup3DestWritesOperand) {
    CPU_Context ctx{};
    DecodeContext d_ctx = MakeDecodeContext();
    d_ctx.entry.op1 = OP_rAX;
    SetGroup3Dest(&ctx, &d_ctx, 0x55);
    EXPECT_EQ(0x55u, ctx.EAX.I32);
}
TEST(CpuGroup3Tests, ExecGroup3NotAndMulBehave) {
    CPU_Context ctx{};
    DecodeContext d_ctx = MakeDecodeContext();
    ctx.EAX.I32 = 0x0F0F0F0F;
    d_ctx.entry.op1 = Ev;
    d_ctx.mod = 3;
    d_ctx.rm = 0;
    d_ctx.reg = 2;
    Exec_Group3(&ctx, &d_ctx);
    EXPECT_EQ(0xF0F0F0F0u, ctx.EAX.I32);
    ctx.EAX.I32 = 0x03;
    ctx.ECX.I32 = 0x04;
    d_ctx.entry.op1 = OP_AL;
    d_ctx.entry.op2 = Eb;
    d_ctx.mod = 3;
    d_ctx.rm = 1;
    d_ctx.reg = 4;
    Exec_Group3(&ctx, &d_ctx);
    EXPECT_EQ(12u, ctx.EAX.I16);
    EXPECT_EQ(0u, ctx.EFLAGS.CF);
}
TEST(CpuExecutionTests, ExecuteInstructionHandlesNop) {
    CPU_Context ctx{};
    DecodeContext d_ctx = MakeDecodeContext();
    d_ctx.opcode = 0x90;
    EXPECT_FALSE(ExecuteInstruction(&ctx, &d_ctx));
}
TEST(CpuExecutionTests, RunCpuExecutesSingleInstruction) {
    CPU_Context ctx{};
    uint8_t program[] = { 0x90 };
    ctx.EIP = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(program));
    EXPECT_EQ(0, runcpu(&ctx, 1));
    EXPECT_EQ(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(program)) + 1, ctx.EIP);
}
TEST(CpuDataTransferTests, ExecMovzxAndMovsxExtendValues) {
    CPU_Context ctx{};
    DecodeContext d_ctx = MakeDecodeContext();
    ctx.ECX.I8.L = 0x80;
    d_ctx.entry.op1 = Gv;
    d_ctx.entry.op2 = Eb;
    d_ctx.reg = 0;
    d_ctx.mod = 3;
    d_ctx.rm = 1;
    Exec_MOVZX(&ctx, &d_ctx, 8);
    EXPECT_EQ(0x80u, ctx.EAX.I32);
    ctx.EAX.I32 = 0;
    Exec_MOVSX(&ctx, &d_ctx, 8);
    EXPECT_EQ(0xFFFFFF80u, ctx.EAX.I32);
}
TEST(CpuDataTransferTests, ExecSignExtendHandlesCbwCwdeCwdCdq) {
    CPU_Context ctx{};
    DecodeContext d_ctx = MakeDecodeContext();
    d_ctx.opcode = 0x98;
    d_ctx.pfx_op_size = 0x66;
    ctx.EAX.I8.L = 0x80;
    Exec_SignExtend(&ctx, &d_ctx);
    EXPECT_EQ(0xFF80u, ctx.EAX.I16);
    d_ctx.pfx_op_size = 0;
    ctx.EAX.I16 = 0x8000;
    Exec_SignExtend(&ctx, &d_ctx);
    EXPECT_EQ(0xFFFF8000u, ctx.EAX.I32);
    d_ctx.opcode = 0x99;
    d_ctx.pfx_op_size = 0x66;
    ctx.EAX.I16 = 0x8000;
    Exec_SignExtend(&ctx, &d_ctx);
    EXPECT_EQ(0xFFFFu, ctx.EDX.I16);
    d_ctx.pfx_op_size = 0;
    ctx.EAX.I32 = 0x7FFFFFFF;
    Exec_SignExtend(&ctx, &d_ctx);
    EXPECT_EQ(0u, ctx.EDX.I32);
}
TEST(CpuDataTransferTests, ExecLeaAndXchgUpdateOperands) {
    CPU_Context ctx{};
    DecodeContext d_ctx = MakeDecodeContext();
    d_ctx.entry.op1 = Gv;
    d_ctx.reg = 0;
    d_ctx.mod = 0;
    d_ctx.modrm = 5;
    d_ctx.rm = 5;
    d_ctx.disp = 0x1234;
    d_ctx.has_sib = false;
    Exec_LEA(&ctx, &d_ctx);
    EXPECT_EQ(0x1234u, ctx.EAX.I32);
    d_ctx.entry.op1 = Gv;
    d_ctx.entry.op2 = Ev;
    d_ctx.reg = 0;
    d_ctx.mod = 3;
    d_ctx.rm = 1;
    ctx.EAX.I32 = 0x11111111;
    ctx.ECX.I32 = 0x22222222;
    Exec_XCHG(&ctx, &d_ctx);
    EXPECT_EQ(0x22222222u, ctx.EAX.I32);
    EXPECT_EQ(0x11111111u, ctx.ECX.I32);
}
TEST(CpuStackTests, ExecPushaAndPopaPreserveRegisters) {
    CPU_Context ctx{};
    DecodeContext d_ctx = MakeDecodeContext();
    uint8_t stack[64] = {};
    auto base = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(stack));
    ctx.ESP.I32 = base + 64;
    ctx.EAX.I32 = 1;
    ctx.ECX.I32 = 2;
    ctx.EDX.I32 = 3;
    ctx.EBX.I32 = 4;
    ctx.ESP.I32 = base + 64;
    ctx.EBP.I32 = 6;
    ctx.ESI.I32 = 7;
    ctx.EDI.I32 = 8;
    Exec_PUSHA(&ctx, &d_ctx);
    EXPECT_EQ(base + 32, ctx.ESP.I32);
    EXPECT_EQ(8u, *reinterpret_cast<uint32_t*>(ctx.ESP.I32));
    ctx.EAX.I32 = ctx.ECX.I32 = ctx.EDX.I32 = ctx.EBX.I32 = 0;
    ctx.EBP.I32 = ctx.ESI.I32 = ctx.EDI.I32 = 0;
    Exec_POPA(&ctx, &d_ctx);
    EXPECT_EQ(1u, ctx.EAX.I32);
    EXPECT_EQ(2u, ctx.ECX.I32);
    EXPECT_EQ(3u, ctx.EDX.I32);
    EXPECT_EQ(4u, ctx.EBX.I32);
    EXPECT_EQ(6u, ctx.EBP.I32);
    EXPECT_EQ(7u, ctx.ESI.I32);
    EXPECT_EQ(8u, ctx.EDI.I32);
    EXPECT_EQ(base + 64, ctx.ESP.I32);
}
TEST(CpuStackTests, ExecPushfPopfRoundTripsFlags) {
    CPU_Context ctx{};
    uint8_t stack[8] = {};
    auto base = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(stack));
    ctx.ESP.I32 = base + 8;
    ctx.EFLAGS.CF = 1;
    ctx.EFLAGS.ZF = 1;
    ctx.EFLAGS.SF = 0;
    Exec_PUSHF(&ctx);
    ctx.EFLAGS.Value = 0;
    Exec_POPF(&ctx);
    EXPECT_EQ(1u, ctx.EFLAGS.CF);
    EXPECT_EQ(1u, ctx.EFLAGS.ZF);
    EXPECT_EQ(0u, ctx.EFLAGS.SF);
}
TEST(CpuStackTests, ExecEnterLeaveManageStackFrame) {
    CPU_Context ctx{};
    DecodeContext d_ctx = MakeDecodeContext();
    uint8_t stack[64] = {};
    auto base = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(stack));
    ctx.ESP.I32 = base + 64;
    ctx.EBP.I32 = base + 48;
    d_ctx.imm = 8;
    d_ctx.imm2 = 0;
    Exec_ENTER(&ctx, &d_ctx);
    EXPECT_EQ(base + 60, ctx.EBP.I32);
    EXPECT_EQ(base + 52, ctx.ESP.I32);
    Exec_LEAVE(&ctx);
    EXPECT_EQ(base + 48, ctx.EBP.I32);
    EXPECT_EQ(base + 64, ctx.ESP.I32);
}
TEST(CpuAluTests, ExecGroup4IncDecUpdateByteOperand) {
    CPU_Context ctx{};
    DecodeContext d_ctx = MakeDecodeContext();
    ctx.EAX.I8.L = 0x10;
    d_ctx.entry.op1 = Eb;
    d_ctx.mod = 3;
    d_ctx.rm = 0;
    d_ctx.reg = 0;
    Exec_Group4(&ctx, &d_ctx);
    EXPECT_EQ(0x11u, ctx.EAX.I8.L);
    d_ctx.reg = 1;
    Exec_Group4(&ctx, &d_ctx);
    EXPECT_EQ(0x10u, ctx.EAX.I8.L);
}
TEST(CpuBranchTests, ExecGroup5HandlesCallAndPush) {
    CPU_Context ctx{};
    DecodeContext d_ctx = MakeDecodeContext();
    uint8_t stack[16] = {};
    auto base = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(stack));
    ctx.ESP.I32 = base + 16;
    ctx.EIP = 100;
    ctx.EAX.I32 = 0x200;
    d_ctx.entry.op1 = Ev;
    d_ctx.mod = 3;
    d_ctx.rm = 0;
    d_ctx.reg = 2;
    d_ctx.instr_len = 5;
    EXPECT_TRUE(Exec_Group5(&ctx, &d_ctx));
    EXPECT_EQ(0x200u, ctx.EIP);
    EXPECT_EQ(105u, *reinterpret_cast<uint32_t*>(ctx.ESP.I32));
    d_ctx.reg = 6;
    ctx.EAX.I32 = 0x1234;
    Exec_Group5(&ctx, &d_ctx);
    EXPECT_EQ(0x1234u, *reinterpret_cast<uint32_t*>(ctx.ESP.I32));
}
TEST(CpuBranchTests, ExecSetccAndCmovccHonorConditions) {
    CPU_Context ctx{};
    DecodeContext d_ctx = MakeDecodeContext();
    ctx.EFLAGS.ZF = 1;
    d_ctx.opcode = 0x94;
    d_ctx.entry.op1 = Eb;
    d_ctx.mod = 3;
    d_ctx.rm = 1;
    Exec_SETcc(&ctx, &d_ctx);
    EXPECT_EQ(1u, ctx.ECX.I8.L);
    ctx.EAX.I32 = 0x11111111;
    ctx.ECX.I32 = 0x22222222;
    d_ctx.opcode = 0x44;
    d_ctx.entry.op1 = Gv;
    d_ctx.entry.op2 = Ev;
    d_ctx.reg = 0;
    d_ctx.mod = 3;
    d_ctx.rm = 1;
    Exec_CMOVcc(&ctx, &d_ctx);
    EXPECT_EQ(0x22222222u, ctx.EAX.I32);
}
TEST(CpuAluTests, ExecImul2OpUpdatesResultAndFlags) {
    CPU_Context ctx{};
    DecodeContext d_ctx = MakeDecodeContext();
    d_ctx.entry.op1 = Gv;
    d_ctx.entry.op2 = Ev;
    d_ctx.mod = 3;
    d_ctx.reg = 0;
    d_ctx.rm = 1;
    ctx.EAX.I32 = 10;
    ctx.ECX.I32 = 20;
    Exec_IMUL_2_Op(&ctx, &d_ctx);
    EXPECT_EQ(200u, ctx.EAX.I32);
    EXPECT_EQ(0u, ctx.EFLAGS.CF);
    EXPECT_EQ(0u, ctx.EFLAGS.OF);
    ctx.EAX.I32 = 0x80000000;
    ctx.ECX.I32 = 2;
    Exec_IMUL_2_Op(&ctx, &d_ctx);
    EXPECT_EQ(1u, ctx.EFLAGS.CF);
    EXPECT_EQ(1u, ctx.EFLAGS.OF);
}
TEST(CpuBranchTests, ExecLoopAdjustsEipAndCountsDown) {
    CPU_Context ctx{};
    DecodeContext d_ctx = MakeDecodeContext();
    ctx.EIP = 100;
    ctx.ECX.I32 = 2;
    d_ctx.opcode = 0xE2;
    d_ctx.disp_len = 2;
    d_ctx.imm = 4;
    EXPECT_TRUE(Exec_LOOP(&ctx, &d_ctx));
    EXPECT_EQ(1u, ctx.ECX.I32);
    EXPECT_EQ(106u, ctx.EIP);
    EXPECT_FALSE(Exec_LOOP(&ctx, &d_ctx));
    EXPECT_EQ(0u, ctx.ECX.I32);
}
// ==========================================
// 补充测试：字符串操作 (MOVS, STOS, REP)
// ==========================================
TEST(CpuStringTests, ExecMovsCopiesMemoryAndUpdatesPointers) {
    CPU_Context ctx{};
    DecodeContext d_ctx = MakeDecodeContext();
    uint8_t src_mem[4] = { 0xAA, 0xBB, 0, 0 };
    uint8_t dst_mem[4] = { 0, 0, 0, 0 };
    ctx.ESI.I32 = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(src_mem));
    ctx.EDI.I32 = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(dst_mem));
    ctx.EFLAGS.DF = 0; // 正向
    // MOVSB (A4)
    d_ctx.opcode = 0xA4;
    Exec_StringOp(&ctx, &d_ctx);
    EXPECT_EQ(0xAAu, dst_mem[0]);
    EXPECT_EQ(ctx.ESI.I32, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(src_mem)) + 1);
    EXPECT_EQ(ctx.EDI.I32, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(dst_mem)) + 1);
}
TEST(CpuStringTests, ExecRepStosFillsMemory) {
    CPU_Context ctx{};
    DecodeContext d_ctx = MakeDecodeContext();
    uint32_t dst_mem[4] = { 0 };
    ctx.EDI.I32 = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(dst_mem));
    ctx.ECX.I32 = 2; // 循环2次
    ctx.EAX.I32 = 0xDEADBEEF;
    ctx.EFLAGS.DF = 0;
    // REP STOSD (AB)
    d_ctx.opcode = 0xAB;
    d_ctx.pfx_rep = true;
    // 模拟 runcpu 的循环调用
    while (Exec_REP_StringOp(&ctx, &d_ctx)) {
        // Continue
    }
    EXPECT_EQ(0xDEADBEEFu, dst_mem[0]);
    EXPECT_EQ(0xDEADBEEFu, dst_mem[1]);
    EXPECT_EQ(0u, dst_mem[2]); // 第3个未被覆盖
    EXPECT_EQ(0u, ctx.ECX.I32); // 计数器归零
}
// ==========================================
// 补充测试：FPU 浮点运算
// ==========================================
TEST(CpuFpuTests, ExecFldAndFaddWork) {
    CPU_Context ctx{};
    DecodeContext d_ctx = MakeDecodeContext();
    float val1 = 1.5f;
    float val2 = 2.5f;
    // 1. FLD val1 (D9 /0)
    d_ctx.opcode = 0xD9;
    d_ctx.mod = 0; d_ctx.reg = 0; d_ctx.rm = 5; // ModRM指向内存
    d_ctx.disp = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&val1));
    Exec_FPU(&ctx, &d_ctx);
    // 2. FADD val2 (D8 /0) -> ST(0) += mem
    d_ctx.opcode = 0xD8;
    d_ctx.disp = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&val2));
    Exec_FPU(&ctx, &d_ctx);
    // 检查 ST(0) (需要访问 FPU 内部结构，假设结构体定义可见)
    // 物理寄存器索引 = (TOP + 0) % 8. 
    // PUSH 了一次，TOP 应该是 7 (初始0, 减1后为7)
    int top_idx = ctx.FPU.SW.TOP;
    EXPECT_DOUBLE_EQ(4.0, ctx.FPU.Regs[top_idx]);
}
// ==========================================
// 补充测试：Group 2 (移位与循环移位)
// ==========================================
TEST(CpuGroup2Tests, ExecRorAndSarVerifyFlags) {
    CPU_Context ctx{};
    DecodeContext d_ctx = MakeDecodeContext();
    // Test 1: ROR (Reg=1)
    // 0x03 (0000...0011) ROR 1 -> 0x80000001, CF=1, OF=1 (MSB ^ MSB-1)
    ctx.EAX.I32 = 0x03;
    // 【修复点】：Group 2 的操作数在 R/M 字段，所以类型必须是 Ev，不能是 Gv
    // Gv 会导致 GetOperandValue 读取 d_ctx.reg (即操作码扩展字段 1)，而不是 d_ctx.rm (0)
    d_ctx.entry.op1 = Ev;
    d_ctx.reg = 1; // ROR (Opcode Extension)
    d_ctx.opcode = 0xD1; // Shift by 1
    d_ctx.mod = 3; d_ctx.rm = 0; // rm=0 -> EAX
    Exec_Group2(&ctx, &d_ctx);
    EXPECT_EQ(0x80000001u, ctx.EAX.I32);
    EXPECT_EQ(1u, ctx.EFLAGS.CF);
    EXPECT_EQ(1u, ctx.EFLAGS.OF); // MSB(1) ^ MSB-1(0) = 1
    // Test 2: SAR (Reg=7) - 算术右移保留符号
    // 0x80000000 SAR 1 -> 0xC0000000
    ctx.EAX.I32 = 0x80000000;
    d_ctx.reg = 7; // SAR (Opcode Extension)
    // 【修复点】：确保这里也是 Ev (虽然上面已经改了，但逻辑上属于同一个 Context)
    d_ctx.entry.op1 = Ev;
    Exec_Group2(&ctx, &d_ctx);
    EXPECT_EQ(0xC0000000u, ctx.EAX.I32);
    EXPECT_EQ(0u, ctx.EFLAGS.CF); // 移出的是0
}
// ==========================================
// 补充测试：Group 3 (除法)
// ==========================================
TEST(CpuGroup3Tests, ExecDivHandlesUnsignedDivision) {
    CPU_Context ctx{};
    DecodeContext d_ctx = MakeDecodeContext();
    // EDX:EAX / ECX = 20 / 4 = 5
    ctx.EDX.I32 = 0;
    ctx.EAX.I32 = 20;
    ctx.ECX.I32 = 4;
    // 【修复点】：同时设置 op1 和 op2
    // op1 用于 Exec_Group3 内部判断操作数大小 (Size)
    // op2 用于 GetGroup3Source 读取除数的值 (Src)
    d_ctx.entry.op1 = Ev;
    d_ctx.entry.op2 = Ev;
    d_ctx.mod = 3; d_ctx.rm = 1; // rm=1 is ECX
    d_ctx.reg = 6; // DIV
    Exec_Group3(&ctx, &d_ctx);
    EXPECT_EQ(5u, ctx.EAX.I32); // Quotient
    EXPECT_EQ(0u, ctx.EDX.I32); // Remainder
}
TEST(CpuGroup3Tests, ExecIdivHandlesSignedDivision) {
    CPU_Context ctx{};
    DecodeContext d_ctx = MakeDecodeContext();
    // -20 / 4 = -5
    ctx.EAX.I32 = -20;
    ctx.EDX.I32 = -1; // Sign extension of -20
    ctx.ECX.I32 = 4;
    // 【修复点】：同上，同时设置 op1 和 op2
    d_ctx.entry.op1 = Ev;
    d_ctx.entry.op2 = Ev;
    d_ctx.mod = 3; d_ctx.rm = 1;
    d_ctx.reg = 7; // IDIV
    Exec_Group3(&ctx, &d_ctx);
    EXPECT_EQ(static_cast<uint32_t>(-5), ctx.EAX.I32);
    EXPECT_EQ(0u, ctx.EDX.I32);
}
// ==========================================
// 补充测试：系统指令与标志位
// ==========================================
TEST(CpuSystemTests, ExecSahfLahfTransferFlags) {
    CPU_Context ctx{};
    // LAHF: Load AH from Flags
    ctx.EFLAGS.SF = 1;
    ctx.EFLAGS.ZF = 1;
    ctx.EFLAGS.CF = 1;
    Exec_LAHF(&ctx);
    // AH bits: SF(7), ZF(6), X, AF(4), X, PF(2), X, CF(0)
    // 11000101 = 0xC5 (AF default 0, PF default 0 in this context setup)
    // Note: Exec_LAHF force sets bit 1 to 1.
    // Let's just check round trip.
    // SAHF: Store AH into Flags
    ctx.EFLAGS.Value = 0;
    Exec_SAHF(&ctx);
    EXPECT_EQ(1u, ctx.EFLAGS.SF);
    EXPECT_EQ(1u, ctx.EFLAGS.ZF);
    EXPECT_EQ(1u, ctx.EFLAGS.CF);
}
TEST(CpuSystemTests, ExecClcStcCmcUpdateCarry) {
    CPU_Context ctx{};
    DecodeContext d_ctx = MakeDecodeContext();
    // STC (F9)
    d_ctx.opcode = 0xF9;
    ExecuteInstruction(&ctx, &d_ctx);
    EXPECT_EQ(1u, ctx.EFLAGS.CF);
    // CMC (F5) -> Flip 1 to 0
    d_ctx.opcode = 0xF5;
    ExecuteInstruction(&ctx, &d_ctx);
    EXPECT_EQ(0u, ctx.EFLAGS.CF);
    // CLC (F8)
    ctx.EFLAGS.CF = 1;
    d_ctx.opcode = 0xF8;
    ExecuteInstruction(&ctx, &d_ctx);
    EXPECT_EQ(0u, ctx.EFLAGS.CF);
}
TEST(CpuSystemTests, ExecInt3HaltsCpu) {
    CPU_Context ctx{};
    DecodeContext d_ctx = MakeDecodeContext();
    d_ctx.opcode = 0xCC; // INT 3
    Exec_INT(&ctx, &d_ctx);
    EXPECT_TRUE(ctx.Halted);
}