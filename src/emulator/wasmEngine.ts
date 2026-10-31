// ============================================================================
// src/emulator/wasmEngine.ts
// WebAssembly FFI 桥接层：直接加载并调用由 core/*.c 编译生成的 public/tinyx86.wasm
// 不包含任何 TypeScript 模拟器重写逻辑，所有指令解码、执行、PE解析、断点与调试命令
// 均 100% 由 C11 原生代码 (core/TinyX86.c, core/disasm.c, core/PELoader.c,
// core/PETools.c, core/debugger.c, core/wasm_bridge.c) 执行。
// ============================================================================

import { CPUContext, PEHeaderInfo, PESectionInfo } from './types';

export interface DisasmLine {
  address: number;
  instr_len: number;
  bytes: number[];
  asm_str: string;
}

export interface CIntegrationTestResult {
  passed: boolean;
  checks: number;
  failures: number;
  sizeofCpuContext: number;
  sizeofDecodeContext: number;
}

interface TinyX86WasmExports {
  memory: WebAssembly.Memory;
  _initialize?: () => void;
  wasm_alloc: (size: number) => number;
  wasm_free: (ptr: number) => void;
  wasm_load_preset: (presetId: number) => number;
  wasm_load_bin: (dataPtr: number, size: number, baseAddr: number) => number;
  wasm_load_pe: (dataPtr: number, size: number) => number;
  wasm_pe_is_valid: () => number;
  wasm_pe_machine: () => number;
  wasm_pe_num_sections: () => number;
  wasm_pe_entry_rva: () => number;
  wasm_pe_image_base: () => number;
  wasm_pe_size_of_image: () => number;
  wasm_pe_size_of_headers: () => number;
  wasm_pe_get_section: (idx: number, outNamePtr: number, outFieldsPtr: number) => number;
  wasm_get_code_base: () => number;
  wasm_exec_command: (cmdStrPtr: number) => number;
  wasm_toggle_breakpoint: (addr: number) => number;
  wasm_get_breakpoints: (outAddrsPtr: number, maxCount: number) => number;
  wasm_clear_breakpoints: () => void;
  wasm_get_gpr: (idx: number) => number;
  wasm_get_eip: () => number;
  wasm_get_eflags: () => number;
  wasm_get_seg: (idx: number) => number;
  wasm_get_fpu_reg: (idx: number) => number;
  wasm_get_fpu_sw: () => number;
  wasm_get_fpu_top: () => number;
  wasm_get_fpu_cw: () => number;
  wasm_get_halted: () => number;
  wasm_set_cpu_state: (namePtr: number, val: number) => number;
  wasm_mem_read: (addr: number, byteLen: number) => number;
  wasm_mem_write: (addr: number, val: number, byteLen: number) => void;
  wasm_disasm_at: (
    addr: number,
    outAsmPtr: number,
    maxAsmLen: number,
    outBytesPtr: number,
    maxBytes: number
  ) => number;
  wasm_get_sizeof_cpu_context: () => number;
  wasm_get_sizeof_decode_context: () => number;
  wasm_run_c_integration_tests: (outChecksPtr: number, outFailuresPtr: number) => number;
}

export class TinyX86WasmEngine {
  private exports!: TinyX86WasmExports;
  private stdoutBuffer = '';
  private capturedLines: string[] = [];
  private cTestResult: CIntegrationTestResult = {
    passed: true,
    checks: 13,
    failures: 0,
    sizeofCpuContext: 0,
    sizeofDecodeContext: 0,
  };

  static async create(): Promise<TinyX86WasmEngine> {
    const engine = new TinyX86WasmEngine();
    await engine.init();
    return engine;
  }

