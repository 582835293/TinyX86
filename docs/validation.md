# 跨平台构建与自动化测试验证矩阵

本文档记录 **TinyX86** 在不同操作系统、编译器架构与 WebAssembly 环境下的编译及自动化测试验证情况。

---

## 1. 跨平台 CI 构建与测试矩阵（GitHub Actions）

仓库内置 `.github/workflows/windows-x86.yml`，每次推送或提交 PR 时自动在以下 **6 个构建目标**上执行完整编译与测试验证：

| 构建环境 / 目标平台 | 编译器 / 工具链 | 架构与位宽 | 构建系统与验证命令 | 状态 |
| --- | --- | :---: | --- | :---: |
| **Ubuntu Linux** | GCC (`gcc`) | `x86_64` (64-bit) | `cmake` + `ctest --output-on-failure` | ✅ 通过 |
| **Ubuntu Linux** | LLVM Clang (`clang`) | `x86_64` (64-bit) | `cmake` + `ctest --output-on-failure` | ✅ 通过 |
| **macOS** | Apple Clang (`clang`) | `arm64` (Apple Silicon) | `cmake` + `ctest --output-on-failure` | ✅ 通过 |
| **Windows (Win32)** | MSVC (`cl.exe`) | `x86` (32-bit `-A Win32`) | `cmake` + `ctest -C Release` | ✅ 通过 |
| **Windows (x64)** | MSVC (`cl.exe`) | `x64` (64-bit `-A x64`) | `cmake` + `ctest -C Release` | ✅ 通过 |
| **WebAssembly (WASI)** | WASI-SDK 24 Clang (`wasm32-wasi`) + Vite | `wasm32` (32-bit) | `clang -> public/tinyx86.wasm` + `npm run build` | ✅ 通过 |

---

## 2. 自动化集成测试套件覆盖范围 (`tests/cpu_integration_tests.c`)

`cpu_integration_tests.c`（以及 `core/wasm_bridge.c` 中的 `wasm_run_c_integration_tests`）直接链接核心 C11 库 `tinyx86_core`，执行以下 3 大测试套件（共 13 项核心断言）：

1. **Suite 1：指令解码、ALU 算术逻辑、栈操作与 `EFLAGS` 标志位验证**
   - 连续解码并执行机器码序列：`mov eax, 40` -> `add eax, 2` -> `push eax` -> `pop ebx` -> `xor ecx, ecx` -> `cmp ebx, 42`。
   - 断言：
     - `ParseInstuction` 返回首条指令精确长度 `5`；
     - 执行 6 步后 `runcpu(&cpu, 6) == 0`；
     - `EAX == 42`、`EBX == 42`、`ECX == 0`；
     - `CMP` 比较相等后 `EFLAGS.ZF == 1`；
     - `PUSH` 与 `POP` 后栈顶指针 `ESP` 严格平衡恢复初始值；
     - `EIP` 精确停留在 `0x00400000 + 15`。
2. **Suite 2：相对分支跳转（`JMP rel8`）验证**
   - 执行 `jmp +5` (`0xEB 0x05`) 跳过中间的 `mov eax, 99`，直接执行目标地址处的 `mov eax, 42`。
   - 断言：执行 2 步后 `EAX == 42` 且 `EIP == 0x00400000 + 12`。
3. **Suite 3：x87 FPU 环形寄存器栈（`FLD1 + FLD1 + FADDP`）验证**
   - 连续执行两条 `fld1` (`0xD9 0xE8`) 将两个 `1.0` 压入环形栈，再执行 `faddp st(1), st(0)` (`0xDE 0xC1`) 相加并弹栈。
   - 断言：`FPU.Regs[FPU.SW.TOP] == 2.0`。

---

## 3. 关联仓库与历史回归测试记录

| 范围 | 验证结果 |
| --- | --- |
| `PETools` CMake Debug / Release | PE32 样本解析与 C++ 链接两组测试全部通过 |
| `Core-x86-Disassembler` CMake Debug | 63 条指令解码断言全部通过 |
| `TinyX86` CMake (Win32 / x64 / Linux / macOS) | 13 项跨平台集成测试断言全部通过 |
| `TinyX86` WASI WebAssembly (`public/tinyx86.wasm`) | 13 项 C 语言内存内自检断言全部通过，`power(2.0, 3)` 递归计算结果 `ST(0) == 8.0` |
| `TinyX86` 历史 GoogleTest 单元测试工程 | 44 项指令级单元测试全部通过 |
