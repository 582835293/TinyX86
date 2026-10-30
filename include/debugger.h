#pragma once
#include "TinyX86.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// TinyX86 交互式调试器接口
// ============================================================================
void Debugger(CPU_Context* ctx);

// ============================================================================
// 状态显示与控制 (公开API，供外部调用)
// ============================================================================
void ShowState(CPU_Context* ctx);
void PrintFlags(CPU_Context* ctx);
void PrintFPU(CPU_Context* ctx);
bool SetCpuState(CPU_Context* ctx, const char* name_in, uint32_t val);

#ifdef __cplusplus
}
#endif
