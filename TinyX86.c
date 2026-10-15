// x86-emulator_v1.0.cpp : 此文件包含 "main" 函数。程序执行将在此处开始并结束。
//

#include "TinyX86.h"
#include <stdio.h> // 用于printf调试
#include <stdlib.h>
#include <math.h> // 需要 math.h 支持 double 运算

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

//tang
int OffsetByte[] =
{
	0 + 4 * 0,
	0 + 4 * 1,
	0 + 4 * 2,
	0 + 4 * 3,

	1 + 4 * 0,
	1 + 4 * 1,
	1 + 4 * 2,
	1 + 4 * 3
};

int runcpu(CPU_Context* p, int step)
{
	DecodeContext d_ctx;
	int instr_len = 0;
	for (int i = 0; i < step; i++) {
		// 0.【新增】检查是否停机
		if(p->Halted) {
			printf("[System] CPU Halted.\n");
			break;
		}
		//tang
		//if ((0xF8 & *(uint8_t*)p->EIP) == 0xb0)
		//{
		//	instr_len = 2;
		//	((uint8_t*)&p->EAX)[OffsetByte[7 & *(uint8_t*)p->EIP]] = *(uint8_t*)(1 + p->EIP);

		//}

		//if ((0xF8 & *(uint8_t*)p->EIP) == 0xb8)
		//{
		//	instr_len = 5;
		//	((uint32_t*)&p->EAX)[7&*(uint8_t*)p->EIP] = *(uint32_t*)(1 + p->EIP);
		//}


		// 1. 取指与解码 (Fetch & Decode)
		// 直接读取宿主内存地址 p->EIP
		instr_len = ParseInstuction((uint8_t*)p->EIP, p->EIP, &d_ctx);
		FormatInstruction((uint8_t*)p->EIP, &d_ctx);//可选，显示汇编指令，会消耗额外的cpu性能，适合调试时使用
		if (instr_len <= 0) {
			printf("Error: Decoding failed at EIP: 0x%08X\n", p->EIP);
			return -1;
		}
		// 2. 执行指令 (Execute)
		// 传递 instr_len 给执行函数，因为 JMP 需要基于 (EIP + len) 计算跳转目标
		bool jumped = ExecuteInstruction(p, &d_ctx);
		// 3. 更新 EIP 指向下一条指令
		// 仅当没有发生跳转时，才推进 EIP
		if (!jumped) p->EIP += instr_len;
		// 调试打印 (可选，用于观察 Sprint 1 效果)
		//printf("[Trace] EIP=0x%08X |  %s | Len: %d\n", p->EIP, d_ctx.asm_str, instr_len);

	}
	return 0;
}


// 执行分发器
static bool ExecuteInstruction(CPU_Context* ctx, DecodeContext* d_ctx) {
	// 默认不跳转
	bool jumped = false;

	// ========================================================
		// 分支 A: 处理双字节指令 (0x0F B6 ...)
		// ========================================================
	if (d_ctx->is_two_byte_opcode) {
		switch (d_ctx->opcode) {
			// --- CMOVcc (0x40 - 0x4F) ---
			case 0x40: case 0x41: case 0x42: case 0x43:
			case 0x44: case 0x45: case 0x46: case 0x47:
			case 0x48: case 0x49: case 0x4A: case 0x4B:
			case 0x4C: case 0x4D: case 0x4E: case 0x4F:
				Exec_CMOVcc(ctx, d_ctx);break;

			//  Long Conditional Jumps (0x80 - 0x8F)
			// Jcc Jz (16/32 bit offset)
			case 0x80: case 0x81: case 0x82: case 0x83:
			case 0x84: case 0x85: case 0x86: case 0x87:
			case 0x88: case 0x89: case 0x8A: case 0x8B:
			case 0x8C: case 0x8D: case 0x8E: case 0x8F:// 直接复用 Exec_Branch！
				jumped = Exec_Branch(ctx, d_ctx, true);// 这里的 opcode 低4位完美对应 CheckCondition 的条件码
				break;// d_ctx->imm 已经是 16/32 位偏移

			// SETcc (0x90 - 0x9F) - 之前实现的
			case 0x90: case 0x91: case 0x92: case 0x93:
			case 0x94: case 0x95: case 0x96: case 0x97:
			case 0x98: case 0x99: case 0x9A: case 0x9B:
			case 0x9C: case 0x9D: case 0x9E: case 0x9F:
				Exec_SETcc(ctx, d_ctx);break;

				// --- FS/GS PUSH/POP (0x0F A0 - 0x0F A9) ---
						// 这里的 opcode 来自 two_uint8_t_opcode_table
			case 0xA0: case 0xA8: // PUSH FS, PUSH GS
				Exec_PUSH(ctx, d_ctx);break;
			case 0xA1: case 0xA9: // POP FS, POP GS
				Exec_POP(ctx, d_ctx);break;

		
			// --- IMUL (双操作数 Gv, Ev) ---
			case 0xAF:
				Exec_IMUL_2_Op(ctx, d_ctx);break;
			// --- MOVZX (0xB6, 0xB7) ---
			case 0xB6: Exec_MOVZX(ctx, d_ctx, 8);  break; // uint8_t -> Duint16_t
			case 0xB7: Exec_MOVZX(ctx, d_ctx, 16); break; // uint16_t -> Duint16_t
			// --- MOVSX (0xBE, 0xBF) ---
			case 0xBE: Exec_MOVSX(ctx, d_ctx, 8);  break; // uint8_t -> Duint16_t
			case 0xBF: Exec_MOVSX(ctx, d_ctx, 16); break; // uint16_t -> Duint16_t

			default:
				printf("[CPU Error] Unimplemented 2-uint8_t opcode: 0x0F %02X\n", d_ctx->opcode);
		}
		return jumped;// 双字节指令大多不涉及跳转 (除了长跳转 0x8x)
	}

	// ========================================================
	// 分支 B: 处理单字节指令
	// ========================================================
	switch (d_ctx->opcode) {
		

		// ==========================================
		// Group A: 数据传送 (Data Transfer)
		// ==========================================
		// 
			// MOV r/m, r (0x88 - 0x8B)
		case 0x88: case 0x89: case 0x8A: case 0x8B:
		case 0x8C:          // 新增: MOV r/m, seg
		case 0x8E:          // 新增: MOV seg, r/m
		case 0xB0: case 0xB1: case 0xB2: case 0xB3:
		case 0xB4: case 0xB5: case 0xB6: case 0xB7:
		case 0xB8: case 0xB9: case 0xBA: case 0xBB:
		case 0xBC: case 0xBD: case 0xBE: case 0xBF:
		case 0xC6: case 0xC7:
			Exec_MOV_Generic(ctx, d_ctx); break;

		case 0xA0: // MOV AL, moffs8
			// 指令格式：A0 [offset32]
			// 注意：这里的 d_ctx->imm 应该是解码阶段读取到的 4 字节地址偏移
			ctx->GPR[0].I8.L = (uint8_t)MemRead(d_ctx->imm, 1);
			break;

		case 0xA1: // MOV EAX, moffs16/32
		{
			int size = (d_ctx->pfx_op_size == 0x66) ? 2 : 4;
			if (size == 2) ctx->GPR[0].I16 = (uint16_t)MemRead(d_ctx->imm, 2);
			else ctx->GPR[0].I32 = MemRead(d_ctx->imm, 4);
		}
		break;
		case 0xA2: // MOV moffs8, AL
			MemWrite(d_ctx->imm, ctx->GPR[0].I8.L, 1);
			break;
		case 0xA3: // MOV moffs16/32, EAX
		{
			int size = (d_ctx->pfx_op_size == 0x66) ? 2 : 4;
			if (size == 2) MemWrite(d_ctx->imm, ctx->GPR[0].I16, 2);
			else MemWrite(d_ctx->imm, ctx->GPR[0].I32, 4);
		}
		break;

		case 0x8D: Exec_LEA(ctx, d_ctx); break; // LEA 指令
		case 0x90: break; // NOP
		case 0x86:case 0x87: // XCHG Eb, Gb / Ev, Gv
		case 0x91:case 0x92: case 0x93: case 0x94: case 0x95:case 0x96:case 0x97: // XCHG EAX, reg
			Exec_XCHG(ctx, d_ctx); break;

			// --- 符号扩展 (0x98, 0x99) ---
		case 0x98: // CBW / CWDE
		case 0x99: // CWD / CDQ
			Exec_SignExtend(ctx, d_ctx); break;

		// ==========================================
		// Group B: 栈操作 (Stack)
		// ==========================================
		// 
		// --- PUSH / POP ---
		case 0x50: case 0x51: case 0x52: case 0x53: case 0x54: case 0x55: case 0x56: case 0x57: // PUSH r32
		case 0x68: case 0x6A: // PUSH imm
		case 0x06: case 0x0E: case 0x16: case 0x1E: // PUSH Seg
			Exec_PUSH(ctx, d_ctx);break;
		case 0x58: case 0x59: case 0x5A: case 0x5B: case 0x5C: case 0x5D: case 0x5E: case 0x5F: // POP r32
		case 0x07: case 0x17: case 0x1F: // POP Seg
			Exec_POP(ctx, d_ctx);break;
		case 0x60:
			Exec_PUSHA(ctx, d_ctx); break;
		case 0x61:
			Exec_POPA(ctx, d_ctx); break;
		case 0x9C:
			Exec_PUSHF(ctx); break; // PUSHF
		case 0x9D:
			Exec_POPF(ctx); break; // POPF
		case 0xC8:
			Exec_ENTER(ctx, d_ctx); break; // ENTER
		case 0xC9:
			Exec_LEAVE(ctx); break; // LEAVE

		// ==========================================
		// Group C: 算术运算 (ALU)
		// ==========================================
		// 
		// ADD, OR, ADC, SBB, AND, SUB, XOR, CMP (0x00 - 0x3D
			// 标准 ADD (Gv, Ev 等)
		case 0x00: case 0x01: case 0x02: case 0x03: case 0x04: case 0x05:
			Exec_ALU_Generic(ctx, d_ctx, ALU_ADD, false); break;
			//标准 OR
		case 0x08: case 0x09: case 0x0A: case 0x0B: case 0x0C: case 0x0D:
			Exec_ALU_Generic(ctx, d_ctx, ALU_OR, false); break;
			//标准ADC
		case 0x10: case 0x11: case 0x12: case 0x13: case 0x14: case 0x15:
			Exec_ALU_Generic(ctx, d_ctx, ALU_ADC, false); break;
			//标准SBB
		case 0x18: case 0x19: case 0x1A: case 0x1B: case 0x1C: case 0x1D:
			Exec_ALU_Generic(ctx, d_ctx, ALU_SBB, false); break;
			// 标准 AND
		case 0x20: case 0x21: case 0x22: case 0x23: case 0x24: case 0x25:
			Exec_ALU_Generic(ctx, d_ctx, ALU_AND, false); break;
			// 标准 SUB
		case 0x28: case 0x29: case 0x2A: case 0x2B: case 0x2C: case 0x2D:
			Exec_ALU_Generic(ctx, d_ctx, ALU_SUB, false); break;
		case 0x30: case 0x31: case 0x32: case 0x33: case 0x34: case 0x35: 
			Exec_ALU_Generic(ctx, d_ctx, ALU_XOR, false); break;
			// 标准 CMP
		case 0x38: case 0x39: case 0x3A: case 0x3B: case 0x3C: case 0x3D:
			Exec_ALU_Generic(ctx, d_ctx, ALU_CMP, true); break;
			// Group 1: 0x80-0x83 (Immediate ALU):ADD, OR, ADC, SBB, AND, SUB, XOR, CMP
		case 0x80: case 0x81: case 0x83:
			Exec_Group1(ctx, d_ctx); break;
			// TEST (0x84, 0x85, A8, A9)
		case 0x84: case 0x85: case 0xA8: case 0xA9:
			Exec_ALU_Generic(ctx, d_ctx, ALU_AND, true); break;
			// --- INC / DEC ---
		case 0x40: case 0x41: case 0x42: case 0x43: case 0x44: case 0x45: case 0x46: case 0x47:
			Exec_INC(ctx, d_ctx); break;
		case 0x48: case 0x49: case 0x4A: case 0x4B: case 0x4C: case 0x4D: case 0x4E: case 0x4F:
			Exec_DEC(ctx, d_ctx);break;
			// Group 3 (F6/F7): NEG, NOT, MUL, IMUL, DIV, IDIV
		case 0xF6: case 0xF7:
			Exec_Group3(ctx, d_ctx); break;
		case 0xFE:
			Exec_Group4(ctx, d_ctx); break; // Group 4 (INC/DEC Eb)

		// ==========================================
		// Group D: 移位操作 (Shift/Rotate)
		// ==========================================
		// 
		// Group 2 (C0, C1, D0, D1, D2, D3)
		case 0xC0: case 0xC1: // Grp2 Eb/Ev, Ib
		case 0xD0: case 0xD1: // Grp2 Eb/Ev, 1
		case 0xD2: case 0xD3: // Grp2 Eb/Ev, CL
			Exec_Group2(ctx, d_ctx); break;

		// ==========================================
		// Group E: 串操作 (String)
		// ==========================================
		// 
		case 0xA4: case 0xA5: // MOVS
		case 0xA6: case 0xA7: // CMPS
		case 0xAA: case 0xAB: // STOS
		case 0xAC: case 0xAD: // LODS	
		case 0xAE: case 0xAF: // SCANS
			if (d_ctx->pfx_rep || d_ctx->pfx_repne) { return Exec_REP_StringOp(ctx, d_ctx);}
			else{Exec_StringOp(ctx, d_ctx); return false; }

		// ==========================================
		// Group F: 控制流 (Control Flow)
		// ==========================================
		//	
		// 	
			// --- Jcc (条件跳转) ---
			// 0x70 - 0x7F (Short Jumps)
		case 0x70: case 0x71: case 0x72: case 0x73:
		case 0x74: case 0x75: case 0x76: case 0x77:
		case 0x78: case 0x79: case 0x7A: case 0x7B:
		case 0x7C: case 0x7D: case 0x7E: case 0x7F:
			jumped = Exec_Branch(ctx, d_ctx, true);
			break;
		case 0xE9: // JMP Jz (Near Jump 32)
		case 0xEB: // JMP Jb (Short Jump 8)
			jumped = Exec_Branch(ctx, d_ctx, false);
			break;
			// --- JCXZ / JECXZ (0xE3) ---
		case 0xE3:
			if (ctx->ECX.I32 == 0) {
				int8_t offset = (int8_t)d_ctx->imm;
				ctx->EIP += d_ctx->instr_len + offset;
				jumped = true;
			}
			break;
		case 0xE8: // CALL Jz
			jumped = Exec_CALL(ctx, d_ctx);
			break;
		case 0xC3: // RETN
		case 0xC2: // RETN Iw
			jumped = Exec_RET(ctx, d_ctx);
			break;
		case 0xFF:
			jumped = Exec_Group5(ctx, d_ctx); break; // Group 5 (CALL/JMP/PUSH Ev)
			// --- LOOP ---
		case 0xE0: case 0xE1: case 0xE2: // LOOPxx Jb
			jumped = Exec_LOOP(ctx, d_ctx);
			break;

		// ==========================================
		// Group G: 系统与 FPU (System & FPU)
		// ==========================================
		case 0xCC: // INT 3
		case 0xCD: // INT Ib
			Exec_INT(ctx, d_ctx); break;
			// FPU (D8 - DF)
		case 0xD8: case 0xD9: case 0xDA: case 0xDB:
		case 0xDC: case 0xDD: case 0xDE: case 0xDF:
			Exec_FPU(ctx, d_ctx);
			break;
			// --- SAHF (0x9E) LAHF(0x9F)---
		case 0x9E:
			Exec_SAHF(ctx); break;
		case 0x9F:
			Exec_LAHF(ctx); break;
		case 0xF5:
			ctx->EFLAGS.CF ^= 1; break;// CMC
		case 0xF8:
			ctx->EFLAGS.CF = 0; break;// CLC
		case 0xF9:
			ctx->EFLAGS.CF = 1; break;// STC
		case 0xFA:
			ctx->EFLAGS.IF = 0; break;//CLI
		case 0xFB:
			ctx->EFLAGS.IF = 1; break;//STI
		case 0xFC:
			ctx->EFLAGS.DF = 0; break;//CLD
		case 0xFD:
			ctx->EFLAGS.DF = 1; break;//STD
		case 0xF4: // HLT
			ctx->Halted = true; break;
		// ==========================================
		// Group I: 未实现
		// =========================================
		case 0x26: case 0x2E: case 0x36: case 0x3E: // 段超越前缀 (通常在解码层处理)
		case 0x62: // BOUND
		case 0x63: // ARPL
		case 0x64: case 0x65: // FS/GS Prefix
		case 0x66: case 0x67: // Size Override Prefix
		case 0x69: case 0x6B: // IMUL3 ( Immediate)
		case 0x6C: case 0x6D: case 0x6E: case 0x6F: // IN/OUT String
		case 0x9A: // CALL FAR
		case 0x9B: // WAIT/FWAIT
		case 0xC4: case 0xC5: // LES / LDS
		case 0xCA: case 0xCB: // RETF
		case 0xCE: // INTO
		case 0xCF: // IRET
		case 0xD4: // AAM
		case 0xD5: // AAD
		case 0xD6: // SALC (Undocumented)
		case 0xD7: // XLAT
		case 0xE4: case 0xE5: case 0xE6: case 0xE7: // IN / OUT (Fixed Port)
		case 0xEA: // JMP FAR
		case 0xEC: case 0xED: case 0xEE: case 0xEF: // IN / OUT (DX Port)
		case 0xF0: // LOCK Prefix
		case 0xF2: case 0xF3: // REPNE / REP Prefix
			// 此处记录所有 0x00-0xFF 范围内尚未在上方 Switch 分支中处理的指令
		default:
			printf("[CPU] Unimplemented Opcode: 0x%02X\n", d_ctx->opcode);
			ctx->Halted = true; // 遇到非法指令直接停机
			break;
	}
	return jumped;
}
// 辅助函数：根据索引和大小读取通用寄存器
// ctx: CPU上下文
// reg_index: 寄存器索引 (0-7)
// size: 大小 (8, 16, 32)
static uint32_t ReadGPR(CPU_Context* ctx, int reg_index, int size) {
	if(reg_index<0 || reg_index>=8) return 0;

	if (size == 32) {
		return ctx->GPR[reg_index].I32;
	}else if(size == 16) {
		return ctx->GPR[reg_index].I16;
	}else if(size == 8) {
		if( reg_index < 4) {
			return ctx->GPR[reg_index].I8.L;
		}else {
			return ctx->GPR[reg_index - 4].I8.H;
		}
	}
	return 0;
}
// 辅助函数：写入通用寄存器
//0执行报错，1成功
static bool WriteGPR(CPU_Context* ctx, int reg_index, int size, uint32_t value) {
	if (reg_index < 0 || reg_index >= 8) return 0;

	if (size == 32) {
		ctx->GPR[reg_index].I32 = value;
	}
	else if (size == 16) {
		ctx->GPR[reg_index].I16 = (uint16_t)value;
	}
	else if (size == 8) {
		if (reg_index < 4) {
			ctx->GPR[reg_index].I8.L = (uint8_t)value;
		}
		else {
			ctx->GPR[reg_index - 4].I8.H = (uint8_t)value;
		}
	}
	return 1;
}

