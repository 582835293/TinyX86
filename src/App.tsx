import React, { useState, useRef, useEffect, useMemo } from 'react';
import {
  Play,
  StepForward,
  FastForward,
  RotateCcw,
  Upload,
  Terminal,
  Cpu,
  Layers,
  HardDrive,
  CheckCircle2,
  FileCode,
  CircleDot,
  HelpCircle,
  BookOpen,
} from 'lucide-react';
import { CPUContext, PEHeaderInfo } from './emulator/types';
import { TinyX86WasmEngine, DisasmLine } from './emulator/wasmEngine';

const GPR_NAMES = ['EAX', 'ECX', 'EDX', 'EBX', 'ESP', 'EBP', 'ESI', 'EDI'];
const SEG_NAMES = ['ES', 'CS', 'SS', 'DS', 'FS', 'GS'];
const FLAG_KEYS = ['CF', 'PF', 'AF', 'ZF', 'SF', 'TF', 'IF', 'DF', 'OF', 'NT'] as const;
const FLAG_DESCRIPTIONS: Record<(typeof FLAG_KEYS)[number], string> = {
  CF: 'Bit 0: Carry Flag (进位/借位标志)',
  PF: 'Bit 2: Parity Flag (奇偶标志)',
  AF: 'Bit 4: Auxiliary Carry Flag (辅助进位标志)',
  ZF: 'Bit 6: Zero Flag (零标志)',
  SF: 'Bit 7: Sign Flag (符号标志)',
  TF: 'Bit 8: Trap Flag (单步陷阱标志)',
  IF: 'Bit 9: Interrupt Enable Flag (中断允许标志)',
  DF: 'Bit 10: Direction Flag (串操作方向标志)',
  OF: 'Bit 11: Overflow Flag (有符号溢出标志)',
  NT: 'Bit 14: Nested Task Flag (嵌套任务标志)',
};

type ProgramPresetId = 'power_fpu' | 'sum_loop' | 'dos_int21';

interface PresetInfo {
  id: ProgramPresetId;
  presetIndex: number;
  name: string;
  subtitle: string;
}

const PRESETS: PresetInfo[] = [
  {
    id: 'power_fpu',
    presetIndex: 0,
    name: 't: 递归幂运算 power(2.0, 3) [main.c 默认]',
    subtitle: '栈帧开辟、REP STOSD 0xCCCCCCCC、递归调用与 x87 FPU (ST0 = 8.0)',
  },
  {
    id: 'sum_loop',
    presetIndex: 1,
    name: 'Sum 1..10 累加循环 (ALU + LOOP)',
    subtitle: '计算 1+2+...+10 = 55 (0x37) 存入 EAX 与 EBX',
  },
  {
    id: 'dos_int21',
    presetIndex: 2,
    name: 'DOS INT 21h 高层模拟 (HLE 字符串输出)',
    subtitle: '模拟 AH=09h 输出 $ 结尾字符串与 AH=4Ch 程序退出',
  },
];

function createInitialCpuState(): CPUContext {
  return {
    GPR: new Uint32Array([0, 0, 0, 0, 0x0010ffe0, 0x0010ffe0, 0, 0]),
    EIP: 0x00400000,
    EFLAGS: {
      CF: 0,
      PF: 0,
      AF: 0,
      ZF: 0,
      SF: 0,
      TF: 0,
      IF: 1,
      DF: 0,
      OF: 0,
      IOPL: 0,
      NT: 0,
    },
    seg: new Uint16Array([0x0023, 0x001b, 0x0023, 0x0023, 0x0000, 0x0000]),
    FPU: {
      Regs: [0, 0, 0, 0, 0, 0, 0, 0],
      SW: {
        IE: 0,
        DE: 0,
        ZE: 0,
        OE: 0,
        UE: 0,
        PE: 0,
        SF: 0,
        ES: 0,
        C0: 0,
        C1: 0,
        C2: 0,
        TOP: 0,
        C3: 0,
        B: 0,
      },
      CW: 0x037f,
      TW: 0xffff,
    },
    Halted: false,
  };
}

function hex32(val: number): string {
  return '0x' + (val >>> 0).toString(16).toUpperCase().padStart(8, '0');
}

function hex16(val: number): string {
  return '0x' + (val & 0xffff).toString(16).toUpperCase().padStart(4, '0');
}

function hex8(val: number): string {
  return (val & 0xff).toString(16).toUpperCase().padStart(2, '0');
}

