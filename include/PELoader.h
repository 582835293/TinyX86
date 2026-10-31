#pragma once
#include "TinyX86.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// 释放内存
void UnloadExe(CPU_Context* ctx);
// 加载 PE 文件 (自动解析基址和入口)
bool LoadExe(const char* filepath, CPU_Context* ctx);
// 从内存缓冲区加载 PE32 镜像 (供 CLI 与 WebAssembly 共同调用)
bool LoadExeFromMemory(const void* pFileBuffer, uint32_t fileSize, CPU_Context* ctx);
// 加载 Bin (指定基址，如 0x400000)
bool LoadBin(const char* filepath, CPU_Context* ctx, uint32_t baseAddr);
// 加载内存数组
bool LoadMem(const uint8_t* code, uint32_t size, CPU_Context* ctx, uint32_t baseAddr);

#ifdef __cplusplus
}
#endif