static uint32_t GetEffectiveAddress(CPU_Context* ctx, DecodeContext* d_ctx) {
	uint32_t base = 0;
	uint32_t index = 0;
	// 1. 处理 SIB 逻辑
	if (d_ctx->has_sib) {
		if (d_ctx->base != 5 && d_ctx->mod != 00) {
			base = ReadGPR(ctx, d_ctx->base, 32);
		}
		if (d_ctx->index != 4) {
			index = ReadGPR(ctx, d_ctx->index, 32);
		}
		//否则保持默认为0
	}// 2. 无 SIB 逻辑
	else {
		if (d_ctx->mod == 0 && d_ctx->rm == 5) {
			base = 0; // disp32 直接作为地址
		}
		else {
			base = ReadGPR(ctx, d_ctx->rm, 32);
		}
	}
	return base + index * (1 << d_ctx->scale) + d_ctx->disp;
}

//参照FormatOperand对应修改
static uint32_t GetOperandValue(CPU_Context* ctx, DecodeContext* d_ctx, int op_idx) {
	OperandType type;
	// 根据索引获取类型
	if(op_idx == 0) type = d_ctx->entry.op1;
	else if (op_idx == 1) type = d_ctx->entry.op2;
	else type = d_ctx->entry.op3;

	int op_size = (d_ctx->pfx_op_size == 0x66) ? 16 : 32;
	switch (type) {
		// --- 寄存器 ---
		case Gb: return ReadGPR(ctx, d_ctx->reg, 8);
		case Gw: return ReadGPR(ctx, d_ctx->reg, 16);
		case Gv: return ReadGPR(ctx, d_ctx->reg, op_size);
		// 补充对 OP_AL ... OP_rDI 的支持
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

		// --- 段寄存器 (Sw: from ModRM.reg) ---
		case Sw:
			// ctx->seg 数组顺序: ES=0, CS=1, SS=2, DS=3, FS=4, GS=5
			// d_ctx->reg 刚好对应这个索引 (0-5)
			if (d_ctx->reg < 6) return ctx->seg[d_ctx->reg];
			return 0;

		// --- 硬编码段寄存器 (OP_ES ... OP_GS) ---
		case OP_ES: return ctx->ES;
		case OP_CS: return ctx->CS;
		case OP_SS: return ctx->SS;
		case OP_DS: return ctx->DS;
		case OP_FS: return ctx->FS;
		case OP_GS: return ctx->GS;
		

		// --- 内存/寄存器 (ModR/M) ---
		case Eb:
		case Ew:
		case Ev: {
			if(d_ctx->mod == 3) {
				// 寄存器直接访问
				int size_E = (type == Eb) ? 8 : ((type == Ew) ? 16 : op_size);
				return ReadGPR(ctx, d_ctx->rm, size_E);
			}
			else {
				uint32_t addr = GetEffectiveAddress(ctx, d_ctx);
				int uint8_t_len = (type == Eb) ? 1 : ((type == Ew) ? 2 : (op_size / 8));
				return MemRead(addr, uint8_t_len); //读取内存并返回
			}
		}
		// --- 立即数 ---
		
			// 立即数已经解析在 imm / imm2 字段中
			// 假设 op1/op2 使用 imm，op3 使用 imm2 (极少见)
			// 简单起见，这里假设只用 imm。如果指令有两个立即数(如 ENTER)，需判断
		case Ib: case Iw: case Iv: case Iz:
			return (uint32_t)d_ctx->imm;

		default:
			return 0; // 暂不支持或其他类型
	}
}

