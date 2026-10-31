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
import { CPUContext, DecodeContext, PEHeaderInfo } from './emulator/types';
import {
  VirtualMemory,
  cloneCPUContext,
  runcpu,
  ParseInstruction,
  SetCpuState,
  IsStepOverCandidate,
  PackEFLAGS,
} from './emulator/tinyx86';
import {
  G_TEST_CODE,
  SUM_1_TO_10_CODE,
  DOS_INT21_CODE,
  LoadMem,
  LoadExeBuffer,
  runCpuIntegrationTests,
} from './emulator/peLoader';

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
  name: string;
  subtitle: string;
  code: Uint8Array;
}

const PRESETS: PresetInfo[] = [
  {
    id: 'power_fpu',
    name: 't: 递归幂运算 power(2.0, 3) [main.c 默认]',
    subtitle: '栈帧开辟、REP STOSD 0xCCCCCCCC、递归调用与 x87 FPU (ST0 = 8.0)',
    code: G_TEST_CODE,
  },
  {
    id: 'sum_loop',
    name: 'Sum 1..10 累加循环 (ALU + LOOP)',
    subtitle: '计算 1+2+...+10 = 55 (0x37) 存入 EAX 与 EBX',
    code: SUM_1_TO_10_CODE,
  },
  {
    id: 'dos_int21',
    name: 'DOS INT 21h 高层模拟 (HLE 字符串输出)',
    subtitle: '模拟 AH=09h 输出 $ 结尾字符串与 AH=4Ch 程序退出',
    code: DOS_INT21_CODE,
  },
];

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
  const memRef = useRef<VirtualMemory>(new VirtualMemory());
  const [cpu, setCpu] = useState<CPUContext>(() =>
    LoadMem(G_TEST_CODE, memRef.current, 0x00400000)
  );
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
    'TinyX86 Emulator [Sprint 13 - Cross-Platform Windows / Linux / macOS / Web]',
    "Loaded internal test 'g_TestCode' (recursive power(2.0, 3)) at 0x00400000.",
    '=== TinyX86 Debugger Initialized ===',
    "Type 'h' for help, or use 's' (Step Into), 'n' (Step Over), 'c' (Continue), 'r' (Registers).",
  ]);

  const terminalEndRef = useRef<HTMLDivElement>(null);
  const fileInputRef = useRef<HTMLInputElement>(null);

  useEffect(() => {
    terminalEndRef.current?.scrollIntoView({ behavior: 'smooth' });
  }, [logs]);

  const appendLog = (lines: string | string[]) => {
    const arr = Array.isArray(lines) ? lines : [lines];
    setLogs((prev) => [...prev.slice(-300), ...arr]);
  };

  const isSoftwareBreakpoint = (eip: number): boolean => {
    return memRef.current.readU8(eip) === 0xcc;
  };

  const isUserBreakpoint = (eip: number, bpList = breakpoints): boolean => {
    return bpList.includes(eip >>> 0);
  };

  const loadPreset = (preset: PresetInfo) => {
    const nextCpu = LoadMem(preset.code, memRef.current, 0x00400000);
    setCpu(nextCpu);
    setActivePreset(preset.id);
    setLoadedTitle(preset.name);
    setPeInfo(null);
    setMemViewAddr(0x00400000);
    setMemAddrInput('00400000');
    setMemVersion((v) => v + 1);
    appendLog([
      `[Shell] Loading ${preset.name}...`,
      `[Loader] Test code mapped at 0x00400000 (ESP=${hex32(nextCpu.GPR[4])})`,
    ]);
  };

  const handleStepInto = (count = 1, currentCpu = cpu) => {
    const next = cloneCPUContext(currentCpu);
    const outLogs: string[] = [];
    const n = Math.max(1, Math.min(count, 10000));
    outLogs.push(`Stepping ${n} instruction${n > 1 ? 's' : ''}...`);

    for (let i = 0; i < n; i++) {
      if (next.Halted) break;
      if (isSoftwareBreakpoint(next.EIP)) {
        outLogs.push(
          `[Step] Trapped by INT 3 at ${hex32(next.EIP)} (Skipping 0xCC byte)`
        );
        next.EIP = (next.EIP + 1) >>> 0;
        break;
      }
      runcpu(next, memRef.current, 1, (m) => outLogs.push(m));
      if (i < n - 1 && isUserBreakpoint(next.EIP)) {
        outLogs.push(`[Step] Stopped at breakpoint ${hex32(next.EIP)}`);
        break;
      }
    }

    setCpu(next);
    setMemVersion((v) => v + 1);
    appendLog(outLogs);
  };

  const handleStepOver = (currentCpu = cpu) => {
    const next = cloneCPUContext(currentCpu);
    if (next.Halted) {
      appendLog('[System] CPU Halted. Reset or reload program to run again.');
      return;
    }

    const d_ctx = ParseInstruction((addr) => memRef.current.readU8(addr), next.EIP);
    if (IsStepOverCandidate(d_ctx)) {
      const targetEip = (next.EIP + d_ctx.instr_len) >>> 0;
      const outLogs: string[] = [
        `[Debugger] Stepping over Block/Call... (Target EIP: ${hex32(targetEip)})`,
      ];
      let maxSteps = 200000;
      while (next.EIP !== targetEip && !next.Halted && maxSteps-- > 0) {
        if (isSoftwareBreakpoint(next.EIP)) {
          outLogs.push(`[Debugger] Hit INT 3 inside StepOver at ${hex32(next.EIP)}`);
          break;
        }
        runcpu(next, memRef.current, 1, (m) => outLogs.push(m));
        if (isUserBreakpoint(next.EIP)) {
          outLogs.push(`[Debugger] Hit User Breakpoint inside StepOver at ${hex32(next.EIP)}`);
          break;
        }
      }
      if (maxSteps <= 0) {
        outLogs.push('[Warn] StepOver timeout (Loop too long or infinite?). Stopped.');
      }
      setCpu(next);
      setMemVersion((v) => v + 1);
      appendLog(outLogs);
    } else {
      handleStepInto(1, next);
    }
  };

  const handleContinue = (currentCpu = cpu) => {
    const next = cloneCPUContext(currentCpu);
    if (next.Halted) {
      appendLog('[System] CPU is halted. Click Reset to restart execution.');
      return;
    }
    const outLogs: string[] = ['Running...'];
    if (isUserBreakpoint(next.EIP)) {
      runcpu(next, memRef.current, 1, (m) => outLogs.push(m));
    }

    let maxSteps = 500000;
    while (!next.Halted && maxSteps-- > 0) {
      if (isSoftwareBreakpoint(next.EIP)) {
        outLogs.push(`[Debugger] Hit Software Breakpoint (INT 3) at ${hex32(next.EIP)}`);
        break;
      }
      runcpu(next, memRef.current, 1, (m) => outLogs.push(m));
      if (isUserBreakpoint(next.EIP)) {
        outLogs.push(`[Debugger] Hit Breakpoint at ${hex32(next.EIP)}`);
        break;
      }
    }
    if (maxSteps <= 0) {
      outLogs.push('[Warn] Execution paused after 500,000 steps (infinite loop guard).');
    }

    setCpu(next);
    setMemVersion((v) => v + 1);
    appendLog(outLogs);
  };

  const toggleBreakpoint = (addr: number) => {
    const uAddr = addr >>> 0;
    setBreakpoints((prev) => {
      if (prev.includes(uAddr)) {
        appendLog(`Removed breakpoint at ${hex32(uAddr)}`);
        return prev.filter((x) => x !== uAddr);
      }
      if (prev.length >= 16) {
        appendLog('Error: Breakpoint list full (16).');
        return prev;
      }
      appendLog(`Breakpoint set at ${hex32(uAddr)}`);
      return [...prev, uAddr];
    });
  };

  const formatShowState = (c: CPUContext): string[] => {
    const lines: string[] = [];
    lines.push('--- CPU STATE ---------------------------------------------------');
    lines.push(
      `EAX: ${hex32(c.GPR[0])}  EBX: ${hex32(c.GPR[3])}  ECX: ${hex32(c.GPR[1])}  EDX: ${hex32(c.GPR[2])}`
    );
    lines.push(
      `ESI: ${hex32(c.GPR[6])}  EDI: ${hex32(c.GPR[7])}  EBP: ${hex32(c.GPR[5])}  ESP: ${hex32(c.GPR[4])}`
    );
    lines.push(
      `EIP: ${hex32(c.EIP)}  CS:${(c.seg[1] & 0xffff).toString(16).toUpperCase().padStart(4, '0')} DS:${(c.seg[3] & 0xffff).toString(16).toUpperCase().padStart(4, '0')} ES:${(c.seg[0] & 0xffff).toString(16).toUpperCase().padStart(4, '0')} SS:${(c.seg[2] & 0xffff).toString(16).toUpperCase().padStart(4, '0')}`
    );
    const activeFlags = FLAG_KEYS.filter((k) => c.EFLAGS[k] === 1).join(' ');
    lines.push(
      `FLAGS: [ ${activeFlags}${activeFlags ? ' ' : ''}IOPL=${c.EFLAGS.IOPL} ] (Raw: ${hex32(PackEFLAGS(c))})`
    );
    lines.push('--- FPU ---------------------------------------------------------');
    const top = c.FPU.SW.TOP;
    lines.push(
      `FPU SW: TOP=${top}  C3=${c.FPU.SW.C3} C2=${c.FPU.SW.C2} C1=${c.FPU.SW.C1} C0=${c.FPU.SW.C0}`
    );
    for (let i = 0; i < 8; i++) {
      const logicalIdx = (i - top + 8) & 7;
      const tag = i === top ? '<- ST(0) TOP' : `   ST(${logicalIdx})`;
      lines.push(`   R${i}  | ${c.FPU.Regs[i].toFixed(10).padStart(18, ' ')} | ${tag}`);
    }
    lines.push('-----------------------------------------------------------------');
    return lines;
  };

  const executeDebuggerCommand = (rawCmd: string) => {
    const trimmed = rawCmd.trim();
    if (!trimmed) return;
    appendLog(`(dbg) > ${trimmed}`);

    const cmd = trimmed[0].toLowerCase();
    const args = trimmed.slice(1).trim();

    switch (cmd) {
      case 't': {
        const preset = PRESETS[0];
        loadPreset(preset);
        break;
      }
      case 's': {
        const count = args ? parseInt(args, 10) || 1 : 1;
        handleStepInto(count);
        break;
      }
      case 'n': {
        handleStepOver();
        break;
      }
      case 'c': {
        handleContinue();
        break;
      }
      case 'b': {
        if (args.startsWith('l')) {
          if (breakpoints.length === 0) appendLog('No breakpoints set.');
          else {
            appendLog([
              'Breakpoints:',
              ...breakpoints.map((bp, idx) => `  [${idx}] ${hex32(bp)}`),
            ]);
          }
        } else if (args.startsWith('c')) {
          setBreakpoints([]);
          appendLog('All breakpoints cleared.');
        } else {
          const addr = parseInt(args.replace(/^0x/i, ''), 16);
          if (!Number.isNaN(addr)) {
            toggleBreakpoint(addr >>> 0);
          } else {
            appendLog('Usage: b <hex_addr> | bl | bc');
          }
        }
        break;
      }
      case 'r': {
        appendLog(formatShowState(cpu));
        break;
      }
      case 'u': {
        const linesCount = Math.min(50, Math.max(1, parseInt(args, 10) || 5));
        let tempEip = cpu.EIP >>> 0;
        const out: string[] = [`Disassembly (${linesCount} lines):`];
        for (let i = 0; i < linesCount; i++) {
          const d = ParseInstruction((a) => memRef.current.readU8(a), tempEip);
          const hexBytes = d.bytes.map((b) => hex8(b)).join('').padEnd(16, ' ');
          out.push(`  ${hex32(tempEip)}: ${hexBytes} | ${d.asm_str}`);
          tempEip = (tempEip + d.instr_len) >>> 0;
        }
        appendLog(out);
        break;
      }
      case 'm': {
        const addr = parseInt(args.replace(/^0x/i, ''), 16);
        if (Number.isNaN(addr)) {
          appendLog('Usage: m <hex_addr>');
        } else {
          const uAddr = addr >>> 0;
          setMemViewAddr(uAddr);
          setMemAddrInput(uAddr.toString(16).toUpperCase().padStart(8, '0'));
          setActiveRightTab('memory');
          const out: string[] = [`Memory at ${hex32(uAddr)}:`];
          for (let i = 0; i < 4; i++) {
            const lineAddr = (uAddr + i * 16) >>> 0;
            let hexPart = '';
            let asciiPart = '';
            for (let j = 0; j < 16; j++) {
              const b = memRef.current.readU8(lineAddr + j);
              hexPart += hex8(b) + ' ';
              asciiPart += b >= 32 && b <= 126 ? String.fromCharCode(b) : '.';
            }
            out.push(
              `  ${lineAddr.toString(16).toUpperCase().padStart(8, '0')}: ${hexPart} | ${asciiPart}`
            );
          }
          appendLog(out);
        }
        break;
      }
      case 'k': {
        const count = Math.min(32, Math.max(1, parseInt(args, 10) || 8));
        const esp = cpu.GPR[4] >>> 0;
        const ebp = cpu.GPR[5] >>> 0;
        const out: string[] = [
          `Stack Dump (ESP=${hex32(esp)}):`,
          'Address     Value       ASCII   Annotation',
          '----------  ----------  -----   ------------------',
        ];
        for (let i = -2; i < count; i++) {
          const addr = (esp + i * 4) >>> 0;
          const val = memRef.current.read(addr, 4);
          let ascii = '';
          for (let b = 0; b < 4; b++) {
            const byte = (val >>> (b * 8)) & 0xff;
            ascii += byte >= 32 && byte <= 126 ? String.fromCharCode(byte) : '.';
          }
          const notes: string[] = [];
          if (addr === esp) notes.push('<--- ESP (Top)');
          if (addr === ebp) notes.push('<--- EBP (Frame)');
          out.push(`${hex32(addr)}  ${hex32(val)}  ${ascii}    ${notes.join(' / ')}`);
        }
        appendLog(out);
        break;
      }
      case 'e': {
        if (args.startsWith('d ') || args.startsWith('w ')) {
          const isDword = args.startsWith('d ');
          const parts = args.slice(2).trim().split(/\s+/);
          if (parts.length >= 2) {
            const addr = parseInt(parts[0].replace(/^0x/i, ''), 16) >>> 0;
            const val = parseInt(parts[1].replace(/^0x/i, ''), 16) >>> 0;
            memRef.current.write(addr, val, isDword ? 4 : 2);
            setMemVersion((v) => v + 1);
            appendLog(
              `Memory Write: [${hex32(addr)}] = 0x${val.toString(16).toUpperCase()} (${isDword ? 4 : 2} bytes)`
            );
          } else {
            appendLog(`Usage: e${isDword ? 'd' : 'w'} <hex_addr> <hex_value>`);
          }
        } else {
          const parts = args.split(/\s+/).filter(Boolean);
          if (parts.length >= 2) {
            const addr = parseInt(parts[0].replace(/^0x/i, ''), 16) >>> 0;
            let count = 0;
            for (let i = 1; i < parts.length; i++) {
              const b = parseInt(parts[i].replace(/^0x/i, ''), 16);
              if (!Number.isNaN(b)) {
                memRef.current.writeU8(addr + count, b & 0xff);
                count++;
              }
            }
            setMemVersion((v) => v + 1);
            appendLog(`Wrote ${count} bytes to ${hex32(addr)}`);
          } else {
            appendLog('Usage: e <addr> <byte1> <byte2> ...');
          }
        }
        break;
      }
      case 'w': {
        const parts = args.split(/\s+/).filter(Boolean);
        if (parts.length >= 2) {
          const target = parts[0];
          const val = parseInt(parts[1].replace(/^0x/i, ''), 16) >>> 0;
          const next = cloneCPUContext(cpu);
          if (SetCpuState(next, target, val)) {
            setCpu(next);
            appendLog(`Updated ${target.toUpperCase()} = 0x${val.toString(16).toUpperCase()}`);
          } else {
            appendLog(`Error: Unknown register or flag '${target}'`);
          }
        } else {
          appendLog('Usage: w <reg/flag> <hex_value> (e.g., w EAX 1234, w ZF 1)');
        }
        break;
      }
      case 'h':
      case '?': {
        appendLog([
          '--- Command Help ---',
          ' Execution:',
          '  s [N]     : Step Into (execute N instructions, default 1)',
          '  n         : Step Over (skip CALL, LOOP, REP... instructions)',
          '  c         : Continue (run until breakpoint or halt)',
          '  t         : Reload default internal test (g_TestCode)',
          '  q         : Halt CPU',
          ' Breakpoints:',
          '  b <addr>  : Toggle Breakpoint at hex address (e.g., b 400014)',
          '  bl        : List all Breakpoints',
          '  bc        : Clear all Breakpoints',
          ' Inspection:',
          '  r         : Show all Registers (GPR, Flags, FPU)',
          '  u [N]     : Disassemble next N instructions (default 5)',
          '  m <addr>  : Dump memory at hex address (64 bytes)',
          '  k [N]     : Dump Stack (show N dwords around ESP, default 8)',
          ' Modification:',
          '  w <reg> <val>     : Write Register (e.g., w EAX 1234, w ZF 1)',
          '  e <addr> <b1>...  : Edit memory bytes (e.g., e 400000 90 CC)',
          '  ew <addr> <val>   : Edit memory Word (2 bytes)',
          '  ed <addr> <val>   : Edit memory DWORD (4 bytes)',
        ]);
        break;
      }
      case 'q': {
        const next = cloneCPUContext(cpu);
        next.Halted = true;
        setCpu(next);
        appendLog('Debugger exited. (Halted State: YES)');
        break;
      }
      default:
        appendLog(`Unknown command '${cmd}'. Type 'h' for help.`);
    }
  };

  const handleFileUpload = async (e: React.ChangeEvent<HTMLInputElement>) => {
    const file = e.target.files?.[0];
    if (!file) return;
    const arrayBuffer = await file.arrayBuffer();
    const bytes = new Uint8Array(arrayBuffer);
    const isExe = file.name.toLowerCase().endsWith('.exe') || (bytes[0] === 0x4d && bytes[1] === 0x5a);

    if (isExe) {
      const outLogs: string[] = [`[Shell] Loading PE: ${file.name}`];
      const { ctx, peInfo: parsedPe } = LoadExeBuffer(bytes, memRef.current, (m) =>
        outLogs.push(m)
      );
      if (ctx) {
        setCpu(ctx);
        setPeInfo(parsedPe);
        setLoadedTitle(`PE32: ${file.name}`);
        setMemViewAddr(ctx.EIP);
        setMemAddrInput(ctx.EIP.toString(16).toUpperCase().padStart(8, '0'));
        setActiveRightTab('pe');
        setMemVersion((v) => v + 1);
      }
      appendLog(outLogs);
    } else {
      const ctx = LoadMem(bytes, memRef.current, 0x00400000);
      setCpu(ctx);
      setPeInfo(null);
      setLoadedTitle(`BIN: ${file.name} (${bytes.length} bytes)`);
      setMemViewAddr(0x00400000);
      setMemAddrInput('00400000');
      setMemVersion((v) => v + 1);
      appendLog([
        `[Shell] Loading Binary: ${file.name}`,
        `[Loader] Binary loaded at 0x00400000 (${bytes.length} bytes)`,
      ]);
    }
    e.target.value = '';
  };

  const disasmList = useMemo(() => {
    const list: DecodeContext[] = [];
    const base = memRef.current.codeBase >>> 0;
    const startAddr =
      cpu.EIP >= base && cpu.EIP < base + 0x200 ? base : cpu.EIP >>> 0;
    let cur = startAddr;
    for (let i = 0; i < 28; i++) {
      const d = ParseInstruction((addr) => memRef.current.readU8(addr), cur);
      list.push(d);
      cur = (cur + Math.max(1, d.instr_len)) >>> 0;
    }
    return list;
  }, [cpu.EIP, memVersion]);

  const stackRows = useMemo(() => {
    const esp = cpu.GPR[4] >>> 0;
    const ebp = cpu.GPR[5] >>> 0;
    const rows = [];
    for (let i = -3; i < 14; i++) {
      const addr = (esp + i * 4) >>> 0;
      const val = memRef.current.read(addr, 4);
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
  }, [cpu.GPR, memVersion]);

  const memRows = useMemo(() => {
    const rows = [];
    const base = memViewAddr >>> 0;
    for (let r = 0; r < 12; r++) {
      const rowAddr = (base + r * 16) >>> 0;
      const bytes: number[] = [];
      let ascii = '';
      for (let c = 0; c < 16; c++) {
        const b = memRef.current.readU8(rowAddr + c);
        bytes.push(b);
        ascii += b >= 32 && b <= 126 ? String.fromCharCode(b) : '.';
      }
      rows.push({ rowAddr, bytes, ascii });
    }
    return rows;
  }, [memViewAddr, memVersion]);

  const integrationResult = useMemo(() => runCpuIntegrationTests(), []);

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
                  cpu.Halted
                    ? 'bg-red-500/15 text-red-300 border-red-500/30'
                    : 'bg-emerald-500/15 text-emerald-300 border-emerald-500/30'
                }`}
              >
                {cpu.Halted ? 'HALTED' : 'READY'}
              </span>
              <span className="text-[11px] font-mono px-1.5 py-0.5 rounded bg-sky-500/15 text-sky-300 border border-sky-500/30">
                Windows · Linux · macOS · Web
              </span>
            </div>
            <p className="text-xs text-zinc-400 font-mono truncate max-w-md">
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
            title="Step Into (s)"
          >
            <StepForward className="w-3.5 h-3.5" />
            Step Into (s)
          </button>

          <button
            onClick={() => handleStepOver()}
            className="flex items-center gap-1.5 px-3 py-1.5 rounded bg-zinc-800 hover:bg-zinc-700 border border-zinc-700 text-zinc-100 text-xs font-medium transition cursor-pointer"
            title="Step Over CALL/LOOP/REP (n)"
          >
            <FastForward className="w-3.5 h-3.5 text-amber-400" />
            Step Over (n)
          </button>

          <button
            onClick={() => handleContinue()}
            className="flex items-center gap-1.5 px-3 py-1.5 rounded bg-zinc-800 hover:bg-zinc-700 border border-zinc-700 text-zinc-100 text-xs font-medium transition cursor-pointer"
            title="Continue until Breakpoint or INT3 (c)"
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
              <span className="text-[11px] font-mono text-zinc-500">Click to edit</span>
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
                        if (!Number.isNaN(parsed)) {
                          const next = cloneCPUContext(cpu);
                          next.GPR[idx] = parsed >>> 0;
                          setCpu(next);
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
              <span className="text-xs font-mono text-zinc-400">
                {hex32(PackEFLAGS(cpu))}
              </span>
            </div>
            <div className="grid grid-cols-4 gap-1.5 font-mono text-xs">
              {FLAG_KEYS.map((flag) => {
                const active = cpu.EFLAGS[flag] === 1;
                return (
                  <button
                    key={flag}
                    onClick={() => {
                      const next = cloneCPUContext(cpu);
                      next.EFLAGS[flag] = active ? 0 : 1;
                      setCpu(next);
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
                  const next = cloneCPUContext(cpu);
                  next.EFLAGS.IOPL = ((cpu.EFLAGS.IOPL + 1) & 3) as number;
                  setCpu(next);
                }}
                className={`col-span-2 py-1 rounded border text-center transition cursor-pointer ${
                  cpu.EFLAGS.IOPL > 0
                    ? 'bg-amber-500/20 border-amber-500/60 text-amber-300 font-semibold'
                    : 'bg-zinc-950 border-zinc-800 text-zinc-500 hover:border-zinc-700'
                }`}
                title="Bits 12..13: I/O Privilege Level (I/O 特权级 0..3，点击循环切换)"
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
                    <span className={`text-[11px] ${isTop ? 'text-amber-300 font-semibold' : 'text-zinc-500'}`}>
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
                Core-x86 Disassembler View
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
                TinyX86 Interactive Debugger CLI (debugger.c)
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

            <div className="flex-1 bg-zinc-950 border border-zinc-800 rounded p-2.5 overflow-y-auto font-mono text-xs space-y-1 max-h-64">
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
              <div ref={terminalEndRef} />
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
              Tests ({integrationResult.checks}/{integrationResult.checks})
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
                  <p>Currently running a raw machine-code image mapped at 0x00400000.</p>
                  <p>
                    Click <strong>Load .EXE / .BIN</strong> in the top bar to parse and map any
                    32-bit Windows PE executable (<code>IMAGE_DOS_HEADER</code>,{' '}
                    <code>IMAGE_NT_HEADERS32</code>, and section table mapping via{' '}
                    <code>PELoader.c</code>).
                  </p>
                </div>
              ) : (
                <div className="space-y-2">
                  <div className="bg-zinc-950 border border-zinc-800 rounded p-2.5 space-y-1">
                    <div className="text-emerald-400 font-semibold mb-1">PE32 Header Info</div>
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
                  tests/cpu_integration_tests.c Verification
                </div>
                <div className="text-[11px] text-emerald-300/90 mt-0.5">
                  Status: {integrationResult.passed ? 'ALL PASSED' : 'FAILED'} (
                  {integrationResult.checks} checks, {integrationResult.failures} failures)
                </div>
              </div>
              <div className="bg-zinc-950 border border-zinc-800 rounded p-2.5 space-y-1">
                {integrationResult.details.map((line, idx) => (
                  <div
                    key={idx}
                    className={
                      line.startsWith('PASS')
                        ? 'text-emerald-400'
                        : line.startsWith('FAIL')
                        ? 'text-red-400'
                        : 'text-amber-300 font-semibold pt-1 border-t border-zinc-800'
                    }
                  >
                    {line}
                  </div>
                ))}
              </div>
            </div>
          )}

          {activeRightTab === 'guide' && (
            <div className="flex-1 overflow-y-auto text-xs space-y-2.5 pr-1">
              <div className="bg-zinc-950 border border-zinc-800 rounded p-2.5 space-y-1.5">
                <div className="text-amber-300 font-semibold">
                  手写模拟器学 x86 汇编的 6 大核心要点
                </div>
                <p className="text-zinc-300 leading-relaxed">
                  <strong>1. 寄存器物理重叠 (Union)：</strong> <code>EAX</code> 低 16 位为{' '}
                  <code>AX</code>，拆分为 <code>AH</code> (bits 8..15) 与 <code>AL</code> (bits
                  0..7)；8 位编码索引 4~7 对应 <code>AH/CH/DH/BH</code>。
                </p>
                <p className="text-zinc-300 leading-relaxed">
                  <strong>2. CF 与 OF 标志位公式：</strong> 硬件加法器不区分有符号/无符号，
                  <code>CF</code> 检测无符号进借位，<code>OF</code> 检测有符号同号相加异号溢出{' '}
                  <code>((dest ^ res) &amp; (src ^ res) &amp; sign_mask)</code>。
                </p>
                <p className="text-zinc-300 leading-relaxed">
                  <strong>3. ModR/M + SIB 寻址：</strong>{' '}
                  <code>EA = Base + Index * (1 &lt;&lt; Scale) + Disp</code>；<code>LEA</code>{' '}
                  只算地址不访存。
                </p>
                <p className="text-zinc-300 leading-relaxed">
                  <strong>4. Debug 栈帧 “烫烫烫”：</strong> 函数序言通过{' '}
                  <code>rep stosd</code> 将局部变量区填满 <code>0xCCCCCCCC</code>（即{' '}
                  <code>INT 3</code> 软断点）。
                </p>
                <p className="text-zinc-300 leading-relaxed">
                  <strong>5. x87 FPU 环形栈：</strong> 8 个物理寄存器通过{' '}
                  <code>(TOP + i) &amp; 7</code> 映射为 <code>ST(i)</code>，压栈时{' '}
                  <code>TOP</code> 递减回绕。
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