  private async init(): Promise<void> {
    const decoder = new TextDecoder('utf-8');

    const wasiImports = {
      fd_write: (fd: number, iovsPtr: number, iovsLen: number, nwrittenPtr: number): number => {
        const mem = new DataView(this.exports.memory.buffer);
        const u8 = new Uint8Array(this.exports.memory.buffer);
        let totalWritten = 0;

        for (let i = 0; i < iovsLen; i++) {
          const bufPtr = mem.getUint32(iovsPtr + i * 8, true);
          const bufLen = mem.getUint32(iovsPtr + i * 8 + 4, true);
          if (bufLen > 0) {
            const chunk = decoder.decode(u8.subarray(bufPtr, bufPtr + bufLen), { stream: true });
            if (fd === 1 || fd === 2) {
              this.stdoutBuffer += chunk;
              const parts = this.stdoutBuffer.split('\n');
              this.stdoutBuffer = parts.pop() ?? '';
              for (const line of parts) {
                const cleaned = line.replace(/\r$/, '');
                if (cleaned.length > 0) {
                  this.capturedLines.push(cleaned);
                }
              }
            }
            totalWritten += bufLen;
          }
        }
        mem.setUint32(nwrittenPtr, totalWritten, true);
        return 0;
      },
      fd_close: () => 0,
      fd_seek: () => 0,
      fd_read: () => 0,
      fd_fdstat_get: (fd: number, statPtr: number): number => {
        const mem = new DataView(this.exports.memory.buffer);
        mem.setUint8(statPtr, 2); // Character device
        mem.setUint16(statPtr + 2, 0, true);
        mem.setBigUint64(statPtr + 8, 0n, true);
        mem.setBigUint64(statPtr + 16, 0n, true);
        return 0;
      },
      fd_fdstat_set_flags: () => 0,
      fd_prestat_get: () => 8, // EBADF
      fd_prestat_dir_name: () => 8,
      environ_sizes_get: (countPtr: number, bufSizePtr: number): number => {
        const mem = new DataView(this.exports.memory.buffer);
        mem.setUint32(countPtr, 0, true);
        mem.setUint32(bufSizePtr, 0, true);
        return 0;
      },
      environ_get: () => 0,
      args_sizes_get: (argcPtr: number, argvBufSizePtr: number): number => {
        const mem = new DataView(this.exports.memory.buffer);
        mem.setUint32(argcPtr, 0, true);
        mem.setUint32(argvBufSizePtr, 0, true);
        return 0;
      },
      args_get: () => 0,
      proc_exit: (code: number) => {
        this.capturedLines.push(`[WASI] proc_exit(${code})`);
      },
      clock_time_get: (_id: number, _precision: bigint, timePtr: number): number => {
        const mem = new DataView(this.exports.memory.buffer);
        mem.setBigUint64(timePtr, BigInt(Date.now()) * 1000000n, true);
        return 0;
      },
    };

    const baseUrl = import.meta.env.BASE_URL || './';
    const wasmUrl = `${baseUrl.endsWith('/') ? baseUrl : baseUrl + '/'}tinyx86.wasm`;
    const response = await fetch(wasmUrl);
    const bytes = await response.arrayBuffer();
    const { instance } = await WebAssembly.instantiate(bytes, {
      wasi_snapshot_preview1: wasiImports,
    });

    this.exports = instance.exports as unknown as TinyX86WasmExports;
    if (typeof this.exports._initialize === 'function') {
      this.exports._initialize();
    }

    // 1. Run the native C integration test suite (tests/cpu_integration_tests.c logic inside C)
    const scratchPtr = this.exports.wasm_alloc(16);
    const passed = this.exports.wasm_run_c_integration_tests(scratchPtr, scratchPtr + 4) === 1;
    const view = new DataView(this.exports.memory.buffer);
    const checks = view.getInt32(scratchPtr, true);
    const failures = view.getInt32(scratchPtr + 4, true);
    this.exports.wasm_free(scratchPtr);

    this.cTestResult = {
      passed,
      checks,
      failures,
      sizeofCpuContext: this.exports.wasm_get_sizeof_cpu_context(),
      sizeofDecodeContext: this.exports.wasm_get_sizeof_decode_context(),
    };

    // Clear startup test stdout and load default preset 0 (g_TestCode in C)
    this.drainStdoutLines();
    this.exports.wasm_load_preset(0);
  }

  drainStdoutLines(): string[] {
    if (this.stdoutBuffer.length > 0) {
      this.capturedLines.push(this.stdoutBuffer.replace(/\r$/, ''));
      this.stdoutBuffer = '';
    }
    const out = [...this.capturedLines];
    this.capturedLines = [];
    return out;
  }

  getIntegrationResult(): CIntegrationTestResult {
    return this.cTestResult;
  }

  private withCString<T>(str: string, fn: (ptr: number) => T): T {
    const encoded = new TextEncoder().encode(str + '\0');
    const ptr = this.exports.wasm_alloc(encoded.length);
    new Uint8Array(this.exports.memory.buffer, ptr, encoded.length).set(encoded);
    try {
      return fn(ptr);
    } finally {
      this.exports.wasm_free(ptr);
    }
  }

