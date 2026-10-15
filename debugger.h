#pragma once
#include "TinyX86.h"
#include <stdbool.h>
#include <stdint.h>
// ============================================================================
// TinyX86 交互式调试器接口
// ============================================================================
/**
 * @brief 启动调试器主循环
 * 程序将阻塞在此处，直到用户输入 'q' 退出或执行 'c' 继续运行
 */
void Debugger(CPU_Context* ctx);
// ============================================================================
// 状态显示与控制 (公开API，供外部调用)
// ============================================================================
// 打印完整 CPU 状态 (GPRs, EFLAGS, FPU)
void ShowState(CPU_Context* ctx);
// 打印 EFLAGS 标志位
void PrintFlags(CPU_Context* ctx);
// 打印 FPU 状态
void PrintFPU(CPU_Context* ctx);
// 设置寄存器状态
// 返回 true 表示设置成功，false 表示名称无效
bool SetCpuState(CPU_Context* ctx, const char* name_in, uint32_t val);