//参照FormatOperand对应修改
// 写入操作数的值
static void SetOperandValue(CPU_Context* ctx, DecodeContext* d_ctx, int op_idx, uint32_t value) {
	OperandType type = (op_idx == 0) ? d_ctx->entry.op1 : d_ctx->entry.op2;
	int op_size = (d_ctx->pfx_op_size == 0x66) ? 16 : 32;
	switch (type) {
		// --- 寄存器 ---
		case Gb: WriteGPR(ctx, d_ctx->reg, 8, value); break;
		case Gw: WriteGPR(ctx, d_ctx->reg, 16, value); break;
		case Gv: WriteGPR(ctx, d_ctx->reg, op_size, value); break;
		// --- 内存/寄存器 ---
		case Eb:
		case Ew:
		case Ev: {
			if (d_ctx->mod == 3) {
				int size_E = (type == Eb) ? 8 : ((type == Ew) ? 16 : op_size);
				WriteGPR(ctx, d_ctx->rm, size_E, value);
			}
			else {
				uint32_t addr = GetEffectiveAddress(ctx, d_ctx);
				int uint8_t_len = (type == Eb) ? 1 : ((type == Ew) ? 2 : (op_size / 8));
				MemWrite(addr, value, uint8_t_len); // 调用提取出来的函数
			}
			break;
		}
		// 补充对 OP_AL ... OP_rDI 的支持
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
			// --- 段寄存器 (Sw) ---
		case Sw:
			if (d_ctx->reg < 6) ctx->seg[d_ctx->reg] = (uint16_t)value;
			break;

			// --- 硬编码段寄存器 ---
			// 注意：CS 通常不能直接 MOV 修改，但 PUSH/POP 可能涉及到，模拟器为了简单先允许写
		case OP_ES: ctx->ES = (uint16_t)value; break;
		case OP_CS: ctx->CS = (uint16_t)value; break; // 警告：真实CPU改CS通常需要JMP FAR
		case OP_SS: ctx->SS = (uint16_t)value; break;
		case OP_DS: ctx->DS = (uint16_t)value; break;
		case OP_FS: ctx->FS = (uint16_t)value; break;
		case OP_GS: ctx->GS = (uint16_t)value; break;
	}
}

// 处理 MOV r, i (0xB0 - 0xBF)
static void Exec_MOV_Generic(CPU_Context* ctx, DecodeContext* d_ctx) {
	// 1. 读取源操作数 (OP2: Immediate)
	uint32_t src = GetOperandValue(ctx, d_ctx, 1);
	// 2. 写入目的操作数 (OP1: Register)
	SetOperandValue(ctx, d_ctx, 0, src);
}

static int CalcPF(uint8_t res) {
	int count = 0;
	for (int i = 0; i < 8; i++) {
		if(res & (1 << i)) count++;
	}
	return (count % 2 == 0) ? 1 : 0;
}

static void UpdateEFLAGS(CPU_Context* ctx, uint32_t res, uint32_t dest, uint32_t src, int size, ALU_Op op) {
	// 1. 提取最高位 (Sign Bit) 掩码
	uint32_t sign_mask = (size == 8) ? 0x80 : (size == 16) ? 0x8000 : 0x80000000;
	// 截断结果以符合操作数大小
	uint32_t truncated_res = res;
	if (size == 8) truncated_res &= 0xFF;
	else if (size == 16) truncated_res &= 0xFFFF;
	// 2. 通用标志位更新
	// ZF (Zero Flag): 结果为0
	ctx->EFLAGS.ZF = (truncated_res == 0) ? 1 : 0;
	// SF (Sign Flag): 结果符号位,最高位为1
	ctx->EFLAGS.SF = (truncated_res & sign_mask) ? 1 : 0;
	// PF (Parity Flag): 低8位偶校验
	ctx->EFLAGS.PF = CalcPF((uint8_t)truncated_res);
	// --- 算术特定标志位 (CF, OF, AF) ---
	// 逻辑运算 (AND/OR/XOR) 清除 CF/OF
	if (op == ALU_AND || op == ALU_OR || op == ALU_XOR) {
		ctx->EFLAGS.CF =0;
		ctx->EFLAGS.OF = 0;
		ctx->EFLAGS.AF = 0; // 未定义，通常清零
	}
	else if(op == ALU_ADD){
		// ADD: res = dest + src
		// CF (Carry): 无符号溢出 (结果小于任一操作数)
		// 注意：需比较截断前的 res 或者判断截断后的回绕
		if (size == 32) ctx->EFLAGS.CF = (res < dest);
		else ctx->EFLAGS.CF = ((res & (sign_mask << 1)) != 0); // 检查更高一位进位//bug:修复了!= 与 &优先级的问题
		// OF (Overflow): 有符号溢出 (正+正=负 或 负+负=正)
		// 公式：(dest ^ res) & (src ^ res) & sign_mask
		ctx->EFLAGS.OF = ((dest ^ truncated_res) & (src ^ truncated_res) & sign_mask)!= 0;
		// AF (Adjust): Bit 3 借位
		//加数，被加数，结果异或等于该位进位制的数学原理
		ctx->EFLAGS.AF = (dest ^ src ^ truncated_res) & 0x10 != 0;	
	}
	else if (op == ALU_SUB) {

		// SUB: res = dest - src (CMP 也是这类)
		// CF (Borrow): 无符号借位 (dest < src)
		ctx->EFLAGS.CF = (dest < src);
		// OF (Overflow): 正-负=负 或 负-正=正
		// 公式：(dest ^ src) & (dest ^ res) & sign_mask
		ctx->EFLAGS.OF = ((dest ^ src) & (dest ^ res) & sign_mask) != 0;
		// AF (Adjust): Bit 3 借位
		ctx->EFLAGS.AF = ((dest ^ res ^ truncated_res) & 0x10) != 0;
	}
	else if(op == ALU_INC){
		// INC 不更新 CF
		ctx->EFLAGS.OF = ((dest ^ truncated_res) & (src ^ truncated_res) & sign_mask) != 0;
		ctx->EFLAGS.AF = (dest ^ src ^ truncated_res) & 0x10 != 0;
	}else if(op == ALU_DEC){
		// DEC 不更新 CF
		ctx->EFLAGS.OF = ((dest ^ src) & (dest ^ res) & sign_mask) != 0;
		ctx->EFLAGS.AF = ((dest ^ res ^ truncated_res) & 0x10) != 0;
	}

}

// 辅助函数：获取操作数的实际位宽 (8, 16, 32)
// 用于确保 ALU 标志位计算正确
static int GetOperandBitSize(CPU_Context* ctx, DecodeContext* d_ctx, OperandType type) {
	// 1. 获取前缀决定的默认大小 (16 or 32)
	int def_op_size = (d_ctx->pfx_op_size == 0x66) ? 16 : 32;

	switch (type) {
		// --- 8 位操作数 ---
		case Eb: case Gb:
		case OP_AL: case OP_CL: case OP_DL: case OP_BL:
		case OP_AH: case OP_CH: case OP_DH: case OP_BH:
			return 8;

			// --- 16 位操作数 ---
		case Ew: case Gw:
		case OP_DX: // 用于 IN/OUT
		return 16;

			// --- 依赖前缀的操作数 (16/32) ---
		case Ev: case Gv: case Gz:
		case OP_rAX: case OP_rCX: case OP_rDX: case OP_rBX:
		case OP_rSP: case OP_rBP: case OP_rSI: case OP_rDI:
			return def_op_size;

			// --- 立即数通常跟随目的操作数大小，这里作为 fallback ---
		case Ib: return 8;
		case Iw: return 16;
		case Iv: case Iz: return def_op_size;

		default:
			return def_op_size; // 默认
	}
}

static void Exec_ALU_Generic(CPU_Context* ctx, DecodeContext* d_ctx, ALU_Op op, bool is_compare) {
	// 1. 确定运算位宽 (非常关键！)
	// 我们根据 Op1 (目的操作数) 的类型来决定运算是 8位 还是 32位
	// 之前漏掉了 OP_AL 等类型，现在用 GetOperandBitSize 修复
	int size_d = GetOperandBitSize(ctx, d_ctx, d_ctx->entry.op1);
	int size_s = GetOperandBitSize(ctx, d_ctx, d_ctx->entry.op2);
	// 2. 获取操作数
	uint32_t dest = GetOperandValue(ctx, d_ctx, 0); // Op1 (Dest)
	uint32_t src = GetOperandValue(ctx, d_ctx, 1); // Op2 (Src)
	// 对于 8 位和 16 位运算，必须对读取到的值进行截断清理，
	// 防止高位垃圾数据干扰标志位计算 (特别是立即数符号扩展后的高位)

	uint32_t mask_d = (size_d == 8) ? 0xFF : (size_d == 16 ? 0xFFFF : 0xFFFFFFFF);
	uint32_t mask_s = (size_s == 8) ? 0xFF : (size_s == 16 ? 0xFFFF : 0xFFFFFFFF);
	dest &= mask_d;
	src &= mask_s;
	uint32_t res = 0;

	uint32_t cf_in = ctx->EFLAGS.CF; // 获取当前的进位
	// 3. 执行计算
	switch (op) {
		case ALU_ADD: res = dest + src; break;
		case ALU_OR : res = dest | src; break;
		case ALU_ADC: res = dest + src + cf_in; break;
		case ALU_SBB: res = dest - src - cf_in; break;
		case ALU_AND: res = dest & src; break;
		case ALU_CMP: case ALU_SUB:res = dest - src; break;
		case ALU_XOR: res = dest ^ src; break;
		case ALU_INC: res = dest + 1; break;
		case ALU_DEC: res = dest - 1; break;
	}
	// 3.1.在更新标志位前，先拿到截断后的结果
	uint32_t truncated_res = res & mask_d;
	// 4. 更新标志位 (传入正确的 size)
	UpdateEFLAGS(ctx, truncated_res, dest, src, size_d, op);
	// 5. 写回结果 (如果是 CMP/TEST 则跳过)
	if (!is_compare) {
		SetOperandValue(ctx, d_ctx, 0, truncated_res);
	}
}

// 处理 Group 1 指令 (0x80-0x83: ADD/OR/ADC/SBB/AND/SUB/XOR/CMP immediate)
static void Exec_Group1(CPU_Context* ctx, DecodeContext* d_ctx) {
	// Group 1 的指令由 modrm.reg 字段决定操作类型
	switch (d_ctx->reg) {
		case 0: Exec_ALU_Generic(ctx, d_ctx, ALU_ADD, false); break;
		case 1: Exec_ALU_Generic(ctx, d_ctx, ALU_OR, false); break;
		case 2: Exec_ALU_Generic(ctx, d_ctx, ALU_ADC, false); break;
		case 3: Exec_ALU_Generic(ctx, d_ctx, ALU_SBB, false); break;
		case 4: Exec_ALU_Generic(ctx, d_ctx, ALU_ADD, false); break;
		case 5: Exec_ALU_Generic(ctx, d_ctx, ALU_SUB, false); break;
		case 6: Exec_ALU_Generic(ctx, d_ctx, ALU_XOR, false); break;
		case 7: Exec_ALU_Generic(ctx, d_ctx, ALU_CMP, true); break;
	}
}