  loadPreset(presetIndex: number): string[] {
    this.drainStdoutLines();
    this.exports.wasm_load_preset(presetIndex);
    return this.drainStdoutLines();
  }

  loadBin(bytes: Uint8Array, baseAddr = 0x00400000): string[] {
    this.drainStdoutLines();
    const ptr = this.exports.wasm_alloc(bytes.length);
    new Uint8Array(this.exports.memory.buffer, ptr, bytes.length).set(bytes);
    try {
      this.exports.wasm_load_bin(ptr, bytes.length, baseAddr >>> 0);
    } finally {
      this.exports.wasm_free(ptr);
    }
    return this.drainStdoutLines();
  }

  loadPE(bytes: Uint8Array): { ok: boolean; peInfo: PEHeaderInfo | null; logs: string[] } {
    this.drainStdoutLines();
    const ptr = this.exports.wasm_alloc(bytes.length);
    new Uint8Array(this.exports.memory.buffer, ptr, bytes.length).set(bytes);
    let ok = false;
    try {
      ok = this.exports.wasm_load_pe(ptr, bytes.length) === 1;
    } finally {
      this.exports.wasm_free(ptr);
    }
    const logs = this.drainStdoutLines();
    if (!ok || this.exports.wasm_pe_is_valid() === 0) {
      return { ok: false, peInfo: null, logs };
    }

    const numSections = this.exports.wasm_pe_num_sections();
    const sections: PESectionInfo[] = [];
    const namePtr = this.exports.wasm_alloc(16);
    const fieldsPtr = this.exports.wasm_alloc(20);
    const decoder = new TextDecoder('utf-8');

    try {
      for (let i = 0; i < numSections; i++) {
        if (this.exports.wasm_pe_get_section(i, namePtr, fieldsPtr) === 1) {
          const u8 = new Uint8Array(this.exports.memory.buffer, namePtr, 9);
          let end = u8.indexOf(0);
          if (end < 0) end = 8;
          const name = decoder.decode(u8.subarray(0, end));
          const dv = new DataView(this.exports.memory.buffer, fieldsPtr, 20);
          sections.push({
            name,
            virtualAddress: dv.getUint32(0, true),
            virtualSize: dv.getUint32(4, true),
            pointerToRawData: dv.getUint32(8, true),
            sizeOfRawData: dv.getUint32(12, true),
            characteristics: dv.getUint32(16, true),
          });
        }
      }
    } finally {
      this.exports.wasm_free(namePtr);
      this.exports.wasm_free(fieldsPtr);
    }

    const peInfo: PEHeaderInfo = {
      isValidPE: true,
      machine: this.exports.wasm_pe_machine(),
      numberOfSections: numSections,
      entryPointRVA: this.exports.wasm_pe_entry_rva() >>> 0,
      imageBase: this.exports.wasm_pe_image_base() >>> 0,
      sizeOfImage: this.exports.wasm_pe_size_of_image() >>> 0,
      sizeOfHeaders: this.exports.wasm_pe_size_of_headers() >>> 0,
      sections,
    };

    return { ok: true, peInfo, logs };
  }

  execCommand(cmdLine: string): string[] {
    this.drainStdoutLines();
    this.withCString(cmdLine, (ptr) => this.exports.wasm_exec_command(ptr));
    return this.drainStdoutLines();
  }

  toggleBreakpoint(addr: number): string[] {
    this.drainStdoutLines();
    this.exports.wasm_toggle_breakpoint(addr >>> 0);
    return this.drainStdoutLines();
  }

  getBreakpoints(): number[] {
    const maxCount = 16;
    const ptr = this.exports.wasm_alloc(maxCount * 4);
    try {
      const count = this.exports.wasm_get_breakpoints(ptr, maxCount);
      const dv = new DataView(this.exports.memory.buffer, ptr, count * 4);
      const list: number[] = [];
      for (let i = 0; i < count; i++) {
        list.push(dv.getUint32(i * 4, true) >>> 0);
      }
      return list;
    } finally {
      this.exports.wasm_free(ptr);
    }
  }

  setCpuState(name: string, val: number): { ok: boolean; logs: string[] } {
    this.drainStdoutLines();
    const ok = this.withCString(name, (ptr) => this.exports.wasm_set_cpu_state(ptr, val >>> 0) === 1);
    return { ok, logs: this.drainStdoutLines() };
  }

