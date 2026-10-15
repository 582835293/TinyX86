#pragma once

#include "disasm.h"
// FOU栈核心转换公式：物理索引 = (TOP + i) % 8
// 注意：TOP 是向下增长的（Push 是减），但在栈内寻址时 ST(1) 是 TOP+1
#define ST(i) ctx->FPU.Regs[(ctx->FPU.SW.TOP + (i)) & 0x7]
typedef union {
	uint32_t I32;
	uint16_t I16;
	struct {
		uint8_t L;
		uint8_t H;
	}I8;
}REG32;
// FPU 状态字(Status uint16_t) - 完全位域化
// 遵循 x86 小端序定义
typedef union {
	uint16_t Value; // 整体访问
	struct {
		uint16_t IE : 1; // 0: 无效操作异常
		uint16_t DE : 1; // 1: 非规格化操作数异常
		uint16_t ZE : 1; // 2: 除零异常
		uint16_t OE : 1; // 3: 溢出异常
		uint16_t UE : 1; // 4: 下溢异常
		uint16_t PE : 1; // 5: 精度异常
		uint16_t SF : 1; // 6: 栈错误
		uint16_t ES : 1; // 7: 异常汇总
		uint16_t C0 : 1; // 8: 条件码 0
		uint16_t C1 : 1; // 9: 条件码 1
		uint16_t C2 : 1; // 10: 条件码 2
		uint16_t TOP : 3; // 11-13: 栈顶指针 (关键！)
		uint16_t C3 : 1; // 14: 条件码 3
		uint16_t B : 1;  // 15: FPU 忙
	};
} FPU_STATUS_uint16_t;
// FPU 控制字 (Control uint16_t)
typedef union {
	uint16_t Value;
	struct {
		uint16_t IM : 1; // 无效操作屏蔽
		uint16_t DM : 1; // 非规格化屏蔽
		uint16_t ZM : 1; // 除零屏蔽
		uint16_t OM : 1; // 溢出屏蔽
		uint16_t UM : 1; // 下溢屏蔽
		uint16_t PM : 1; // 精度屏蔽
		uint16_t Reserved : 2;
		uint16_t PC : 2; // 精度控制 (00=24bit, 10=53bit, 11=64bit)
		uint16_t RC : 2; // 舍入控制
		uint16_t IC : 1; // 无穷控制
		uint16_t Reserved2 : 3;
	};
} FPU_CONTROL_uint16_t;
typedef struct {
	// 物理寄存器存储 (模拟 R0-R7)
	// 尽管 x87 是 80 位扩展精度，但在模拟器中我们用 double (64位) 近似
	double Regs[8];
	FPU_STATUS_uint16_t SW;
	FPU_CONTROL_uint16_t CW;
	uint16_t TW; // Tag uint16_t (暂简化)
} FPU_Context;

//tang
#define I_EAX	0
#define I_ECX	1
#define I_EDX	2
#define I_EBX	3
#define I_ESP	4
#define I_EBP	5
#define I_ESI	6
#define I_EDI	7

typedef struct {
	//通用寄存器
	union {
		REG32 GPR[8];
		struct {
			REG32 EAX;
			REG32 ECX;
			REG32 EDX;
			REG32 EBX;
			REG32 ESP;
			REG32 EBP;
			REG32 ESI;
			REG32 EDI;
		};
	};
	//指令指针
	uint32_t EIP;
	union {
		uint32_t Value;           // 整体读写 -> ctx.EFLAGS.Value
		struct {               // 按位访问 -> ctx.EFLAGS.Bits.ZF
			uint32_t CF : 1;
			uint32_t : 1;     // 保留位
			uint32_t PF : 1;
			uint32_t : 1;
			uint32_t AF : 1;
			uint32_t : 1;
			uint32_t ZF : 1;
			uint32_t SF : 1;
			uint32_t TF : 1;
			uint32_t IF : 1;
			uint32_t DF : 1;
			uint32_t OF : 1;
			uint32_t IOPL : 2;
			uint32_t NT : 1;
			uint32_t : 15;
		};
	} EFLAGS;
	//段寄存器
	union {
		uint16_t seg[6];
		struct {
			uint16_t ES;
			uint16_t CS;
			uint16_t SS;
			uint16_t DS;
			uint16_t FS;
			uint16_t GS;
		};
	};
	FPU_Context FPU;// FPU 子系统
	bool Halted;// 运行控制
}CPU_Context;

// 定义操作类型，用于标志位计算
typedef enum {
	ALU_ADD,
	ALU_OR,
	ALU_ADC,
	ALU_SBB,
	ALU_AND,
	ALU_SUB,
	ALU_XOR,
	ALU_CMP,
	ALU_INC,
	ALU_DEC
} ALU_Op;
//运行CPU模拟器核心函数,step为执行步数
int runcpu(CPU_Context* p, int step);
//内存读辅助函数,返回读取的值
uint32_t MemRead(uint32_t addr, int uint8_t_len);
//内存写辅助函数
void MemWrite(uint32_t addr, uint32_t val, int uint8_t_len);





