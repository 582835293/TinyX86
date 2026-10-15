# TinyX86

滴水逆向学习过程中的纯 C x86-32 CPU 模拟器，包含寄存器与 EFLAGS、指令执行、部分 x87 运算、PE32/原始机器码加载，以及命令行交互调试器。

解码器复用 [Core-x86-Disassembler](https://github.com/582835293/Core-x86-Disassembler)，PE 功能复用 [PETools](https://github.com/582835293/PETools)。两个库通过 `deps/` 子模块固定版本，通过静态库项目引用编译，应用根目录不再保留共享源码副本。

## 下载与构建

```powershell
git clone --recurse-submodules https://github.com/582835293/TinyX86.git
cd TinyX86
# 在 Visual Studio Developer PowerShell 中运行；也可直接打开仓库内 .slnx
msbuild "TinyX86.vcxproj" /m /p:Configuration=Debug /p:Platform=Win32
# VS 2022 工具集可追加 /p:PlatformToolset=v143
```

安装 Visual Studio C++ 桌面开发组件和 Windows 10 SDK，选择 x86/Win32。CMake 构建与验证：

```powershell
cmake -S . -B build -A Win32
cmake --build build --config Debug --parallel
ctest --test-dir build -C Debug --output-on-failure
```

完整应用当前只支持 Windows x86/PE32。[共享依赖、升级方法和项目关系](docs/project-relationships.md)。

## 使用

启动 `Debug/TinyX86.exe`（CMake 构建为 `build/Debug/TinyX86.exe`）后：

```text
t                   加载内置递归幂运算机器码，进入调试器
e <path.exe>        加载 PE32 文件
b <path.bin>        加载原始机器码
q                   退出
```

调试器支持 `s [N]` 单步、`n` 步过、`c` 继续、`r` 查看寄存器、`u [N]` 反汇编、`b <addr>` 断点、`m <addr>` 内存查看、`k [N]` 栈查看、`w <reg> <value>` 修改寄存器和 `q` 返回外层 Shell。完整帮助输入 `h`。

## 文档与验证

- [架构、执行流程与当前限制](docs/architecture.md)
- [四个仓库的关系与日常维护](docs/project-relationships.md)
- [技术长文](Kanxue_TinyX86_Blog.md)

CMake 的 `cpu_integration_tests` 链接真实共享解码器，验证 MOV、算术、栈、标志、相对跳转和基本 x87 运算。原 `Sample-Test1` Google Test 工程保留，其解析器为桩函数，适合隔离执行辅助函数；它需要还原原有 NuGet 包，不属于新增集成测试。

这是指令子集模拟器，未完整模拟操作系统和 Win32 API，不保证能运行任意 PE。当前直接访问宿主内存，尚未实现内存隔离。本仓库尚未指定许可证。

[构建与测试记录](docs/validation.md) · [GitHub Actions](https://github.com/582835293/TinyX86/actions)