  readCpuState(): CPUContext {
    const gpr = new Uint32Array(8);
    for (let i = 0; i < 8; i++) {
      gpr[i] = this.exports.wasm_get_gpr(i) >>> 0;
    }
    const seg = new Uint16Array(6);
    for (let i = 0; i < 6; i++) {
      seg[i] = this.exports.wasm_get_seg(i) & 0xffff;
    }
    const rawEflags = this.exports.wasm_get_eflags() >>> 0;
    const swRaw = this.exports.wasm_get_fpu_sw() & 0xffff;
    const fpuRegs: number[] = [];
    for (let i = 0; i < 8; i++) {
      fpuRegs.push(this.exports.wasm_get_fpu_reg(i));
    }

    return {
      GPR: gpr,
      EIP: this.exports.wasm_get_eip() >>> 0,
      EFLAGS: {
        CF: (rawEflags >>> 0) & 1,
        PF: (rawEflags >>> 2) & 1,
        AF: (rawEflags >>> 4) & 1,
        ZF: (rawEflags >>> 6) & 1,
        SF: (rawEflags >>> 7) & 1,
        TF: (rawEflags >>> 8) & 1,
        IF: (rawEflags >>> 9) & 1,
        DF: (rawEflags >>> 10) & 1,
        OF: (rawEflags >>> 11) & 1,
        IOPL: (rawEflags >>> 12) & 3,
        NT: (rawEflags >>> 14) & 1,
      },
      seg,
      FPU: {
        Regs: fpuRegs,
        SW: {
          IE: (swRaw >>> 0) & 1,
          DE: (swRaw >>> 1) & 1,
          ZE: (swRaw >>> 2) & 1,
          OE: (swRaw >>> 3) & 1,
          UE: (swRaw >>> 4) & 1,
          PE: (swRaw >>> 5) & 1,
          SF: (swRaw >>> 6) & 1,
          ES: (swRaw >>> 7) & 1,
          C0: (swRaw >>> 8) & 1,
          C1: (swRaw >>> 9) & 1,
          C2: (swRaw >>> 10) & 1,
          TOP: (swRaw >>> 11) & 7,
          C3: (swRaw >>> 14) & 1,
          B: (swRaw >>> 15) & 1,
        },
        CW: this.exports.wasm_get_fpu_cw() & 0xffff,
        TW: 0xffff,
      },
      Halted: this.exports.wasm_get_halted() === 1,
    };
  }

  getRawEflags(): number {
    return this.exports.wasm_get_eflags() >>> 0;
  }

  getCodeBase(): number {
    return this.exports.wasm_get_code_base() >>> 0;
  }

  readMemU8(addr: number): number {
    return this.exports.wasm_mem_read(addr >>> 0, 1) & 0xff;
  }

  readMemU32(addr: number): number {
    return this.exports.wasm_mem_read(addr >>> 0, 4) >>> 0;
  }

  writeMem(addr: number, val: number, byteLen: number): void {
    this.exports.wasm_mem_write(addr >>> 0, val >>> 0, byteLen);
  }

  disassembleList(startAddr: number, count: number): DisasmLine[] {
    const outAsmPtr = this.exports.wasm_alloc(128);
    const outBytesPtr = this.exports.wasm_alloc(16);
    const decoder = new TextDecoder('utf-8');
    const result: DisasmLine[] = [];

    try {
      let cur = startAddr >>> 0;
      for (let i = 0; i < count; i++) {
        const len = this.exports.wasm_disasm_at(cur, outAsmPtr, 128, outBytesPtr, 16);
        const step = Math.max(1, len);
        const u8Asm = new Uint8Array(this.exports.memory.buffer, outAsmPtr, 128);
        let nullIdx = u8Asm.indexOf(0);
        if (nullIdx < 0) nullIdx = 128;
        const asm_str = decoder.decode(u8Asm.subarray(0, nullIdx));

        const u8Bytes = new Uint8Array(this.exports.memory.buffer, outBytesPtr, Math.min(step, 16));
        const bytes = Array.from(u8Bytes);

        result.push({
          address: cur,
          instr_len: step,
          bytes,
          asm_str,
        });
        cur = (cur + step) >>> 0;
      }
    } finally {
      this.exports.wasm_free(outAsmPtr);
      this.exports.wasm_free(outBytesPtr);
    }

    return result;
  }
}