// PUSH r32 / imm32 / imm8
static void Exec_PUSH(CPU_Context* ctx, DecodeContext* d_ctx) {
	// 1. 读取要压入的值 (Op1)
	// 注意处理 PUSH ESP 的特殊情况：Intel 规定压入的是执行指令前的 ESP 值
	// 我们的 GetOperandValue 会读取当前的 ESP，这没问题。
	uint32_t value = GetOperandValue(ctx, d_ctx, 0);
	// 获取操作数大小 (通常是 32位，即 4字节)
	int size = (d_ctx->pfx_op_size == 0x66) ? 2 : 4;
	// 2. 调整 ESP (向下增长)
	ctx->ESP.I32 -= size;
	// 3. 写入内存
	// Host-Passthrough: 直接写入 ctx->ESP 指向的地址
	if (size == 4) {
		*(uint32_t*)ctx->ESP.I32 = value;
	}
	else {
		*(uint16_t*)ctx->ESP.I32 = (uint16_t)value;
	}
}
// POP r32
static void Exec_POP(CPU_Context* ctx, DecodeContext* d_ctx) {
	int size = (d_ctx->pfx_op_size == 0x66) ? 2 : 4;
	// 1. 从栈顶读取数据
	uint32_t val = 0;
	if (size == 4) {
		val = *(uint32_t*)ctx->ESP.I32;
	}
	else {
		val = *(uint16_t*)ctx->ESP.I32;
	}
	// 2. 调整 ESP (向上收缩)
	// 注意 POP ESP 的特殊情况：它会先读栈顶数据，然后 ESP+4，最后把读出的数据覆盖回 ESP。
	// 结果就是 ESP 等于栈顶原来的那个值（ESP+4 被覆盖了）。
	ctx->ESP.I32 += size;
	// 3. 写入目的操作数 (Op1)
	SetOperandValue(ctx, d_ctx, 0, val);
}