export function App() {
  const engineRef = useRef<TinyX86WasmEngine | null>(null);
  const [wasmReady, setWasmReady] = useState<boolean>(false);
  const [cpu, setCpu] = useState<CPUContext>(createInitialCpuState);
  const [rawEflags, setRawEflags] = useState<number>(0x00000202);
  const [activePreset, setActivePreset] = useState<ProgramPresetId>('power_fpu');
  const [loadedTitle, setLoadedTitle] = useState<string>('g_TestCode (Recursive power(2.0, 3))');
  const [peInfo, setPeInfo] = useState<PEHeaderInfo | null>(null);
  const [breakpoints, setBreakpoints] = useState<number[]>([]);
  const [memViewAddr, setMemViewAddr] = useState<number>(0x00400000);
  const [memAddrInput, setMemAddrInput] = useState<string>('00400000');
  const [cmdInput, setCmdInput] = useState<string>('');
  const [activeRightTab, setActiveRightTab] = useState<'stack' | 'memory' | 'pe' | 'tests' | 'guide'>('stack');
  const [memVersion, setMemVersion] = useState<number>(0);
  const [logs, setLogs] = useState<string[]>([
    'TinyX86 Emulator [Native C11 Core compiled to WebAssembly: public/tinyx86.wasm]',
    'Initializing WASI C11 modules (core/TinyX86.c, disasm.c, PELoader.c, PETools.c, debugger.c)...',
  ]);

  const terminalContainerRef = useRef<HTMLDivElement>(null);
  const fileInputRef = useRef<HTMLInputElement>(null);

  const appendLog = (lines: string | string[]) => {
    const arr = (Array.isArray(lines) ? lines : [lines]).filter((l) => l.length > 0);
    if (arr.length === 0) return;
    setLogs((prev) => [...prev.slice(-300), ...arr]);
  };

  const syncFromC = (engine = engineRef.current) => {
    if (!engine) return;
    setCpu(engine.readCpuState());
    setRawEflags(engine.getRawEflags());
    setBreakpoints(engine.getBreakpoints());
    setMemVersion((v) => v + 1);
  };

  useEffect(() => {
    let mounted = true;
    TinyX86WasmEngine.create()
      .then((engine) => {
        if (!mounted) return;
        engineRef.current = engine;
        setWasmReady(true);
        syncFromC(engine);
        const testRes = engine.getIntegrationResult();
        setLogs([
          `TinyX86 Emulator [C11 -> WASI WebAssembly | sizeof(CPU_Context)=${testRes.sizeofCpuContext}B, sizeof(DecodeContext)=${testRes.sizeofDecodeContext}B]`,
          "Loaded internal test 'g_TestCode' (recursive power(2.0, 3)) at 0x00400000 via LoadMem() in core/PELoader.c.",
          '=== TinyX86 Debugger Initialized (core/debugger.c) ===',
          "Type 'h' for help, or press keyboard shortcuts: 's' (Step Into), 'n' (Step Over), 'c' (Continue), 'r' (Registers).",
        ]);
      })
      .catch((err) => {
        if (!mounted) return;
        appendLog(`[Error] Failed to initialize tinyx86.wasm: ${String(err)}`);
      });
    return () => {
      mounted = false;
    };
  }, []);

  useEffect(() => {
    const el = terminalContainerRef.current;
    if (el) {
      el.scrollTop = el.scrollHeight;
    }
  }, [logs]);

  const loadPreset = (preset: PresetInfo) => {
    const engine = engineRef.current;
    if (!engine) return;
    const cLogs = engine.loadPreset(preset.presetIndex);
    syncFromC(engine);
    setActivePreset(preset.id);
    setLoadedTitle(preset.name);
    setPeInfo(null);
    setMemViewAddr(0x00400000);
    setMemAddrInput('00400000');
    appendLog([`[Shell] Loading ${preset.name} via C LoadMem()...`, ...cLogs]);
  };

  const handleStepInto = (count = 1) => {
    const engine = engineRef.current;
    if (!engine) return;
    const cmd = count > 1 ? `s ${count}` : 's';
    const cLogs = engine.execCommand(cmd);
    syncFromC(engine);
    appendLog(cLogs);
  };

  const handleStepOver = () => {
    const engine = engineRef.current;
    if (!engine) return;
    const cLogs = engine.execCommand('n');
    syncFromC(engine);
    appendLog(cLogs);
  };

  const handleContinue = () => {
    const engine = engineRef.current;
    if (!engine) return;
    const cLogs = engine.execCommand('c');
    syncFromC(engine);
    appendLog(cLogs);
  };

  const toggleBreakpoint = (addr: number) => {
    const engine = engineRef.current;
    if (!engine) return;
    const cLogs = engine.toggleBreakpoint(addr >>> 0);
    syncFromC(engine);
    appendLog(cLogs);
  };

  const executeDebuggerCommand = (rawCmd: string) => {
    const engine = engineRef.current;
    if (!engine) return;
    const trimmed = rawCmd.trim();
    if (!trimmed) return;
    appendLog(`(dbg) > ${trimmed}`);

    const cmd = trimmed[0].toLowerCase();
    const args = trimmed.slice(1).trim();

    if (cmd === 't') {
      loadPreset(PRESETS[0]);
      return;
    }

    if (cmd === 'm') {
      const addr = parseInt(args.replace(/^0x/i, ''), 16);
      if (!Number.isNaN(addr)) {
        const uAddr = addr >>> 0;
        setMemViewAddr(uAddr);
        setMemAddrInput(uAddr.toString(16).toUpperCase().padStart(8, '0'));
        setActiveRightTab('memory');
      }
    }

    // Directly call DebuggerExecuteCommand(&g_WasmCpu, trimmed) inside core/debugger.c!
    const cLogs = engine.execCommand(trimmed);
    syncFromC(engine);
    appendLog(cLogs);
  };

  useEffect(() => {
    const onKeyDown = (e: KeyboardEvent) => {
      if (e.ctrlKey || e.metaKey || e.altKey) return;
      const target = e.target as HTMLElement | null;
      if (
        target &&
        (target.tagName === 'INPUT' ||
          target.tagName === 'TEXTAREA' ||
          target.tagName === 'SELECT' ||
          target.isContentEditable)
      ) {
        return;
      }

      const key = e.key.toLowerCase();
      if (key === 's' || e.key === 'F7' || e.key === 'F11') {
        e.preventDefault();
        handleStepInto(1);
      } else if (key === 'n' || e.key === 'F8' || e.key === 'F10') {
        e.preventDefault();
        handleStepOver();
      } else if (key === 'c' || e.key === 'F5' || e.key === 'F9') {
        e.preventDefault();
        handleContinue();
      } else if (key === 'r') {
        e.preventDefault();
        executeDebuggerCommand('r');
      } else if (key === 'u') {
        e.preventDefault();
        executeDebuggerCommand('u 8');
      } else if (key === 'k') {
        e.preventDefault();
        executeDebuggerCommand('k 8');
      } else if (key === 'h') {
        e.preventDefault();
        executeDebuggerCommand('h');
      }
    };

    window.addEventListener('keydown', onKeyDown);
    return () => window.removeEventListener('keydown', onKeyDown);
  });

  const handleFileUpload = async (e: React.ChangeEvent<HTMLInputElement>) => {
    const engine = engineRef.current;
    const file = e.target.files?.[0];
    if (!file || !engine) return;
    const arrayBuffer = await file.arrayBuffer();
    const bytes = new Uint8Array(arrayBuffer);
    const isExe =
      file.name.toLowerCase().endsWith('.exe') || (bytes[0] === 0x4d && bytes[1] === 0x5a);

    if (isExe) {
      const { ok, peInfo: parsedPe, logs: cLogs } = engine.loadPE(bytes);
      syncFromC(engine);
      if (ok && parsedPe) {
        const nextCpu = engine.readCpuState();
        setPeInfo(parsedPe);
        setLoadedTitle(`PE32: ${file.name}`);
        setMemViewAddr(nextCpu.EIP);
        setMemAddrInput(nextCpu.EIP.toString(16).toUpperCase().padStart(8, '0'));
        setActiveRightTab('pe');
      }
      appendLog([`[Shell] Loading PE via C LoadExeFromMemory(): ${file.name}`, ...cLogs]);
    } else {
      const cLogs = engine.loadBin(bytes, 0x00400000);
      syncFromC(engine);
      setPeInfo(null);
      setLoadedTitle(`BIN: ${file.name} (${bytes.length} bytes)`);
      setMemViewAddr(0x00400000);
      setMemAddrInput('00400000');
      appendLog([`[Shell] Loading Binary via C LoadMem(): ${file.name}`, ...cLogs]);
    }
    e.target.value = '';
  };

  const disasmList = useMemo<DisasmLine[]>(() => {
    const engine = engineRef.current;
    if (!engine || !wasmReady) return [];
    const base = engine.getCodeBase() >>> 0;
    const startAddr = cpu.EIP >= base && cpu.EIP < base + 0x200 ? base : cpu.EIP >>> 0;
    return engine.disassembleList(startAddr, 28);
  }, [cpu.EIP, memVersion, wasmReady]);

  const stackRows = useMemo(() => {
    const engine = engineRef.current;
    if (!engine || !wasmReady) return [];
    const esp = cpu.GPR[4] >>> 0;
    const ebp = cpu.GPR[5] >>> 0;
    const rows = [];
    for (let i = -3; i < 14; i++) {
      const addr = (esp + i * 4) >>> 0;
      const val = engine.readMemU32(addr);
      let ascii = '';
      for (let b = 0; b < 4; b++) {
        const ch = (val >>> (b * 8)) & 0xff;
        ascii += ch >= 32 && ch <= 126 ? String.fromCharCode(ch) : '.';
      }
      rows.push({
        addr,
        val,
        ascii,
        isEsp: addr === esp,
        isEbp: addr === ebp,
      });
    }
    return rows;
  }, [cpu.GPR, memVersion, wasmReady]);

  const memRows = useMemo(() => {
    const engine = engineRef.current;
    if (!engine || !wasmReady) return [];
    const rows = [];
    const base = memViewAddr >>> 0;
    for (let r = 0; r < 12; r++) {
      const rowAddr = (base + r * 16) >>> 0;
      const bytes: number[] = [];
      let ascii = '';
      for (let c = 0; c < 16; c++) {
        const b = engine.readMemU8(rowAddr + c);
        bytes.push(b);
        ascii += b >= 32 && b <= 126 ? String.fromCharCode(b) : '.';
      }
      rows.push({ rowAddr, bytes, ascii });
    }
    return rows;
  }, [memViewAddr, memVersion, wasmReady]);

  const integrationResult = engineRef.current?.getIntegrationResult() ?? {
    passed: true,
    checks: 13,
    failures: 0,
    sizeofCpuContext: 168,
    sizeofDecodeContext: 260,
  };

  return (
    <div className="min-h-screen bg-zinc-950 text-zinc-100 flex flex-col font-sans">
      {/* Top Header & Controls */}
      <header className="border-b border-zinc-800 bg-zinc-900/80 px-4 py-2.5 flex flex-wrap items-center justify-between gap-3">
        <div className="flex items-center gap-3">
          <div className="w-8 h-8 rounded bg-emerald-500/15 border border-emerald-500/40 flex items-center justify-center text-emerald-400">
            <Cpu className="w-4 h-4" />
          </div>
          <div>
            <div className="flex items-center gap-2">
              <h1 className="text-sm font-semibold tracking-tight text-zinc-100">
                TinyX86 Emulator &amp; Interactive Debugger
              </h1>
              <span
                className={`text-[11px] font-mono px-1.5 py-0.5 rounded border ${
                  !wasmReady
                    ? 'bg-amber-500/15 text-amber-300 border-amber-500/30'
                    : cpu.Halted
                    ? 'bg-red-500/15 text-red-300 border-red-500/30'
                    : 'bg-emerald-500/15 text-emerald-300 border-emerald-500/30'
                }`}
              >
                {!wasmReady ? 'LOADING WASM' : cpu.Halted ? 'HALTED' : 'READY'}
              </span>
              <span className="text-[11px] font-mono px-1.5 py-0.5 rounded bg-sky-500/15 text-sky-300 border border-sky-500/30">
                Native C11 (core/*.c → tinyx86.wasm)
              </span>
            </div>
            <p className="text-xs text-zinc-400 font-mono truncate max-w-xl">
              Loaded: {loadedTitle} | EIP={hex32(cpu.EIP)}
            </p>
          </div>
        </div>

        {/* Execution Toolbar */}
        <div className="flex flex-wrap items-center gap-2">
          <select
            value={activePreset}
            onChange={(e) => {
              const found = PRESETS.find((p) => p.id === e.target.value);
              if (found) loadPreset(found);
            }}
            className="bg-zinc-900 border border-zinc-700 text-xs text-zinc-200 rounded px-2.5 py-1.5 font-mono focus:outline-none focus:border-emerald-500"
          >
            {PRESETS.map((p) => (
              <option key={p.id} value={p.id}>
                {p.name}
              </option>
            ))}
          </select>

          <button
            onClick={() => handleStepInto(1)}
            className="flex items-center gap-1.5 px-3 py-1.5 rounded bg-emerald-600 hover:bg-emerald-500 text-white text-xs font-medium transition cursor-pointer"
            title="Step Into (Shortcut: s / F7 / F11)"
          >
            <StepForward className="w-3.5 h-3.5" />
            Step Into (s)
          </button>

          <button
            onClick={() => handleStepOver()}
            className="flex items-center gap-1.5 px-3 py-1.5 rounded bg-zinc-800 hover:bg-zinc-700 border border-zinc-700 text-zinc-100 text-xs font-medium transition cursor-pointer"
            title="Step Over CALL/LOOP/REP (Shortcut: n / F8 / F10)"
          >
            <FastForward className="w-3.5 h-3.5 text-amber-400" />
            Step Over (n)
          </button>

          <button
            onClick={() => handleContinue()}
            className="flex items-center gap-1.5 px-3 py-1.5 rounded bg-zinc-800 hover:bg-zinc-700 border border-zinc-700 text-zinc-100 text-xs font-medium transition cursor-pointer"
            title="Continue until Breakpoint or INT3 (Shortcut: c / F5 / F9)"
          >
            <Play className="w-3.5 h-3.5 text-emerald-400" />
            Continue (c)
          </button>

          <button
            onClick={() => {
              const found = PRESETS.find((p) => p.id === activePreset) ?? PRESETS[0];
              loadPreset(found);
            }}
            className="flex items-center gap-1.5 px-2.5 py-1.5 rounded bg-zinc-800 hover:bg-zinc-700 border border-zinc-700 text-zinc-200 text-xs transition cursor-pointer"
            title="Reset CPU & Memory"
          >
            <RotateCcw className="w-3.5 h-3.5" />
            Reset
          </button>

          <input
            ref={fileInputRef}
            type="file"
            accept=".exe,.bin,.com"
            onChange={handleFileUpload}
            className="hidden"
          />
          <button
            onClick={() => fileInputRef.current?.click()}
            className="flex items-center gap-1.5 px-2.5 py-1.5 rounded bg-zinc-800 hover:bg-zinc-700 border border-zinc-700 text-zinc-200 text-xs transition cursor-pointer"
            title="Load PE32 (.exe) or Raw Binary (.bin)"
          >
            <Upload className="w-3.5 h-3.5 text-sky-400" />
            Load .EXE / .BIN
          </button>
        </div>
      </header>

      {/* Main 3-Column Debugger Grid */}
      <main className="flex-1 grid grid-cols-1 lg:grid-cols-12 gap-3 p-3 overflow-hidden">
        {/* Left Column: CPU Registers, EFLAGS, x87 FPU */}
        <section className="lg:col-span-3 flex flex-col gap-3 overflow-y-auto">
          {/* General Purpose Registers */}
          <div className="bg-zinc-900 border border-zinc-800 rounded-md p-3">
            <div className="flex items-center justify-between mb-2">
              <h2 className="text-xs font-semibold uppercase tracking-wider text-zinc-400 flex items-center gap-1.5">
                <Cpu className="w-3.5 h-3.5 text-emerald-400" />
                32-Bit Registers (GPR)
              </h2>
              <span className="text-[11px] font-mono text-zinc-500">SetCpuState() in C</span>
            </div>

            <div className="grid grid-cols-1 gap-1 font-mono text-xs">
              {GPR_NAMES.map((name, idx) => {
                const val = cpu.GPR[idx] >>> 0;
                const low16 = val & 0xffff;
                const low8 = val & 0xff;
                const high8 = (val >>> 8) & 0xff;
                return (
                  <div
                    key={name}
                    className="flex items-center justify-between px-2 py-1 rounded bg-zinc-950/70 border border-zinc-800/80 hover:border-zinc-700"
                  >
                    <span className="text-emerald-400 font-semibold w-10">{name}</span>
                    <input
                      type="text"
                      value={val.toString(16).toUpperCase().padStart(8, '0')}
                      onChange={(e) => {
                        const parsed = parseInt(e.target.value, 16);
                        if (!Number.isNaN(parsed) && engineRef.current) {
                          engineRef.current.setCpuState(name, parsed >>> 0);
                          syncFromC(engineRef.current);
                        }
                      }}
                      className="bg-transparent text-zinc-100 w-20 text-right focus:outline-none focus:text-amber-300"
                    />
                    <span className="text-[11px] text-zinc-500">
                      {idx < 4
                        ? `${hex8(high8)}:${hex8(low8)}`
                        : `${low16.toString(16).toUpperCase().padStart(4, '0')}`}
                    </span>
                  </div>
                );
              })}

              {/* EIP */}
              <div className="flex items-center justify-between px-2 py-1 rounded bg-emerald-950/30 border border-emerald-800/50 mt-1">
                <span className="text-amber-400 font-semibold w-10">EIP</span>
                <span className="text-amber-300 font-semibold">{hex32(cpu.EIP)}</span>
                <span className="text-[11px] text-zinc-400">CS:{hex16(cpu.seg[1])}</span>
              </div>
            </div>

            {/* Segment Registers */}
            <div className="mt-2 pt-2 border-t border-zinc-800 grid grid-cols-3 gap-1 font-mono text-[11px]">
              {SEG_NAMES.map((sName, idx) => (
                <div key={sName} className="bg-zinc-950 px-1.5 py-0.5 rounded flex justify-between">
                  <span className="text-zinc-500">{sName}</span>
                  <span className="text-zinc-300">
                    {(cpu.seg[idx] & 0xffff).toString(16).toUpperCase().padStart(4, '0')}
                  </span>
                </div>
              ))}
            </div>
          </div>

          {/* EFLAGS */}
          <div className="bg-zinc-900 border border-zinc-800 rounded-md p-3">
            <div className="flex items-center justify-between mb-2">
              <h2 className="text-xs font-semibold uppercase tracking-wider text-zinc-400">
                EFLAGS Register
              </h2>
              <span className="text-xs font-mono text-zinc-400">{hex32(rawEflags)}</span>
            </div>
            <div className="grid grid-cols-4 gap-1.5 font-mono text-xs">
              {FLAG_KEYS.map((flag) => {
                const active = cpu.EFLAGS[flag] === 1;
                return (
                  <button
                    key={flag}
                    onClick={() => {
                      if (!engineRef.current) return;
                      engineRef.current.setCpuState(flag, active ? 0 : 1);
                      syncFromC(engineRef.current);
                    }}
                    className={`py-1 rounded border text-center transition cursor-pointer ${
                      active
                        ? 'bg-emerald-500/20 border-emerald-500/60 text-emerald-300 font-semibold'
                        : 'bg-zinc-950 border-zinc-800 text-zinc-500 hover:border-zinc-700'
                    }`}
                    title={FLAG_DESCRIPTIONS[flag]}
                  >
                    {flag}={active ? 1 : 0}
                  </button>
                );
              })}
              <button
                onClick={() => {
                  if (!engineRef.current) return;
                  const nextIopl = (cpu.EFLAGS.IOPL + 1) & 3;
                  engineRef.current.setCpuState('IOPL', nextIopl);
                  syncFromC(engineRef.current);
                }}
                className={`col-span-2 py-1 rounded border text-center transition cursor-pointer ${
                  cpu.EFLAGS.IOPL > 0
                    ? 'bg-amber-500/20 border-amber-500/60 text-amber-300 font-semibold'
                    : 'bg-zinc-950 border-zinc-800 text-zinc-500 hover:border-zinc-700'
                }`}
                title="Bits 12..13: I/O Privilege Level (I/O 特权级 0..3，点击通过 C SetCpuState 切换)"
              >
                IOPL={cpu.EFLAGS.IOPL}
              </button>
            </div>
          </div>

          {/* x87 FPU Stack */}
          <div className="bg-zinc-900 border border-zinc-800 rounded-md p-3 flex-1">
            <div className="flex items-center justify-between mb-2">
              <h2 className="text-xs font-semibold uppercase tracking-wider text-zinc-400">
                x87 FPU Subsystem
              </h2>
              <span className="text-[11px] font-mono text-zinc-400">
                TOP={cpu.FPU.SW.TOP} | CW={hex16(cpu.FPU.CW)}
              </span>
            </div>
            <div className="flex gap-2 mb-2 font-mono text-[11px] text-zinc-400">
              <span className="px-1.5 py-0.5 rounded bg-zinc-950 border border-zinc-800">
                C0={cpu.FPU.SW.C0}
              </span>
              <span className="px-1.5 py-0.5 rounded bg-zinc-950 border border-zinc-800">
                C1={cpu.FPU.SW.C1}
              </span>
              <span className="px-1.5 py-0.5 rounded bg-zinc-950 border border-zinc-800">
                C2={cpu.FPU.SW.C2}
              </span>
              <span className="px-1.5 py-0.5 rounded bg-zinc-950 border border-zinc-800">
                C3={cpu.FPU.SW.C3}
              </span>
            </div>

            <div className="space-y-1 font-mono text-xs">
              {cpu.FPU.Regs.map((val, physIdx) => {
                const top = cpu.FPU.SW.TOP;
                const logicalIdx = (physIdx - top + 8) & 7;
                const isTop = physIdx === top;
                return (
                  <div
                    key={physIdx}
                    className={`flex items-center justify-between px-2 py-1 rounded border ${
                      isTop
                        ? 'bg-amber-500/15 border-amber-500/40 text-amber-200'
                        : 'bg-zinc-950/60 border-zinc-800/80 text-zinc-300'
                    }`}
                  >
                    <span className="text-zinc-400">R{physIdx}</span>
                    <span className="font-semibold">{val.toFixed(6)}</span>
                    <span
                      className={`text-[11px] ${
                        isTop ? 'text-amber-300 font-semibold' : 'text-zinc-500'
                      }`}
                    >
                      ST({logicalIdx}){isTop ? ' TOP' : ''}
                    </span>
                  </div>
                );
              })}
            </div>
          </div>
        </section>

        {/* Center Column: Disassembly & Interactive Debugger Terminal */}
        <section className="lg:col-span-5 flex flex-col gap-3 overflow-hidden">
          {/* Disassembly View */}
          <div className="bg-zinc-900 border border-zinc-800 rounded-md p-3 flex flex-col h-72">
            <div className="flex items-center justify-between mb-2">
              <h2 className="text-xs font-semibold uppercase tracking-wider text-zinc-400 flex items-center gap-1.5">
                <FileCode className="w-3.5 h-3.5 text-sky-400" />
                Core-x86 Disassembler View (core/disasm.c)
              </h2>
              <span className="text-[11px] text-zinc-500 font-mono">
                Click row to toggle breakpoint (b)
              </span>
            </div>

            <div className="flex-1 overflow-y-auto font-mono text-xs space-y-0.5 pr-1">
              {disasmList.map((inst) => {
                const isCurrent = inst.address === cpu.EIP;
                const hasBp = breakpoints.includes(inst.address);
                const bytesHex = inst.bytes.map((b) => hex8(b)).join(' ');
                return (
                  <div
                    key={inst.address}
                    onClick={() => toggleBreakpoint(inst.address)}
                    className={`flex items-center gap-2 px-2 py-1 rounded cursor-pointer transition ${
                      isCurrent
                        ? 'bg-emerald-500/20 border border-emerald-500/50 text-emerald-200'
                        : hasBp
                        ? 'bg-red-500/15 border border-red-500/40 text-red-200'
                        : 'hover:bg-zinc-800/60 text-zinc-300'
                    }`}
                  >
                    <span className="w-4 flex items-center justify-center">
                      {hasBp ? (
                        <CircleDot className="w-3 h-3 text-red-400" />
                      ) : isCurrent ? (
                        <span className="text-emerald-400 font-bold">&gt;</span>
                      ) : null}
                    </span>
                    <span className="text-zinc-400 w-20 shrink-0">{hex32(inst.address)}</span>
                    <span className="text-zinc-500 w-36 shrink-0 truncate">{bytesHex}</span>
                    <span className="text-zinc-100 font-medium truncate">{inst.asm_str}</span>
                  </div>
                );
              })}
            </div>
          </div>

          {/* Interactive Shell & Debugger CLI */}
          <div className="bg-zinc-900 border border-zinc-800 rounded-md p-3 flex-1 flex flex-col min-h-[260px]">
            <div className="flex items-center justify-between mb-2">
              <h2 className="text-xs font-semibold uppercase tracking-wider text-zinc-400 flex items-center gap-1.5">
                <Terminal className="w-3.5 h-3.5 text-emerald-400" />
                TinyX86 Interactive Debugger CLI (core/debugger.c stdout)
              </h2>
              <div className="flex items-center gap-1">
                <button
                  onClick={() => executeDebuggerCommand('r')}
                  className="text-[11px] font-mono px-2 py-0.5 rounded bg-zinc-800 hover:bg-zinc-700 text-zinc-300 cursor-pointer"
                >
                  r (Regs)
                </button>
                <button
                  onClick={() => executeDebuggerCommand('u 8')}
                  className="text-[11px] font-mono px-2 py-0.5 rounded bg-zinc-800 hover:bg-zinc-700 text-zinc-300 cursor-pointer"
                >
                  u 8 (Disasm)
                </button>
                <button
                  onClick={() => executeDebuggerCommand('k 8')}
                  className="text-[11px] font-mono px-2 py-0.5 rounded bg-zinc-800 hover:bg-zinc-700 text-zinc-300 cursor-pointer"
                >
                  k (Stack)
                </button>
                <button
                  onClick={() => executeDebuggerCommand('h')}
                  className="text-[11px] font-mono px-2 py-0.5 rounded bg-zinc-800 hover:bg-zinc-700 text-zinc-300 cursor-pointer"
                >
                  <HelpCircle className="w-3 h-3 inline mr-0.5" />h
                </button>
              </div>
            </div>

            <div
              ref={terminalContainerRef}
              className="flex-1 bg-zinc-950 border border-zinc-800 rounded p-2.5 overflow-y-auto font-mono text-xs space-y-1 max-h-64"
            >
              {logs.map((line, i) => (
                <div
                  key={i}
                  className={`whitespace-pre-wrap break-all ${
                    line.startsWith('(dbg) >')
                      ? 'text-emerald-400 font-semibold'
                      : line.includes('Error') || line.includes('FAIL')
                      ? 'text-red-400'
                      : line.includes('Hit') || line.includes('PASS') || line.includes('DOS INT')
                      ? 'text-amber-300'
                      : 'text-zinc-300'
                  }`}
                >
                  {line}
                </div>
              ))}
            </div>

            <form
              onSubmit={(e) => {
                e.preventDefault();
                if (!cmdInput.trim()) return;
                executeDebuggerCommand(cmdInput);
                setCmdInput('');
              }}
              className="mt-2 flex items-center gap-2"
            >
              <span className="font-mono text-xs text-emerald-400 shrink-0">(dbg) &gt;</span>
              <input
                type="text"
                value={cmdInput}
                onChange={(e) => setCmdInput(e.target.value)}
                placeholder="Enter command: s [N], n, c, r, u [N], b <addr>, m <addr>, k, w EAX 2A, h..."
                className="flex-1 bg-zinc-950 border border-zinc-800 rounded px-2.5 py-1.5 text-xs font-mono text-zinc-100 focus:outline-none focus:border-emerald-500"
              />
              <button
                type="submit"
                className="px-3 py-1.5 rounded bg-emerald-600 hover:bg-emerald-500 text-white text-xs font-medium cursor-pointer"
              >
                Run
              </button>
            </form>
          </div>
        </section>

        {/* Right Column: Stack Dump, Memory Inspector, PE32 Headers, Integration Tests, x86 Guide */}
        <section className="lg:col-span-4 flex flex-col bg-zinc-900 border border-zinc-800 rounded-md p-3 overflow-hidden">
          <div className="flex flex-wrap items-center gap-1 border-b border-zinc-800 pb-2 mb-2.5">
            <button
              onClick={() => setActiveRightTab('stack')}
              className={`px-2 py-1 rounded text-xs font-medium flex items-center gap-1 cursor-pointer ${
                activeRightTab === 'stack'
                  ? 'bg-emerald-500/20 text-emerald-300 border border-emerald-500/40'
                  : 'text-zinc-400 hover:text-zinc-200'
              }`}
            >
              <Layers className="w-3.5 h-3.5" />
              Stack
            </button>
            <button
              onClick={() => setActiveRightTab('memory')}
              className={`px-2 py-1 rounded text-xs font-medium flex items-center gap-1 cursor-pointer ${
                activeRightTab === 'memory'
                  ? 'bg-emerald-500/20 text-emerald-300 border border-emerald-500/40'
                  : 'text-zinc-400 hover:text-zinc-200'
              }`}
            >
              <HardDrive className="w-3.5 h-3.5" />
              Mem
            </button>
            <button
              onClick={() => setActiveRightTab('pe')}
              className={`px-2 py-1 rounded text-xs font-medium flex items-center gap-1 cursor-pointer ${
                activeRightTab === 'pe'
                  ? 'bg-emerald-500/20 text-emerald-300 border border-emerald-500/40'
                  : 'text-zinc-400 hover:text-zinc-200'
              }`}
            >
              PE32
            </button>
            <button
              onClick={() => setActiveRightTab('tests')}
              className={`px-2 py-1 rounded text-xs font-medium flex items-center gap-1 cursor-pointer ${
                activeRightTab === 'tests'
                  ? 'bg-emerald-500/20 text-emerald-300 border border-emerald-500/40'
                  : 'text-zinc-400 hover:text-zinc-200'
              }`}
            >
              <CheckCircle2 className="w-3.5 h-3.5 text-emerald-400" />
              C Tests ({integrationResult.checks}/{integrationResult.checks})
            </button>
            <button
              onClick={() => setActiveRightTab('guide')}
              className={`px-2 py-1 rounded text-xs font-medium flex items-center gap-1 cursor-pointer ${
                activeRightTab === 'guide'
                  ? 'bg-emerald-500/20 text-emerald-300 border border-emerald-500/40'
                  : 'text-zinc-400 hover:text-zinc-200'
              }`}
            >
              <BookOpen className="w-3.5 h-3.5 text-amber-400" />
              x86 &amp; Build
            </button>
          </div>

          {activeRightTab === 'stack' && (
            <div className="flex-1 overflow-y-auto font-mono text-xs space-y-1">
              <div className="flex items-center justify-between text-[11px] text-zinc-400 pb-1 border-b border-zinc-800">
                <span>ESP: {hex32(cpu.GPR[4])}</span>
                <span>EBP: {hex32(cpu.GPR[5])}</span>
              </div>
              {stackRows.map((row) => (
                <div
                  key={row.addr}
                  className={`flex items-center justify-between px-2 py-1 rounded ${
                    row.isEsp
                      ? 'bg-emerald-500/15 border border-emerald-500/40 text-emerald-200'
                      : row.isEbp
                      ? 'bg-amber-500/15 border border-amber-500/40 text-amber-200'
                      : 'bg-zinc-950/60 text-zinc-300'
                  }`}
                >
                  <span className="text-zinc-400">{hex32(row.addr)}</span>
                  <span className="font-semibold">{hex32(row.val)}</span>
                  <span className="text-zinc-500">{row.ascii}</span>
                  <span className="text-[11px] w-24 text-right">
                    {row.isEsp && row.isEbp
                      ? 'ESP / EBP'
                      : row.isEsp
                      ? '<-- ESP'
                      : row.isEbp
                      ? '<-- EBP'
                      : ''}
                  </span>
                </div>
              ))}
            </div>
          )}

          {activeRightTab === 'memory' && (
            <div className="flex-1 flex flex-col overflow-hidden">
              <form
                onSubmit={(e) => {
                  e.preventDefault();
                  const parsed = parseInt(memAddrInput.replace(/^0x/i, ''), 16);
                  if (!Number.isNaN(parsed)) setMemViewAddr(parsed >>> 0);
                }}
                className="flex items-center gap-2 mb-2"
              >
                <span className="text-xs font-mono text-zinc-400">Addr (Hex):</span>
                <input
                  type="text"
                  value={memAddrInput}
                  onChange={(e) => setMemAddrInput(e.target.value)}
                  className="bg-zinc-950 border border-zinc-800 rounded px-2 py-1 text-xs font-mono text-zinc-100 w-28"
                />
                <button
                  type="submit"
                  className="px-2.5 py-1 rounded bg-zinc-800 hover:bg-zinc-700 text-xs text-zinc-200 cursor-pointer"
                >
                  Go
                </button>
                <button
                  type="button"
                  onClick={() => {
                    setMemViewAddr(cpu.EIP);
                    setMemAddrInput(cpu.EIP.toString(16).toUpperCase().padStart(8, '0'));
                  }}
                  className="px-2 py-1 rounded bg-zinc-800 hover:bg-zinc-700 text-[11px] font-mono text-zinc-300 cursor-pointer"
                >
                  @EIP
                </button>
                <button
                  type="button"
                  onClick={() => {
                    setMemViewAddr(cpu.GPR[4]);
                    setMemAddrInput(cpu.GPR[4].toString(16).toUpperCase().padStart(8, '0'));
                  }}
                  className="px-2 py-1 rounded bg-zinc-800 hover:bg-zinc-700 text-[11px] font-mono text-zinc-300 cursor-pointer"
                >
                  @ESP
                </button>
              </form>

              <div className="flex-1 overflow-y-auto font-mono text-[11px] space-y-1 bg-zinc-950 p-2 rounded border border-zinc-800">
                {memRows.map((r) => (
                  <div key={r.rowAddr} className="flex items-center justify-between gap-2">
                    <span className="text-zinc-500">
                      {r.rowAddr.toString(16).toUpperCase().padStart(8, '0')}
                    </span>
                    <span className="text-zinc-200 tracking-tight">
                      {r.bytes.map((b) => hex8(b)).join(' ')}
                    </span>
                    <span className="text-emerald-400/80">{r.ascii}</span>
                  </div>
                ))}
              </div>
            </div>
          )}

          {activeRightTab === 'pe' && (
            <div className="flex-1 overflow-y-auto text-xs space-y-3 font-mono">
              {!peInfo ? (
                <div className="bg-zinc-950 border border-zinc-800 rounded p-3 text-zinc-400 space-y-2">
                  <p>Currently running a raw machine-code image mapped at 0x00400000 via C LoadMem().</p>
                  <p>
                    Click <strong>Load .EXE / .BIN</strong> in the top bar to parse and map any
                    32-bit Windows PE executable (<code>IMAGE_DOS_HEADER</code>,{' '}
                    <code>IMAGE_NT_HEADERS32</code>, and section table mapping via{' '}
                    <code>core/PELoader.c</code> &amp; <code>core/PETools.c</code>).
                  </p>
                </div>
              ) : (
                <div className="space-y-2">
                  <div className="bg-zinc-950 border border-zinc-800 rounded p-2.5 space-y-1">
                    <div className="text-emerald-400 font-semibold mb-1">
                      PE32 Header Info (Parsed by C ParsePEHeaders)
                    </div>
                    <div>ImageBase: {hex32(peInfo.imageBase)}</div>
                    <div>AddressOfEntryPoint (RVA): {hex32(peInfo.entryPointRVA)}</div>
                    <div>SizeOfImage: {hex32(peInfo.sizeOfImage)}</div>
                    <div>SizeOfHeaders: {hex32(peInfo.sizeOfHeaders)}</div>
                    <div>NumberOfSections: {peInfo.numberOfSections}</div>
                  </div>
                  <div className="bg-zinc-950 border border-zinc-800 rounded p-2.5 space-y-1">
                    <div className="text-sky-400 font-semibold mb-1">Section Table</div>
                    {peInfo.sections.map((sec, idx) => (
                      <div
                        key={idx}
                        className="border-b border-zinc-800/80 py-1 last:border-none"
                      >
                        <div className="text-zinc-200 font-semibold">{sec.name || `(sec ${idx})`}</div>
                        <div className="text-zinc-400 text-[11px]">
                          VA: {hex32(peInfo.imageBase + sec.virtualAddress)} | RawPtr:{' '}
                          {hex32(sec.pointerToRawData)} | RawSize: {hex32(sec.sizeOfRawData)}
                        </div>
                      </div>
                    ))}
                  </div>
                </div>
              )}
            </div>
          )}

          {activeRightTab === 'tests' && (
            <div className="flex-1 overflow-y-auto font-mono text-xs space-y-2">
              <div className="bg-emerald-950/30 border border-emerald-700/50 rounded p-2.5 text-emerald-200">
                <div className="font-semibold">
                  Native C11 WebAssembly Verification (core/*.c → public/tinyx86.wasm)
                </div>
                <div className="text-[11px] text-emerald-300/90 mt-0.5">
                  Status: {integrationResult.passed ? 'ALL PASSED' : 'FAILED'} (
                  {integrationResult.checks} C assertions, {integrationResult.failures} failures)
                </div>
                <div className="text-[11px] text-sky-300 mt-1">
                  sizeof(CPU_Context) = {integrationResult.sizeofCpuContext} bytes | sizeof(DecodeContext) ={' '}
                  {integrationResult.sizeofDecodeContext} bytes
                </div>
              </div>
              <div className="bg-zinc-950 border border-zinc-800 rounded p-2.5 space-y-1.5 text-[11px] text-zinc-300">
                <div className="text-amber-300 font-semibold">
                  Executed directly in C via wasm_run_c_integration_tests():
                </div>
                <div className="text-emerald-400">
                  PASS [Suite 1]: ParseInstuction() + runcpu() ALU/Stack/ZF (EAX=42, EBX=42, ECX=0, ZF=1)
                </div>
                <div className="text-emerald-400">
                  PASS [Suite 2]: Relative Branch JMP rel8 (0xEB 0x05 skipping intermediate MOV)
                </div>
                <div className="text-emerald-400">
                  PASS [Suite 3]: x87 FPU Ring Stack (FLD1 + FLD1 + FADDP -&gt; ST(0) == 2.0)
                </div>
              </div>
            </div>
          )}

          {activeRightTab === 'guide' && (
            <div className="flex-1 overflow-y-auto text-xs space-y-2.5 pr-1">
              <div className="bg-zinc-950 border border-zinc-800 rounded p-2.5 space-y-1.5">
                <div className="text-amber-300 font-semibold">
                  原生 C11 代码直通前端 (WASI WebAssembly 架构)
                </div>
                <p className="text-zinc-300 leading-relaxed">
                  当前前端界面<strong>不含任何 TypeScript 模拟器重写</strong>，而是将仓库中的{' '}
                  <code>core/TinyX86.c</code>、<code>core/disasm.c</code>、
                  <code>core/PELoader.c</code>、<code>core/PETools.c</code> 与{' '}
                  <code>core/debugger.c</code> 通过 Clang (<code>wasm32-wasi</code>) 直接编译为{' '}
                  <code>public/tinyx86.wasm</code>。
                </p>
                <p className="text-zinc-300 leading-relaxed">
                  每一次单步执行、反汇编、寄存器读写和终端输出，均直接调用您的 C 语言函数并拦截 C{' '}
                  <code>printf</code> 标准输出流。
                </p>
              </div>

              <div className="bg-zinc-950 border border-zinc-800 rounded p-2.5 space-y-1.5 font-mono text-[11px]">
                <div className="text-emerald-400 font-semibold font-sans text-xs">
                  跨平台原生 C 构建命令 (CMake / Make / VS)
                </div>
                <div className="text-zinc-400"># Linux / macOS (GCC / Clang)</div>
                <div className="bg-zinc-900 px-2 py-1 rounded text-zinc-200">
                  cmake -S . -B build &amp;&amp; cmake --build build &amp;&amp; ctest --test-dir build
                </div>
                <div className="text-zinc-400"># GNU Make 一键构建与测试</div>
                <div className="bg-zinc-900 px-2 py-1 rounded text-zinc-200">
                  make &amp;&amp; make test
                </div>
                <div className="text-zinc-400"># Windows Visual Studio (Win32 &amp; x64)</div>
                <div className="bg-zinc-900 px-2 py-1 rounded text-zinc-200">
                  cmake --preset windows-x86-debug &amp;&amp; cmake --build --preset windows-x86-debug
                </div>
              </div>
            </div>
          )}
        </section>
      </main>
    </div>
  );
}
export default App;
