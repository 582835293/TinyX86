# 项目关系与 Git 子模块（Submodule）保姆级同步指南

这些项目来自滴水逆向学习练习。每个应用独立发布；共享库（`Core-x86-Disassembler` 与 `PETools`）保留独立 Git 历史，通过 `deps/` 下的 **Git 子模块（Submodule）** 固定依赖版本。

| 仓库 | 职责 |
| --- | --- |
| [TinyX86](https://github.com/582835293/TinyX86) | CPU 模拟器、加载器与交互调试器（父仓库） |
| [Core-x86-Disassembler](https://github.com/582835293/Core-x86-Disassembler) | x86 指令解码静态库与命令行演示（子模块：`deps/Core-x86-Disassembler`） |
| [PETools](https://github.com/582835293/PETools) | PE32 解析和编辑静态库（子模块：`deps/PETools`） |
| [JiaquanPEViewerV1.0](https://github.com/582835293/JiaquanPEViewerV1.0) | Win32 API 图形界面练习与 PE 查看器 |

---

## 核心概念：先花 1 分钟理解 Git 子模块是怎么工作的

很多刚接触 Git Submodule 的同学会疑惑：**“为什么我在 GitHub 上更新了 `Core-x86-Disassembler` 仓库，但 `TinyX86` 里却没有自动变新？”**

原因非常简单：
- `TinyX86`（父仓库）并**不会**直接把 `Core-x86-Disassembler` 的文件存进自己的仓库里，也不跟踪它的分支，而是只记录了一个 **固定的 Commit ID（提交哈希号，相当于书签）**。
- 这样做的好处是：哪怕以后你把 `Core-x86-Disassembler` 改坏了，当前版本的 `TinyX86` 依然锁死在之前测试通过的那个 Commit 上，永远不会莫名其妙编译失败。
- 因此，当你更新了 `Core-x86-Disassembler` 之后，只需要在 `TinyX86` 里 **“把子模块拉到最新 Commit → 在父仓库里保存这个新书签并推送”** 即可！

---

## 场景一：你在别的目录（或 GitHub 上）更新了 `Core-x86-Disassembler`，怎么同步到現在の `TinyX86`？

假设你已经在 `Core-x86-Disassembler` 自己的仓库里改好了代码并 `git push` 到了 GitHub 的 `main` 分支。现在打开终端，进入 `TinyX86` 根目录，按顺序执行以下 **4 条命令**：

```bash
# 第 1 步：确保 deps/ 下的子模块已经初始化（如果以前拉取过，这步只是保险检查）
git submodule update --init --recursive

# 第 2 步：让 deps/Core-x86-Disassembler 去 GitHub 拉取它的最新 main 分支代码
git -C deps/Core-x86-Disassembler fetch origin
git -C deps/Core-x86-Disassembler checkout origin/main

# 第 3 步：重新编译并跑一下测试，确认新版反汇编引擎和 TinyX86 完全兼容
cmake -S . -B build
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure

# 第 4 步：在 TinyX86 父仓库里记录新的子模块版本号（书签），并推送到 GitHub！
git add deps/Core-x86-Disassembler
git commit -m "chore: sync Core-x86-Disassembler to latest version"
git push
```

> **懒人快捷命令**：第 2 步也可以简化成一句命令：
> ```bash
> git submodule update --remote --merge deps/Core-x86-Disassembler
> ```
> 执行完后再运行第 3 步（编译测试）和第 4 步（`git add` -> `git commit` -> `git push`）即可。

---

## 场景二：你想直接在 `TinyX86/deps/Core-x86-Disassembler` 文件夹里边调试边改代码

如果你在写 `TinyX86` 时发现解码器有个小 Bug，想直接改 `deps/Core-x86-Disassembler` 里的文件，**千万注意先切到 `main` 分支再改**（因为子模块默认处于“游离头指针 detached HEAD”状态）：

```bash
# 1. 进入子模块目录并切换到 main 分支（同时拉取最新代码）
cd deps/Core-x86-Disassembler
git checkout main
git pull

# 2. 修改 disasm.c / disasm.h 代码后，先在子模块里提交并推送到 Core-x86-Disassembler 仓库
git add .
git commit -m "fix: support new opcode in disassembler"
git push origin main

# 3. 退回 TinyX86 根目录，把新的子模块书签提交到 TinyX86 仓库
cd ../..
git add deps/Core-x86-Disassembler
git commit -m "chore: update Core-x86-Disassembler submodule"
git push
```

> **重要铁律**：如果在 `deps/` 里改了代码，**一定要先 `push` 子模块，再 `push` 父仓库 `TinyX86`**。如果只推了父仓库而忘了推子模块，别人（或 GitHub Actions CI）克隆 `TinyX86` 时就会报错“找不到对应的子模块提交号”。

---

## 场景三：换了一台新电脑，怎么完整下载 `TinyX86` 和所有子模块？

- **情况 A：还没克隆**
  加上 `--recurse-submodules` 参数，一条命令把主仓库和 `deps/` 里的两个库全部拉下来：
  ```bash
  git clone --recurse-submodules https://github.com/582835293/TinyX86.git
  ```
- **情况 B：已经普通 `git clone` 下来了，发现 `deps/` 文件夹是空的**
  在 `TinyX86` 根目录下运行：
  ```bash
  git submodule update --init --recursive
  ```

---

## 附：关于内置跨平台 `include/disasm.h` 与 `core/disasm.c` 的说明

为了保证在没有初始化子模块、或者在 Linux / macOS 64 位环境下也能开箱编译，`TinyX86` 在 `include/disasm.h` 和 `core/disasm.c` 中保留了一份内置的跨平台解码实现：
- 当你在 **32 位 Windows** 下且拉取了 `deps/Core-x86-Disassembler` 子模块时，CMake（`TINYX86_USE_SUBMODULES=ON`）会**自动优先使用 `deps/Core-x86-Disassembler` 子模块**。
- 如果你修改了 `Core-x86-Disassembler` 中的 `DecodeContext` 结构体字段或新增了指令，也可以同步更新 `include/disasm.h` 与 `core/disasm.c`，使 Linux / macOS 跨平台构建同步受益。