static void Exec_PUSHA(CPU_Context* ctx, DecodeContext* d_ctx) {
	int size = (d_ctx->pfx_op_size == 0x66) ? 2 : 4;
	uint32_t sp_val = (size == 2) ? (uint32_t)ctx->ESP.I16 : ctx->ESP.I32;

	if (size == 2) {
		for (int i = 0; i < 8; i++) {
			ctx->ESP.I32 -= 2;
			MemWrite(ctx->ESP.I32, ctx->GPR[i].I16, 2);
		}
	}
	else {
		for (int i = 0; i < 8; i++) {
			ctx->ESP.I32 -= 4;
			MemWrite(ctx->ESP.I32, ctx->GPR[i].I32, 4);
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
	}
	else {
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

// 检查条件跳转是否成立 (Jcc)
// condition_code: 输入指令码，取指令 Opcode 的低 4 位 (0x70-0x7F 或 0x80-0x8F 的低位)，还可以处理类似的逻辑，比如CMOV,SETcc等
static bool CheckCondition(CPU_Context* ctx, uint8_t condition_code) {
	switch (condition_code & 0xF) {
		case 0x0: return ctx->EFLAGS.OF == 1;          // JO
		case 0x1: return ctx->EFLAGS.OF == 0;          // JNO
		case 0x2: return ctx->EFLAGS.CF == 1;          // JB, JNAE (无符号 <)
		case 0x3: return ctx->EFLAGS.CF == 0;          // JNB, JAE (无符号 >=)
		case 0x4: return ctx->EFLAGS.ZF == 1;          // JZ, JE (等于)
		case 0x5: return ctx->EFLAGS.ZF == 0;          // JNZ, JNE (不等于)
		case 0x6: return (ctx->EFLAGS.CF | ctx->EFLAGS.ZF) == 1; // JBE, JNA (无符号 <=)
		case 0x7: return (ctx->EFLAGS.CF | ctx->EFLAGS.ZF) == 0; // JA, JNBE (无符号 >)
		case 0x8: return ctx->EFLAGS.SF == 1;          // JS
		case 0x9: return ctx->EFLAGS.SF == 0;          // JNS
		case 0xA: return ctx->EFLAGS.PF == 1;          // JP, JPE
		case 0xB: return ctx->EFLAGS.PF == 0;          // JNP, JPO
		case 0xC: return ctx->EFLAGS.SF != ctx->EFLAGS.OF;       // JL, JNGE (有符号 <)
		case 0xD: return ctx->EFLAGS.SF == ctx->EFLAGS.OF;       // JNL, JGE (有符号 >=)
		case 0xE: return ctx->EFLAGS.ZF == 1 || (ctx->EFLAGS.SF != ctx->EFLAGS.OF); // JLE, JNG (有符号 <=)
		case 0xF: return ctx->EFLAGS.ZF == 0 && (ctx->EFLAGS.SF == ctx->EFLAGS.OF); // JG, JNLE (有符号 >)
	}
	return false; // 默认不跳转
}
// 处理相对跳转 (JMP, Jcc)
// 返回 true 表示跳转发生
static bool Exec_Branch(CPU_Context* ctx, DecodeContext* d_ctx, bool is_conditional) {
	bool take_jump = true;
	if (is_conditional) {
		// 如果是条件跳转，先检查条件
		take_jump = CheckCondition(ctx, d_ctx->opcode);
	}
	if (take_jump) {
		// 计算目标地址
		// x86 相对跳转公式：Target = Current_EIP + Instruction_Length + Relative_Offset
		// d_ctx->imm 存储的是相对偏移 (Relative Offset)
		uint32_t offset = (uint32_t)d_ctx->imm;
		ctx->EIP += d_ctx->instr_len + offset; 
		return true; 
	}
	return false; // 未跳转
}

// 处理 CALL (相对调用)
static bool Exec_CALL(CPU_Context* ctx, DecodeContext* d_ctx) {
	// 1. 计算返回地址 (下一条指令)
	uint32_t ret_addr = ctx->EIP + d_ctx->instr_len;
	// 2. 压入返回地址 (相当于 PUSH ret_addr)
	ctx->ESP.I32 -= 4;
	*(uint32_t*)ctx->ESP.I32 = ret_addr;
	
	// 3. 执行跳转
	uint32_t offset = (uint32_t)d_ctx->imm;
	ctx->EIP += d_ctx->instr_len + offset;
	return true;
}

// 处理 RET (返回)
static bool Exec_RET(CPU_Context* ctx, DecodeContext* d_ctx) {
	// 1. 弹出返回地址 (相当于 POP EIP)
	uint32_t ret_addr = *(uint32_t*)ctx->ESP.I32;
	ctx->ESP.I32 += 4;
	// 2. 处理 RET n (带立即数的返回，用于平栈)
	if(d_ctx->opcode == 0xC2 || d_ctx->opcode == 0xCA) { //以后CA处理段寄存器时再实现
		ctx->ESP.I32 += d_ctx->imm;
	}
	// 3. 设置 EIP
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

static void Exec_INC(CPU_Context* ctx, DecodeContext* d_ctx){
	int reg = d_ctx->opcode - 0x40;
uint32_t val = ReadGPR(ctx,reg,32);
	uint32_t res = val + 1;
	UpdateEFLAGS(ctx, res, val, 1, 32, ALU_INC);
	WriteGPR(ctx, reg, 32, res);
}

// 辅助：仅更新逻辑标志位 (ZF, SF, PF)
static void UpdateLogicFlags(CPU_Context* ctx, uint32_t res, int size) {
	uint32_t truncated_res = res;
	uint32_t sign_mask = 0;

	if (size == 8) {truncated_res &= 0xFF; sign_mask = 0x80;}
	else if (size == 16) {truncated_res &= 0xFFFF; sign_mask = 0x8000;}
	else {sign_mask = 0x80000000;}
	// ZF
	ctx->EFLAGS.ZF = (truncated_res == 0) ? 1 : 0;
	ctx->EFLAGS.SF = (truncated_res & sign_mask) ? 1 : 0;
	ctx->EFLAGS.PF = CalcPF(truncated_res);
}
//处理Group2指令：SHL, SHR, SAR(移位) 和 ROL, ROR, RCL, RCR(循环移位)。
static void Exec_Group2(CPU_Context* ctx, DecodeContext* d_ctx) {
	// 1. 确定位宽
	int size = GetOperandBitSize(ctx, d_ctx, d_ctx->entry.op1);
	uint32_t val = GetOperandValue(ctx, d_ctx, 0);// Dest

	// 掩码用于截断高位垃圾
	uint32_t mask = (size == 32) ? 0xFFFFFFFF : ((1 << size) - 1);
	val &= mask;
	// MSB 掩码 (最高位)
	uint32_t msb_mask = 1 << (size - 1);
	// 2. 确定移位次数 (Count)
	// D0/D1/C0/C1 等 Opcode 已经在 disasm 阶段把立即数解析到了 imm，或者 CL 解析到了 op2
	// 但 Group 2 比较特殊，我们需要手动判断一下来源
	uint32_t count = 0;
	if (d_ctx->opcode == 0xC0 || d_ctx->opcode == 0xC1){count = (uint32_t)d_ctx->imm;}
	else if (d_ctx->opcode == 0xD0 || d_ctx->opcode == 0xD1) {count = 1;}// D0/D1 固定为 1
	else if (d_ctx->opcode == 0xD2 || d_ctx->opcode == 0xD3) {count = ReadGPR(ctx, 1, 8); }// CL
	// Intel 手册规定：移位次数只取低 5 位 (0-31)
	count &= 0x1F;
	if (count == 0) return; // 0 次移位不影响标志位
	// 3. 根据 ModRM.reg 区分指令并执行
	// 使用循环模拟，每次移 1 位，确保 CF/OF 正确
	uint32_t res = val;
	uint32_t original_msb = val & msb_mask; // 用于 SAR,原符符号位
	// reg 字段定义:
	// 0=ROL, 1=ROR, 2=RCL, 3=RCR, 4=SHL/SAL, 5=SHR, 7=SAR
	switch (d_ctx->reg) {
		case 0: { // ROL (循环左移)
			for (int i = 0; i < count; i++) {
				uint32_t bit = (res & msb_mask) ? 1 : 0;
				ctx->EFLAGS.CF = bit; // CF = 移出的位
				res <<= 1;
				res |= bit; // 移出的位补到最低位
			}
			// OF (count=1): MSB ^ CF
			if (count == 1) {
				ctx->EFLAGS.OF = ((res & msb_mask) ? 1 : 0) ^ ctx->EFLAGS.CF;
			}
			break;
		}

		case 1: { // ROR (循环右移)
			for (int i = 0; i < count; i++) {
				uint32_t bit = res & 1;
				res >>= 1;
				ctx->EFLAGS.CF = bit; // CF = 移出的位
				if (bit) res |= msb_mask; // 移出的位补到最高位
			}
			if (count == 1) {
				// 根据 Intel 手册：OF = MSB(result) ^ MSB-1(result)
				uint32_t msb = (res >> (size - 1)) & 1;
				uint32_t msb_minus_1 = (res >> (size - 2)) & 1;
				ctx->EFLAGS.OF = msb ^ msb_minus_1;
			}
			break;
		}
		
		case 2: { // RCL (带进位的循环左移)
			for (int i = 0; i < count; i++) {
				uint32_t bit = (res & msb_mask) ? 1 : 0;
				res <<= 1;
				// 将 CF 补到最低位
				if (ctx->EFLAGS.CF) {
					res |= 1;
				}
				ctx->EFLAGS.CF = bit; // CF = 移出的位
			}
			// OF (count=1): MSB ^ CF
			if (count == 1) {
				ctx->EFLAGS.OF = ((res & msb_mask) ? 1 : 0) ^ ctx->EFLAGS.CF;
			}
			break;
		}

		case 3: { // RCR (带进位的循环右移)
			for (int i = 0; i < count; i++) {
				uint32_t bit = res & 1;
				res >>= 1;
				// 将 CF 补到最高位
				if (ctx->EFLAGS.CF) {
					res |= msb_mask;
				}
				ctx->EFLAGS.CF = bit; // CF = 移出的位
			}
			// OF (count=1): MSB ^ MSB-1
			if (count == 1) {
				uint32_t msb = (res >> (size - 1)) & 1;
				uint32_t msb_minus_1 = (res >> (size - 2)) & 1;
				ctx->EFLAGS.OF = msb ^ msb_minus_1;
			}
			break;
		}
		

		case 4:{// SHL / SAL (逻辑左移)
			for (int i = 0; i < count; i++) {// CF = 移出的最高位
				ctx->EFLAGS.CF = (res & msb_mask) ? 1 : 0;
				res <<= 1;
			}
			// OF (仅 count=1 时定义): 最高位是否改变 (MSB ^ CF)
			if (count == 1) {
				ctx->EFLAGS.OF = ctx->EFLAGS.CF ^ ((res & msb_mask) ? 1 : 0);
			}
			break;
		}
		
		case 5: {
			for (int i = 0; i < count; i++) {// CF = 移出的最低位
				ctx->EFLAGS.CF = res & 1;
				res >>= 1;
			}
			// OF (仅 count=1 时定义): 原最高位 (SHR 总是把最高位清0，所以看原最高位是否为1)
			if (count == 1) {
				ctx->EFLAGS.OF = (original_msb) ? 1 : 0;
			}
			break;
		}

		case 7: { // SAR (算术右移)
			for (int i = 0; i < count; i++) {
				ctx->EFLAGS.CF = res & 1;
				res >>= 1;
				// 保持符号位
				if (original_msb) {
					res |= msb_mask;
				}
			}
			// OF (仅 count=1 时定义): SAR 不改变符号位，所以 OF 总是 0
			if (count == 1) {
				ctx->EFLAGS.OF = 0;
			}
			break;
		}
		default:
			printf("Unimplemented Group 2(S/R) op: %d\n", d_ctx->reg);
			return;

	}

	// 4. 写回结果
	// 截断结果以防万
	res &= mask;
	// 更新 SF, ZF, PF
	UpdateLogicFlags(ctx, res, size);
	// 写回
	SetOperandValue(ctx, d_ctx, 0, res);
}

// 辅助：获取 Group 3 的源操作数 (Multiplier / Divisor)
// 根据你的 disasm 表定义：
// F6: MUL AL, Eb -> Op1=AL, Op2=Eb. 所以源操作数是 Op2 (index 1)
// F6: NOT Eb     -> Op1=Eb.         所以源操作数是 Op1 (index 0)
// 我们需要根据 reg 字段判断源在哪里
static uint32_t GetGroup3Source(CPU_Context* ctx, DecodeContext* d_ctx) {
	// reg 0(TEST), 2(NOT), 3(NEG): 单操作数或立即数，源在 Op1 或 Op2(TEST)
	// reg 4(MUL), 5(IMUL), 6(DIV), 7(IDIV): disasm 表定义了 Op1=Acc, Op2=Src. 所以源在 Op2
	if (d_ctx->reg >= 4) {
		return GetOperandValue(ctx, d_ctx, 1);
	}
	return GetOperandValue(ctx, d_ctx, 0); // NOT/NEG
}

// 写入 Group 3 结果
static void SetGroup3Dest(CPU_Context* ctx, DecodeContext* d_ctx, uint32_t val) {
	SetOperandValue(ctx, d_ctx, 0, val);
}

// 处理 Group 3 指令 (F6/F7: TEST/NOT/NEG/MUL/IMUL/DIV/IDIV)
static void Exec_Group3(CPU_Context* ctx, DecodeContext* d_ctx) {
	int size = GetOperandBitSize(ctx, d_ctx, d_ctx->entry.op1);
	// 注意：对于 MUL/DIV，Op1 是 AL/AX/EAX，Op2 是 r/m。
	// 我们主要关心 r/m 的大小，但在 disasm 表里 Op1 已经反映了正确的大小 (AL=8, EAX=32)

	// 获取源操作数 (乘数 或 除数)
	uint32_t src = GetGroup3Source(ctx, d_ctx);
	// 针对 8/16 位截断源操作数，防止高位垃圾

	if (size == 8) src &= 0xFF;
	else if (size == 16) src &= 0xFFFF;

	switch (d_ctx->reg) {
		case 0: {// TEST (Iz/Ib)
		// TEST 本质是 AND，但不写回
		// 注意：F6/F7 的 TEST 指令结构通常是 TEST r/m, imm
		// disasm 表里 group_tables[3/17] 的 index 0 是 TEST。
		// 调用我们之前的通用 ALU 函数即可
			Exec_ALU_Generic(ctx, d_ctx, ALU_AND, true); // TEST (AND + 不写回)
			break;
		}
		case 2: {// NOT r/m 不影响标志位
			SetGroup3Dest(ctx, d_ctx, ~src);
			break;
		}
		case 3: {// NEG r/m 模拟 0 - src
			SetGroup3Dest(ctx, d_ctx,(0-src));
			UpdateEFLAGS(ctx, (0-src), 0, src, size, ALU_SUB);
			break;
		}
		case 4: {// MUL (Unsigned Multiply) 算法：Accumulator * src
			if (size == 8) {
				// AL * r/m8 -> AX
				uint32_t al = ctx->EAX.I8.L;
				uint32_t res = al * src;
				ctx->EAX.I16 = (uint16_t)res; // 结果写入 AX
				// 更新 CF/OF: 如果高8位(AH)非0则置位
				ctx->EFLAGS.CF = ctx->EFLAGS.OF = (res & 0xFF00) ? 1 : 0;

			}
			else if (size == 16) {
				// AX * r/m16 -> DX:AX
				uint32_t ax = ctx->EAX.I16;
				uint32_t res = ax * src;
				ctx->EDX.I16 = res >> 16; // 高16位写入 DX
				ctx->EAX.I16 = res & 0xFFFF; // 低32位写入 AX
				ctx->EFLAGS.CF = ctx->EFLAGS.OF = (ctx->EDX.I16 != 0);
			}
			else { //32
				// EAX * r/m32 -> EDX:EAX
				uint64_t eax = ctx->EAX.I32;
				uint64_t res = eax * (uint64_t)src;
				ctx->EDX.I32 = (uint32_t)(res >> 32); // 高32位写入 EDX
				ctx->EAX.I32 = (uint32_t)(res & 0xFFFFFFFF); // 低32位写入 EAX
				ctx->EFLAGS.CF = ctx->EFLAGS.OF = (ctx->EDX.I32 != 0);
			}
			break;
		}
		case 5: {
			if (size == 32) {
				int64_t eax = (int32_t)ctx->EAX.I32; //当你把一个 32 位的 int32_t 赋值给 64 位的 int64_t 时，C++ 编译器会自动触发符号扩展
				int64_t val = (int32_t)src; //如果 src 的最高位是 1，val的前 32 位就会全被填充为 1。注意不要强制类型转换成（int64_t）,注意这种默认转换和强制转换的区别
				int64_t res = eax * val;
				ctx->EDX.I32 = (uint32_t)(res >> 32); // 高32位写入 EDX
				ctx->EAX.I32 = (uint32_t)(res & 0xFFFFFFFF); // 低32位写入 EAX
			// IMUL 标志位：如果结果需要扩展到高位才能表示，则 CF=OF=1
			// 简单判断：如果 result != (int32_t)result，说明溢出了 32 位范围
				bool overflow = (res != (int64_t)(int32_t)res);
				ctx->EFLAGS.CF = ctx->EFLAGS.OF = overflow;
			}
			else if (size == 16) {
				int32_t ax = (int16_t)ctx->EAX.I16;
				int32_t val = (int16_t)src;
				int32_t res = ax * val;
				ctx->EDX.I16 = (uint32_t)(res >> 16); // 高16位写入 EDX
				ctx->EAX.I16 = (uint32_t)(res & 0xFFFF); // 低16位写入 EAX
				// IMUL 标志位：如果结果需要扩展到高位才能表示，则 CF=OF=1
				// 简单判断：如果 result != (int32_t)result，说明溢出了 32 位范围
				bool overflow = (res != (int32_t)(int16_t)res);
				ctx->EFLAGS.CF = ctx->EFLAGS.OF = overflow;
			}
			else {
				int16_t al = (int8_t)ctx->EAX.I8.L;
				int16_t val = (int8_t)src;
				int16_t res = al * val;
				ctx->EAX.I16 = (uint16_t)res; // 结果写入 AX
				bool overflow = (res != (int16_t)(int8_t)res);
				ctx->EFLAGS.CF = ctx->EFLAGS.OF = overflow;
			}
			break;
		}
		case 6: {// DIV (Unsigned Divide)
			if (src == 0) {
				printf("[CPU Error] Division by Zero at EIP=0x%08X\n", ctx->EIP);
				// 真实 CPU 会触发 Int 0 异常，这里我们暂时简单处理：不执行
				return;
			}
			if (size == 32) {
				uint64_t dividend = ((uint64_t)ctx->EDX.I32 << 32) | ctx->EAX.I32;
				uint64_t quo = dividend / src;
				uint64_t rem = dividend % src;
				if (quo > 0xFFFFFFFF) {// 商溢出
					printf("[CPU Error] quotient overflow at EIP=0x%08X\n", ctx->EIP);
					return;
				}
				ctx->EAX.I32 = (uint32_t)quo;
				ctx->EDX.I32 = (uint32_t)rem;
				// DIV 标志位未定义，不做更新
			}
			else if (size == 16) {
				uint32_t dividend = ((uint32_t)ctx->EDX.I16 << 16) | ctx->EAX.I16;
				uint32_t quo = dividend / src;
				uint32_t rem = dividend % src;
				if (quo > 0xFFFF) {// 商溢出
					printf("[CPU Error] quotient overflow at EIP=0x%08X\n", ctx->EIP);
					return;
				}
				ctx->EAX.I16 = (uint16_t)quo;
				ctx->EDX.I16 = (uint16_t)rem;
			}
			else {//8位
				uint16_t dividend = ctx->EAX.I16; //被除数：AX
				uint16_t quo = dividend / src;
				uint16_t rem = dividend % src;
				if (quo > 0xFF) {// 商溢出
					printf("[CPU Error] quotient overflow at EIP=0x%08X\n", ctx->EIP);
					return;
				}//AH(余数):AL（商）
				ctx->EAX.I8.L = (uint8_t)quo;
				ctx->EAX.I8.H = (uint8_t)rem;
			}
			break;
		}
		case 7: { // IDIV (Signed Divide)
			if (src == 0) {
				printf("[CPU Error] Division by Zero at EIP=0x%08X\n", ctx->EIP);
				return;
			}

			if (size == 8) {
				// 【8位】被除数：AX (16位有符号)
				int16_t dividend = (int16_t)ctx->EAX.I16;
				int8_t  divisor = (int8_t)src;

				// 特判 INT8_MIN / -1
				if (dividend == -128 && divisor == -1) goto overflow_label;

				int16_t quo = dividend / divisor;
				int16_t rem = dividend % divisor;

				if (quo > 127 || quo < -128) goto overflow_label;

				ctx->EAX.I8.L = (int8_t)quo; // AL
				ctx->EAX.I8.H = (int8_t)rem; // AH
			}
			else if (size == 16) {
				// 【16位】被除数：DX:AX (32位有符号拼接)
				int32_t dividend = (int32_t)(((uint32_t)ctx->EDX.I16 << 16) | (uint16_t)ctx->EAX.I16);
				int16_t divisor = (int16_t)src;

				if (dividend == (int32_t)0x80000000 && divisor == -1) goto overflow_label;

				int32_t quo = dividend / divisor;
				int32_t rem = dividend % divisor;

				if (quo > 32767 || quo < -32768) goto overflow_label;

				ctx->EAX.I16 = (int16_t)quo; // AX
				ctx->EDX.I16 = (int16_t)rem; // DX
			}
			else if (size == 32) {
				// 【32位】被除数：EDX:EAX (64位有符号拼接)
				int64_t dividend = (int64_t)(((uint64_t)ctx->EDX.I32 << 32) | (uint32_t)ctx->EAX.I32);
				int32_t divisor = (int32_t)src;

				// 特判 LLONG_MIN 情况，防止 C++ 进程直接崩溃
				if (dividend == (int64_t)0x8000000000000000 && divisor == -1) goto overflow_label;

				int64_t quo = dividend / divisor;
				int64_t rem = dividend % divisor;

				if (quo > 2147483647LL || quo < -2147483648LL) goto overflow_label;

				ctx->EAX.I32 = (uint32_t)quo; // EAX
				ctx->EDX.I32 = (uint32_t)rem; // EDX
			}
			break;

		overflow_label:
			printf("[CPU Error] IDIV Quotient Overflow at EIP=0x%08X\n", ctx->EIP);
			// 这里只 return，不修改任何寄存器，模拟真 CPU 的异常挂起行为
			return;
		}
		default:
			printf("Group 3 error opcode: %d\n", d_ctx->reg);
	}
	return;
}

uint32_t MemRead(uint32_t addr, int uint8_t_len) {
	if (uint8_t_len == 1) return *(uint8_t*)addr;
	else if(uint8_t_len == 2)return *(uint16_t*)addr;
	return *(uint32_t*)addr;
}

void MemWrite(uint32_t addr, uint32_t val, int uint8_t_len) {
	if (uint8_t_len == 1) *(uint8_t*)addr = (uint8_t)val;
	else if(uint8_t_len == 2) *(uint16_t*)addr = (uint16_t)val;
	else *(uint32_t*)addr = val;
}


static void Exec_StringOp(CPU_Context* ctx, DecodeContext* d_ctx) {
	// 1. 确定宽度
	int bit_sz = (d_ctx->opcode & 1) ? ((d_ctx->pfx_op_size == 0x66) ? 16 : 32) : 8;
	int uint8_t_len = bit_sz / 8;

	// 2. 预计算步长 (核心优化)
	int step = (ctx->EFLAGS.DF == 0) ? uint8_t_len : -uint8_t_len;

	// 3. 获取当前地址
	uint32_t esi = ctx->GPR[6].I32;
	uint32_t edi = ctx->GPR[7].I32;

	switch (d_ctx->opcode & 0xFE) {
		case 0xA4: {// MOVS
			MemWrite(edi, MemRead(esi, uint8_t_len), uint8_t_len);
			ctx->ESI.I32 += step;
			ctx->EDI.I32 += step;
			break;
		}
		case 0xA6: { // CMPS
			uint32_t val_src = MemRead(esi, uint8_t_len);
			uint32_t val_dest = MemRead(edi, uint8_t_len);
			uint32_t res = val_dest - val_src;
			UpdateEFLAGS(ctx, res, val_dest, val_src, bit_sz, ALU_SUB);
			ctx->EDI.I32 += step;
			ctx->ESI.I32 += step;
			break;
		}
		case 0xAA: { // STOS
			uint32_t val = 0;
			if (bit_sz == 8) val = ctx->EAX.I8.L;
			else if (bit_sz == 16) val = ctx->EAX.I16;
			else if (bit_sz == 32) val = ctx->EAX.I32;
			MemWrite(edi, val, uint8_t_len);
			ctx->EDI.I32 += step;
			break;
		}
		case 0xAC: {//LODS
			uint32_t val = MemRead(esi, uint8_t_len);
			if (bit_sz == 8) ctx->EAX.I8.L = (uint8_t)val;
			else if (bit_sz == 16) ctx->EAX.I16 = (uint16_t)val;
			else if (bit_sz == 32) ctx->EAX.I32 = val;
			ctx->ESI.I32 += step;
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
			ctx->EDI.I32 += step;
			break;
		}
	}
}

static bool Exec_REP_StringOp(CPU_Context* ctx, DecodeContext* d_ctx) {
	// 1. 初始检查：如果 ECX 已经到 0，循环彻底结束
	if(ctx->ECX.I32 == 0) return false;
	// 2. 执行单次操作
	Exec_StringOp(ctx, d_ctx);
	//3. 递减 ECX
	ctx->ECX.I32--;
	// 4. 判断是否要“停在原地”继续下一次循环
	// 基本条件：ECX 还没减到 0
	bool should_continue = (ctx->ECX.I32 > 0);
	// 5. 处理 SCAS / CMPS 的额外终止条件 (ZF)
	// 技巧：利用位运算快速识别 A6, A7 (CMPS) 和 AE, AF (SCAS)
	bool is_compare_op = (d_ctx->opcode & 0xA6) == 0xA6;
	if (is_compare_op && should_continue) {
		if (d_ctx->pfx_rep) {
			should_continue = (ctx->EFLAGS.ZF == 1); // REPE/REPZ
		}
		else if (d_ctx->pfx_repne) {
			should_continue = (ctx->EFLAGS.ZF == 0); // REPNE/REPNZ
		}
	}
	return should_continue;
}

static void Exec_LEA(CPU_Context* ctx, DecodeContext* d_ctx) {
	// 1. 计算有效地址 (Effective Address)
	// 这里的核心是复用 GetEffectiveAddress，但坚决不要调用 MemRead！
	uint32_t addr = GetEffectiveAddress(ctx, d_ctx);
	// 2. 将地址本身写入目的寄存器
	// LEA 的目的操作数通常是 Gv (32位通用寄存器)
	SetOperandValue(ctx, d_ctx, 0, addr);
	// 注意：LEA 不改变任何标志位 (EFLAGS)
}

static void Exec_XCHG(CPU_Context* ctx, DecodeContext* d_ctx) {
	uint32_t op1= GetOperandValue(ctx, d_ctx, 0);
	uint32_t op2 = GetOperandValue(ctx, d_ctx, 1);
	SetOperandValue(ctx, d_ctx, 1, op1);
	SetOperandValue(ctx, d_ctx, 0, op2);
}

// 辅助：将 EFLAGS 结构体打包成 32位 整数
static uint32_t PackEFLAGS(CPU_Context* ctx)
{
	// 简单粗暴法：利用 union 的共用体特性 (假设是小端序)
	// 但为了严谨，x86 规定 Bit 1 永远是 1，Bit 3,5,15 永远是 0
	uint32_t raw = ctx->EFLAGS.Value;
	raw |= 0x00000002; // Bit 1 always 1
	raw &= ~0x00008028; // Bit 3, 5, 15 always 0 (0x8000 + 0x20 + 0x08)
	return raw;
}

// 辅助：从 32位 整数解包到 EFLAGS
static void UnpackEFLAGS(CPU_Context* ctx, uint32_t val)
{
	// 保留位处理：通常我们不希望用户修改保留位
	// 但模拟器为了简单，可以直接赋值，只是强行修正固定位
	ctx->EFLAGS.Value = val;
	ctx->EFLAGS.Value |= 0x00000002; // Bit 1 always 1
}

// PUSHF (0x9C)
static void Exec_PUSHF(CPU_Context* ctx) {
	uint32_t eflags = PackEFLAGS(ctx);
	// 压栈 (32位)
	ctx->ESP.I32 -= 4;
	MemWrite(ctx->ESP.I32, eflags, 4);
}
//POPF (0x9D)
static void Exec_POPF(CPU_Context* ctx) {
	// 弹栈 (32位)
	uint32_t eflags = MemRead(ctx->ESP.I32, 4);
	ctx->ESP.I32 += 4;
	UnpackEFLAGS(ctx, eflags);
}
// LEAVE (0xC9): 恢复栈帧
// 等价于: MOV ESP, EBP; POP EBP;
static void Exec_LEAVE(CPU_Context* ctx) {
	ctx->ESP.I32 = ctx->EBP.I32;
	ctx->EBP.I32 = MemRead(ctx->ESP.I32, 4);
	ctx->ESP.I32 += 4;
}
// 从解码上下文中获取参数
// 解码器把第一个立即数(size)放在 imm，第二个(level)放在 imm2
static void Exec_ENTER(CPU_Context* ctx, DecodeContext* d_ctx) {
	// 参数1:分配的栈空间大小 (16位)
	uint16_t size = (uint16_t)d_ctx->imm;
	// 参数2:调用层级 (16位)
	uint8_t level = (uint8_t)(d_ctx->imm2 & 0x1F); // Intel 限制最大 31 层
	// 2. PUSH EBP (保存旧的帧指针)
	ctx->ESP.I32 -= 4;
	MemWrite(ctx->ESP.I32, ctx->EBP.I32, 4);
	// 暂存当前的栈顶作为新的帧指针起点
	uint32_t fp = ctx->ESP.I32;
	// 2. 处理嵌套层级 (Display)
	// 这是为了让嵌套函数能访问父级函数的局部变量
	if (level > 0) {
		uint32_t temp_fp = ctx->EBP.I32;//初始还是父函数的ebp起始
		for (int i = 1; i < level; i++) {//从1开开始，当前的栈指针在后面给
			temp_fp -= 4;
			ctx->ESP.I32 -= 4;
			uint32_t parent_fp = MemRead(temp_fp, 4);
			MemWrite(ctx->ESP.I32, parent_fp, 4);
		}
		// 最后一次 PUSH 当前的帧指针
		ctx->ESP.I32 -= 4;
		MemWrite(ctx->ESP.I32, fp, 4);
	}
	//3.更新寄存器状态,分配栈空间
	ctx->EBP.I32 = fp;
	ctx->ESP.I32 -= size;

}

// 处理 Group 4 (0xFE): 只有 INC Eb / DEC Eb
static void Exec_Group4(CPU_Context* ctx, DecodeContext* d_ctx) {
	uint32_t val = GetOperandValue(ctx, d_ctx, 0);
	switch (d_ctx->reg) {
		case 0: // INC Eb
			Exec_ALU_Generic(ctx, d_ctx, ALU_INC, false);break;
		case 1: // DEC Eb
			Exec_ALU_Generic(ctx, d_ctx, ALU_DEC, false);break;
		default:
			printf("[Error] Group 4 unknown reg: %d (Only 0/1 defined)\n", d_ctx->reg);break;
	}
}

// 处理 Group 5 (0xFF): INC/DEC/CALL/JMP/PUSH
static bool Exec_Group5(CPU_Context* ctx, DecodeContext* d_ctx) {
	// 获取操作数 (目标地址或要操作的值)
	// 注意：对于 CALL/JMP，Op1 (Ev) 是存放"目标地址"的容器
	uint32_t val = GetOperandValue(ctx, d_ctx, 0);
	bool jumped = false;
	switch (d_ctx->reg) {
		case 0: // INC Ev
			Exec_ALU_Generic(ctx, d_ctx, ALU_INC, false); break;
		case 1: // DEC Ev
			Exec_ALU_Generic(ctx, d_ctx, ALU_DEC, false); break;
		case 2: {// CALL Ev(绝对间接调用)
			uint32_t retaddr = ctx->EIP + d_ctx->instr_len;// 1. 压入返回地址 (当前 EIP + 指令长度)
			ctx->ESP.I32 -= 4;
			MemWrite(ctx->ESP.I32,retaddr,4); // 2. 跳转到绝对地址 (val 就是目标地址)
			ctx->EIP = val;// 注意：外层需要知道发生了跳转
			jumped = true;
			break;
		}
		case 3: // CALL Mp (Far Call) - 暂时不支持保护模式跨段
			printf("[Warning] CALL FAR not supported yet.\n");
			break;
		case 4: {// JMP Ev (绝对间接跳转)
			ctx->EIP = val; // 直接跳转到目标地址
			jumped = true;
			break;
		}
		case 5: // JMP Mp (Far Jump) - 暂时不支持保护模式跨段
			printf("[Warning] JMP FAR not supported yet.\n");
			break;
		case 6: // PUSH Ev
			ctx->ESP.I32 -= 4;
			MemWrite(ctx->ESP.I32, val,4);
			break;
		default:
			printf("[Error] Group 5 unknown reg: %d\n", d_ctx->reg);
			break;
	}
	return jumped;
}

// MOVZX: Zero Extend (零扩展)
// src_bits: 源操作数位数 (8 或 16)
static void Exec_MOVZX(CPU_Context* ctx, DecodeContext* d_ctx, int src_bits) {
	uint32_t val = GetOperandValue(ctx, d_ctx, 1); // 源操作数在 Op2
	// 强制截断高位，确保高位为0
	if (src_bits == 8) val &= 0xFF;
	else if(src_bits ==16) val &= 0xFFFF;
	// 写入 Op1 (通常是 32位寄存器)
	SetOperandValue(ctx, d_ctx, 0, val);

}

// MOVSX: Sign Extend (符号扩展)
static void Exec_MOVSX(CPU_Context* ctx, DecodeContext* d_ctx, int src_bits) {
	uint32_t val = GetOperandValue(ctx, d_ctx, 1); // 源操作数在 Op2
	int32_t res = 0;
	// 符号扩展
	if (src_bits == 8) res = (int32_t)(int8_t)val;// 先转为 int8_t 截断并保留符号，再转为 int32_t 触发符号扩展
	else if (src_bits == 16) res = (int32_t)(int8_t)val; //利用 C 语言的有符号类型转换特性来实现符号扩展
	SetOperandValue(ctx, d_ctx, 0, (uint32_t)res);
}

// SETcc Eb
static void Exec_SETcc(CPU_Context* ctx, DecodeContext* d_ctx) {
	  bool match = CheckCondition(ctx, d_ctx->opcode);
	  // SETcc 只写入 1 个字节 (Eb)
	  SetOperandValue(ctx, d_ctx, 0, match ? 1 : 0);
}

static void Exec_CMOVcc(CPU_Context* ctx, DecodeContext* d_ctx) {
	bool match = CheckCondition(ctx, d_ctx->opcode); //检查条件
	if (match) {// 条件成立：执行传送
		uint32_t val = GetOperandValue(ctx, d_ctx, 1); //获取源操作数
		SetOperandValue(ctx, d_ctx, 0, val); //写入目的操作数
	}
	// 条件不成立：什么都不做 (NOP)
}

// IMUL Gv, Ev (0x0F AF)
// 有符号乘法，双操作数版本：Dest = Dest * Src
static void Exec_IMUL_2_Op(CPU_Context* ctx, DecodeContext* d_ctx) {
	// 1. 确定位宽 (16 或 32)
	int size = (d_ctx->pfx_op_size == 0x66) ? 16 : 32;

	// 2. 读取操作数
	// Op1 是 Dest (Gv), Op2 是 Src (Ev)
	// 注意：GetOperandValue 会自动处理寄存器或内存读取
	uint32_t val_dest = GetOperandValue(ctx, d_ctx, 0);
	uint32_t val_src = GetOperandValue(ctx, d_ctx, 1);

	// 3. 执行运算与标志位计算
	if (size == 16) {
		int16_t d = (int16_t)val_dest;
		int16_t s = (int16_t)val_src;
		int32_t res = d * s; // 用更大一级类型计算以检测溢出

		// 写入结果 (截断回 16 位)
		SetOperandValue(ctx, d_ctx, 0, (uint16_t)res);

		// 更新 CF/OF
		// 规则：如果乘积结果超出了目标大小(16位)能表示的范围，CF=OF=1
		// 检查方法：看 res 是否等于 res 强转回 int16 再转回 int32 的值
		if (res != (int32_t)(int16_t)res) {
			ctx->EFLAGS.CF = ctx->EFLAGS.OF = 1;
		}
		else {
			ctx->EFLAGS.CF = ctx->EFLAGS.OF = 0;
		}
	}
	else { // 32-bit
		int32_t d = (int32_t)val_dest;
		int32_t s = (int32_t)val_src;
		int64_t res = (int64_t)d * (int64_t)s;

		// 写入结果 (截断回 32 位)
		SetOperandValue(ctx, d_ctx, 0, (uint32_t)res);

		// 更新 CF/OF
		if (res != (int64_t)(int32_t)res) {
			ctx->EFLAGS.CF = ctx->EFLAGS.OF = 1;
		}
		else {
			ctx->EFLAGS.CF = ctx->EFLAGS.OF = 0;
		}
	}
	// SF, ZF, AF, PF 在 IMUL 中是未定义的 (Undefined)，我们通常保持不变或随意。
	// 为了简单，我们不更新这些标志位。
}

// 处理 0x98 (CBW/CWDE) 和 0x99 (CWD/CDQ)
static void Exec_SignExtend(CPU_Context* ctx, DecodeContext* d_ctx) {
	int op_size = (d_ctx->pfx_op_size == 0x66) ? 16 : 32;

	if (d_ctx->opcode == 0x98) {
		if (op_size == 16) {
			// CBW: AL (8) -> AX (16)
			ctx->EAX.I16 = (int16_t)(int8_t)ctx->EAX.I8.L;
		}
		else {
			// CWDE: AX (16) -> EAX (32)
			ctx->EAX.I32 = (int32_t)(int16_t)ctx->EAX.I16;
		}
	}
	else if (d_ctx->opcode == 0x99) {
		if (op_size == 16) {
			// CWD: AX -> DX:AX
			if ((int16_t)ctx->EAX.I16 < 0) ctx->EDX.I16 = 0xFFFF;
			else ctx->EDX.I16 = 0x0000;
		}
		else {
			// CDQ: EAX -> EDX:EAX
			if ((int32_t)ctx->EAX.I32 < 0) ctx->EDX.I32 = 0xFFFFFFFF;
			else ctx->EDX.I32 = 0x0000;
		}
	}
}

// 处理 INT n (0xCD) 和 INT 3 (0xCC)
// 我们将在这里通过 HLE (高层模拟) 拦截系统调用
static void Exec_INT(CPU_Context* ctx, DecodeContext* d_ctx) {
	uint8_t int_num = 0;
	// 区分 INT 3 (0xCC) 和 INT n (0xCD imm8)
	if (d_ctx->opcode == 0xCC) {
		int_num = 3;
	}
	else{
		int_num = (uint8_t)d_ctx->imm;
	}
	// --- High Level Emulation (HLE) 拦截 ---
	// 我们没有真实的内核，所以在这里用 C 代码模拟操作系统行为

	if (int_num == 0x21) {
		// [DOS API 模拟]
		// AH = 0x02: 输出字符 (DL = 字符)
		// AH = 0x09: 输出字符串 (DS:DX = 字符串, '$' 结尾)
		// AH = 0x4C: 退出程序
		uint8_t ah = ctx->EAX.I8.H;
		switch (ah) {
			case 0x02: {// PutChar
				putchar(ctx->EDX.I8.L);
				fflush(stdout);
				break;
			}
			case 0x09: {// PutString
				// DOS 字符串以 '$' 结束
				// 获取物理地址: DS << 4 + DX (实模式) 或者 DS base + EDX (保护模式)
				// 简单起见，我们假设是 Flat Model，直接用 EDX
				uint32_t addr = ctx->EDX.I32;
				char ch = MemRead(addr, 1);
				while (ch != '$' && ch != 0) {// 防御性：遇到 0 也停
					putchar(ch);
					addr++;
					ch = MemRead(addr, 1);
				}
				break;
			}
			case 0x4c: {// Exit
				uint8_t exit_code = ctx->EAX.I8.L;
				printf("\n[Program exited with code %d]\n", exit_code);
				exit(exit_code);
				break;
			}
			default:
				printf("[TinyX86] Unhandled INT 21h service: AH=0x%02X\n", ah); 
				break;
		}
	}
	else if (int_num == 3) {
		printf("\n[TinyX86] Breakpoint Hit (INT 3) at EIP=0x%08X\n", ctx->EIP);
		// 在调试器中，这里会暂停 CPU 循环,设置停止标记
		ctx->Halted = true;
	}
	else
	{
		printf("[TinyX86] Unhandled Interrupt: 0x%02X\n", int_num);
	}
}

static bool Exec_LOOP(CPU_Context* ctx, DecodeContext* d_ctx) {
	// 1. 递减计数器 (ECX/CX)
	// 根据地址大小前缀 (0x67) 判断是使用 ECX 还是 CX
	bool condition = false;
	uint32_t counter;
	if (d_ctx->pfx_addr_size == 0x67) {
		 counter = --(ctx->ECX.I16);
	}
	else {
		counter = --(ctx->ECX.I32);
	}
	// 2. 检查条件
	bool counter_not_zero = (counter != 0);
	if (d_ctx->opcode == 0xE2) { // LOOP
		condition = counter_not_zero;
	}
	else if (d_ctx->opcode == 0xE1) {// LOOPE / LOOPZ
		condition = counter_not_zero && (ctx->EFLAGS.ZF == 1);
	}
	else if (d_ctx->opcode == 0xE0) {// LOOPNE / LOOPNZ
		condition = counter_not_zero && (ctx->EFLAGS.ZF == 0);
	}
	// 3. 执行跳转
	if (condition) {
		ctx->EIP += d_ctx->disp_len +(int8_t)d_ctx->imm; // 有符号偏移
		return true;
	}
	return false;
}

static void FPU_PUSH(CPU_Context* ctx, double val) {
	ctx->FPU.SW.TOP = (ctx->FPU.SW.TOP - 1) & 0x7; // TOP 环绕递减
	ST(0) = val;
}

static double FPU_POP(CPU_Context* ctx) {
	double val = ST(0);
	ctx->FPU.SW.TOP = (ctx->FPU.SW.TOP + 1) & 0x7; // TOP 环绕递增
	return val;
}

static double MemReadF(uint32_t addr, int size) {
	if (size == 4) {//float也读成double
		uint32_t raw =  MemRead(addr, 4);
		return (double)(*(float*)&raw);
	}
	else { //double
		uint32_t low = MemRead(addr, 4);
		uint32_t high = MemRead(addr + 4, 4);
		uint64_t raw = ((uint64_t)high << 32) | low;
		return *(double*)&raw;
	}
}

static void MemWriteF(uint32_t addr, double val, int size) {
	if (size == 4) {
		float f = (float)val;
		uint32_t raw = *(uint32_t*)&f;
		MemWrite(addr, raw, 4);
	} else {
		uint64_t raw = *(uint64_t*)&val;
		MemWrite(addr, (uint32_t)(raw & 0xFFFFFFFF), 4);
		MemWrite(addr + 4, (uint32_t)(raw >> 32), 4);
	}
}

static void UpdateFPUFlags(CPU_Context* ctx, double v1, double v2) {
	// 先清空相关标志
	ctx->FPU.SW.C0 = 0;//CF
	ctx->FPU.SW.C2 = 0;//非PF- isNaN？
	ctx->FPU.SW.C3 = 0;//ZF

	if (isnan(v1) | isnan(v2)) {//属于 C 语言的标准数学库 <math.h>的宏，判断是否无序NaN
		ctx->FPU.SW.C0 = 1;
		ctx->FPU.SW.C2 = 1;
		ctx->FPU.SW.C3 = 1;
	} else if (v1 > v2) {
		//全0
	} else if (v1 = v2) {
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
	// 初始化为 0x02，确保保留位 Bit 1 为 1
	uint8_t ah = 0x02;
	ah |= (ctx->EFLAGS.SF & 1) << 7;
	ah |= (ctx->EFLAGS.ZF & 1) << 6;
	ah |= (ctx->EFLAGS.AF & 1) << 4;
	ah |= (ctx->EFLAGS.PF & 1) << 2;
	ah |= (ctx->EFLAGS.CF & 1) << 0;
	ctx->EAX.I8.H = ah;
}

// 读取整数内存 (int16/int32) -> double (处理 DA/DB/DE/DF)
static double MemReadI(uint32_t addr, int size) {
	uint32_t raw = MemRead(addr, size);
	if (size == 2) {
		return (double)(int16_t)raw;    // 强转符号扩展
	} else { // size == 4
		return (double)(int32_t)raw;    // 强转符号扩展
	}
}

// 写入整数内存 double -> (int16/int32) (处理 DB/DF)
static void MemWriteI(uint32_t addr, double val, int size) {
	// 简单截断取整 (CVTTSD2SI 行为)
	if (size == 2) {
		int16_t i16 = (int16_t)val;
		MemWrite(addr, (uint16_t)i16, 2);
	} else { // size == 4
		int32_t i32 = (int32_t)val;
		MemWrite(addr, (uint32_t)i32, 4);
	}
}

static void Exec_FPU(CPU_Context* ctx, DecodeContext* d_ctx) {
	uint8_t op_group = d_ctx->opcode;
	uint8_t sub_op = d_ctx->reg;
	bool is_mem = d_ctx->mod != 3;
	// ==================================================
	// 路径 A: 内存操作
	// ==================================================
	if (is_mem) {
		uint32_t addr = GetEffectiveAddress(ctx, d_ctx);
		// ------------------------------------------------
		// Group 1: 算术运算 (D8, DC, DA, DE)
		// 逻辑：ST0 = ST0 op [MEM]
        // ------------------------------------------------
		if (op_group == 0xD8 || op_group == 0xDA || op_group == 0xDC || op_group == 0xDE) {
			double src = 0.0;
			// 1. 根据指令组读取不同类型的源操作数
			if (op_group == 0xD8) src = MemReadF(addr, 4); // float
			else if (op_group == 0xDC) src = MemReadF(addr, 8); // double
			else if (op_group == 0xDA) src = MemReadI(addr, 4); // int32 (FIADD...)
			else if (op_group == 0xDE) src = MemReadI(addr, 2); // int16 (FIADD...)

			double* dst = &ST(0);//直接指针修改ST值
			// 2. 执行统一算术逻辑
			switch (sub_op) {
				case 0: *dst += src; break; // FADD / FIADD
				case 1: *dst *= src; break; // FMUL / FIMUL
				case 2: UpdateFPUFlags(ctx, *dst, src); break; // FCOM / FICOM
				case 3: UpdateFPUFlags(ctx, *dst, src); FPU_POP(ctx); break; // FCOMP / FICOMP
				case 4: *dst -= src; break; // FSUB / FISUB
				case 5: *dst = src - *dst; break; // FSUBR / FISUBR
				case 6: *dst /= src; break; // FDIV / FIDIV
				case 7: *dst = src / *dst; break; // FDIVR / FIDIVR
			}
		}
		// ------------------------------------------------
		// Group 2: 数据传输 (D9, DD, DB, DF)
		// 逻辑：Load / Store
		// ------------------------------------------------
		else {
			int size = 0;
			bool is_int = false;
			// 确定基本类型和大小
			if (op_group == 0xD9) { size = 4; }           // float
			else if (op_group == 0xDD) { size = 8; }           // double
			else if (op_group == 0xDB) { size = 4; is_int = true; } // int32
			else if (op_group == 0xDF) { size = 2; is_int = true; } // int16 ，先简化处理再处理特殊值，不想深化下去了

			switch (sub_op) {
				case 0: {// FLD / FILD (Load)
					if(is_int) FPU_PUSH(ctx,MemReadI(addr, size));
					else       FPU_PUSH(ctx, MemReadF(addr, size));
					break;
				}
				case 2: {// FST / FIST (Store)
					if (is_int) MemWriteI(addr, ST(0), size);
					else        MemWriteF(addr, ST(0), size);
					break;
				}
				case 3: {// FSTP / FISTP (Store & Pop)
					if (is_int) MemWriteI(addr, FPU_POP(ctx), size);
					else        MemWriteF(addr, FPU_POP(ctx), size);
					break;
				}
				case 5: {// 特殊: FILD m64 (DF /5) - 加载 64位整数
					if (op_group == 0xDF) {
						uint32_t low = MemRead(addr, 4);
						uint32_t high = MemRead(addr + 4, 4);
						int64_t val64 = ((int64_t)high << 32) | low;
						FPU_PUSH(ctx, (double)val64);
					} else if (op_group == 0xD9) {
						// D9 /5: FLDCW (Load Control uint16_t) - 适配 CW
						// 直接读取内存到 Value 联合体成员
						ctx->FPU.CW.Value = (uint16_t)MemRead(addr, 2);
					}
					break;
				}
				case 7: {// FISTP m64
					if (op_group == 0xDF) {
						int64_t val64 = (int64_t)FPU_POP(ctx);
						MemWrite(addr, (uint32_t)(val64 & 0xFFFFFFFF), 4);
						MemWrite(addr + 4, (uint32_t)(val64 >> 32), 4);
					} else if (op_group == 0xD9) {
						// D9 /7: FNSTCW (Store Control uint16_t) - 适配 CW
						MemWrite(addr, ctx->FPU.CW.Value, 2);
					} else if (op_group == 0xDD) {
						// DD /7: FSTSW (Store Status uint16_t) - 适配 SW
						MemWrite(addr, ctx->FPU.SW.Value, 2);
					}
					break;
				}
				default:
					printf("[FPU] Unimpl Mem Op: %02X /%d\n", op_group, sub_op);
					return;
			}
		}
	} 
	// ==================================================
	// 路径 B: 寄存器操作
	// ==================================================
	else {
		int i = d_ctx->rm; // ST(i)

		// 1. D8: ST0 = ST0 op ST(i)
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
		}
		// 2. DC: ST(i) = ST(i) op ST(0)
		else if (op_group == 0xDC) {
			switch (sub_op) {
				case 0: ST(i) += ST(0); break;
				case 1: ST(i) *= ST(0); break;
				case 4: ST(i) = ST(0) - ST(i); break;
				case 5: ST(i) = ST(i) - ST(0); break;
				case 6: ST(i) = ST(0) / ST(i); break;
				case 7: ST(i) = ST(i) / ST(0); break;
			}
		}
		// 3. DE: ST(i) = ST(i) op ST(0) 然后 POP
		else if (op_group == 0xDE) {
			switch (sub_op) {
				case 0: ST(i) += ST(0); FPU_POP(ctx); break;
				case 1: ST(i) *= ST(0); FPU_POP(ctx); break;
				case 3: // DE D9: FCOMPP (特殊: 弹栈两次)
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
		}
		// 4. DA: 特殊指令
		else if (op_group == 0xDA) {
			if (sub_op == 5 && i == 1) { // DA E9: FUCOMPP
				UpdateFPUFlags(ctx, ST(0), ST(1));
				FPU_POP(ctx); FPU_POP(ctx);
			}
		}
		// 5. DB: 控制指令
		else if (op_group == 0xDB) {
			if (sub_op == 4) {
				if (i == 2) { // FCLEX
					ctx->FPU.SW.Value &= 0x7F00;
				} else if (i == 3) { // FINIT
					ctx->FPU.SW.TOP = 0;
					ctx->FPU.SW.Value = 0;
					ctx->FPU.SW.Value = 0x037F;
				}
			}
		}
		// 6. DF: 状态存储
		else if (op_group == 0xDF && sub_op == 4 && i == 0) {
			ctx->EAX.I16 = ctx->FPU.SW.Value;
		}
		// [新增] 处理 0xD9 组的寄存器指令 (FLD constants, FCHS, FABS 等)
		else if (op_group == 0xD9) {
			// sub_op 对应 ModRM.reg, i 对应 ModRM.rm
			if (sub_op == 4) {
				switch (i) {
					case 0: ST(0) = -ST(0); break; // FCHS (Change Sign)
					case 1: ST(0) = fabs(ST(0)); break; // FABS (Absolute)
						// case 4: FTST ...
						// case 5: FXAM ...
				}
			} else if (sub_op == 5) {
				// 加载常数 (Push Constant)
				switch (i) {
					case 0: FPU_PUSH(ctx, 1.0); break; // FLD1
					case 1: FPU_PUSH(ctx, log2(10.0)); break; // FLDL2T
					case 2: FPU_PUSH(ctx, log2(2.718281828)); break; // FLDL2E
					case 3: FPU_PUSH(ctx, 3.141592653589793); break; // FLDPI
					case 4: FPU_PUSH(ctx, log10(2.0)); break; // FLDLG2
					case 5: FPU_PUSH(ctx, log(2.0)); break; // FLDLN2
					case 6: FPU_PUSH(ctx, 0.0); break; // FLDZ
				}
			}
			// sub_op 6/7 还有 F2XM1, FSQRT, FSIN 等，暂时可略
		}
	}
}
