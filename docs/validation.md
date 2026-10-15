# 迁移验证记录（2026-10-15）

| 范围 | 本机结果 |
| --- | --- |
| PETools CMake Debug | PE32 样本与 C++ 链接两组测试通过 |
| Core-x86-Disassembler CMake Debug | 63 条断言通过 |
| TinyX86 CMake Debug | 真实解码器集成测试 14 项检查通过 |
| TinyX86 原 Google Test 工程 | 44 项测试通过 |
| 三个应用原生 Debug Win32 | v145 构建通过 |
| 三个应用 .slnx Release x86 | v143 构建通过 |

本机使用 Visual Studio 2026、Windows SDK 10.0.26100.0。CMake 最低版本为 3.21，原生工程默认 v145，可通过 MSBuild PlatformToolset 属性切换 v143。GitHub Actions 使用 Windows 托管环境，独立构建原生项目与 CMake 目标；执行记录以各仓库 Actions 页为准。

PEViewer 自动验证范围为编译和链接；窗口交互、显示内容及进程枚举未加入自动化 GUI 测试。现有学习代码的转换、资源类型等编译告警仍保留，详见接口文档中的限制。
