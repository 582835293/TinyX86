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
// 单条调试器指令执行接口（供命令行循环与 WebAssembly 前端直接调用）
bool DebuggerExecuteCommand(CPU_Context* ctx, const char* input_line);

// ============================================================================
// 断点管理与状态控制公开 API
// ============================================================================
bool DebuggerToggleBreakpoint(uint32_t addr);
int  DebuggerGetBreakpoints(uint32_t* out_addrs, int max_count);
void DebuggerClearBreakpoints(void);

void ShowState(CPU_Context* ctx);
void PrintFlags(CPU_Context* ctx);
void PrintFPU(CPU_Context* ctx);
bool SetCpuState(CPU_Context* ctx, const char* name_in, uint32_t val);

#ifdef __cplusplus
}
#endif
