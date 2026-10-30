import { CPUContext, PEHeaderInfo, PESectionInfo } from './types';
import { createInitialCPUContext, ParseInstruction, runcpu, VirtualMemory } from './tinyx86';

// Built-in g_TestCode from main.c: Recursive power(2.0, 3) = 8.0 using x86 stack frames + x87 FPU
export const G_TEST_CODE = new Uint8Array([
  // --- [0x00] CALLER ---
  0x6a, 0x03,                         // 0x00 | push 3
  0x68, 0x00, 0x00, 0x00, 0x40,       // 0x02 | push 0x40000000 (2.0 high dword)
  0x6a, 0x00,                         // 0x07 | push 0x00000000 (2.0 low dword)
  0xe8, 0x06, 0x00, 0x00, 0x00,       // 0x09 | call +6 (0x00400014)
  0x83, 0xc4, 0x0c,                   // 0x0E | add esp, 12
  0xcc,                               // 0x11 | int3
  0x90, 0x90,                         // 0x12 | nop, nop

  // --- [0x14] POWER FUNCTION START ---
  0x55,                               // 0x14 | push ebp
  0x8b, 0xec,                         // 0x15 | mov ebp, esp
  0x83, 0xec, 0x40,                   // 0x17 | sub esp, 0x40

  // 1. Fill stack frame with 0xCCCCCCCC using REP STOSD
  0x8d, 0x7d, 0xc0,                   // 0x1A | lea edi, [ebp-40h]
  0xb9, 0x10, 0x00, 0x00, 0x00,       // 0x1D | mov ecx, 10h
  0xb8, 0xcc, 0xcc, 0xcc, 0xcc,       // 0x22 | mov eax, 0xCCCCCCCC
  0xf3, 0xab,                         // 0x27 | rep stosd

  // 2. Save non-volatile registers
  0x53,                               // 0x29 | push ebx
  0x56,                               // 0x2A | push esi
  0x57,                               // 0x2B | push edi

  // --- Business Logic ---
  0x83, 0x7d, 0x10, 0x00,             // 0x2C | cmp dword ptr [ebp+16], 0
  0x7f, 0x04,                         // 0x30 | jg +4 (0x00400036)

  // --- Base Case ---
  0xd9, 0xe8,                         // 0x32 | fld1
  0xeb, 0x16,                         // 0x34 | jmp +22 (0x0040004C)

  // --- Recursive Step (0x36) ---
  0x8b, 0x45, 0x10,                   // 0x36 | mov eax, [ebp+16]
  0x48,                               // 0x39 | dec eax
  0x50,                               // 0x3A | push eax
  0xff, 0x75, 0x0c,                   // 0x3B | push dword ptr [ebp+12]
  0xff, 0x75, 0x08,                   // 0x3E | push dword ptr [ebp+8]
  0xe8, 0xce, 0xff, 0xff, 0xff,       // 0x41 | call -50 (0x00400014)
  0x83, 0xc4, 0x0c,                   // 0x46 | add esp, 12
  0xdc, 0x4d, 0x08,                   // 0x49 | fmul qword ptr [ebp+8]

  // --- [0x4C] Epilogue ---
  0x5f,                               // 0x4C | pop edi
  0x5e,                               // 0x4D | pop esi
  0x5b,                               // 0x4E | pop ebx
  0x8b, 0xe5,                         // 0x4F | mov esp, ebp
  0x5d,                               // 0x51 | pop ebp
  0xc3,                               // 0x52 | ret
]);

// Preset 2: Sum 1..10 Loop + ALU + Flags
export const SUM_1_TO_10_CODE = new Uint8Array([
  0x31, 0xc0,                         // xor eax, eax       (sum = 0)
  0xb9, 0x0a, 0x00, 0x00, 0x00,       // mov ecx, 10        (counter = 10)
  // loop_start (0x00400007):
  0x01, 0xc8,                         // add eax, ecx       (sum += counter)
  0xe2, 0xfc,                         // loop -4            (decrement ecx, jump if != 0)
  0x89, 0xc3,                         // mov ebx, eax       (ebx = 55 = 0x37)
  0xcc,                               // int3
]);

// Preset 3: DOS INT 21h String Output HLE Demo
export const DOS_INT21_CODE = new Uint8Array([
  0xba, 0x12, 0x00, 0x40, 0x00,       // mov edx, 0x00400012 (pointer to '$' terminated string)
  0xb4, 0x09,                         // mov ah, 0x09        (DOS Print String)
  0xcd, 0x21,                         // int 0x21
  0xb8, 0x00, 0x4c, 0x00, 0x00,       // mov eax, 0x4C00     (DOS Exit with code 0)
  0xcd, 0x21,                         // int 0x21
  0xcc,                               // int3
  // 0x00400012: "Hello from TinyX86 DOS INT 21h!$"
  0x48, 0x65, 0x6c, 0x6c, 0x6f, 0x20, 0x66, 0x72,
  0x6f, 0x6d, 0x20, 0x54, 0x69, 0x6e, 0x79, 0x58,
  0x38, 0x36, 0x20, 0x44, 0x4f, 0x53, 0x20, 0x49,
  0x4e, 0x54, 0x20, 0x32, 0x31, 0x68, 0x21, 0x24,
]);

export function LoadMem(
  code: Uint8Array,
  mem: VirtualMemory,
  baseAddr = 0x00400000
): CPUContext {
  mem.clear();
  mem.codeBase = baseAddr >>> 0;
  mem.codeSize = Math.max(code.length + 0x100, 0x1000);
  mem.stackBase = 0x00100000;
  mem.stackSize = 0x10000; // 64KB
  mem.loadBlock(baseAddr, code);
  return createInitialCPUContext(baseAddr, mem.stackBase + mem.stackSize - 0x20);
}

export function ParsePEHeaders(buffer: Uint8Array): PEHeaderInfo {
  const fail: PEHeaderInfo = {
    isValidPE: false,
    machine: 0,
    numberOfSections: 0,
    entryPointRVA: 0,
    imageBase: 0,
    sizeOfImage: 0,
    sizeOfHeaders: 0,
    sections: [],
  };
  if (buffer.length < 0x40) return fail;
  const dv = new DataView(buffer.buffer, buffer.byteOffset, buffer.byteLength);
  const mz = dv.getUint16(0, true);
  if (mz !== 0x5a4d) return fail; // 'MZ'

  const e_lfanew = dv.getUint32(0x3c, true);
  if (e_lfanew + 0x18 > buffer.length) return fail;
  const peSig = dv.getUint32(e_lfanew, true);
  if (peSig !== 0x00004550) return fail; // 'PE\0\0'

  const fileHeaderOffset = e_lfanew + 4;
  const machine = dv.getUint16(fileHeaderOffset, true);
  const numberOfSections = dv.getUint16(fileHeaderOffset + 2, true);
  const sizeOfOptionalHeader = dv.getUint16(fileHeaderOffset + 16, true);

  const optHeaderOffset = fileHeaderOffset + 20;
  if (optHeaderOffset + sizeOfOptionalHeader > buffer.length) return fail;
  const magic = dv.getUint16(optHeaderOffset, true);
  if (magic !== 0x010b) {
    // Must be PE32 (0x10B)
    return fail;
  }

  const entryPointRVA = dv.getUint32(optHeaderOffset + 16, true);
  const imageBase = dv.getUint32(optHeaderOffset + 28, true);
  const sizeOfImage = dv.getUint32(optHeaderOffset + 56, true);
  const sizeOfHeaders = dv.getUint32(optHeaderOffset + 60, true);

  const sectionTableOffset = optHeaderOffset + sizeOfOptionalHeader;
  const sections: PESectionInfo[] = [];

  for (let i = 0; i < numberOfSections; i++) {
    const secOff = sectionTableOffset + i * 40;
    if (secOff + 40 > buffer.length) break;
    let name = '';
    for (let j = 0; j < 8; j++) {
      const ch = buffer[secOff + j];
      if (ch === 0) break;
      name += String.fromCharCode(ch);
    }
    const virtualSize = dv.getUint32(secOff + 8, true);
    const virtualAddress = dv.getUint32(secOff + 12, true);
    const sizeOfRawData = dv.getUint32(secOff + 16, true);
    const pointerToRawData = dv.getUint32(secOff + 20, true);
    const characteristics = dv.getUint32(secOff + 36, true);
    sections.push({
      name,
      virtualSize,
      virtualAddress,
      sizeOfRawData,
      pointerToRawData,
      characteristics,
    });
  }

  return {
    isValidPE: true,
    machine,
    numberOfSections,
    entryPointRVA,
    imageBase,
    sizeOfImage,
    sizeOfHeaders,
    sections,
  };
}

export function LoadExeBuffer(
  buffer: Uint8Array,
  mem: VirtualMemory,
  onLog?: (msg: string) => void
): { ctx: CPUContext | null; peInfo: PEHeaderInfo } {
  const peInfo = ParsePEHeaders(buffer);
  if (!peInfo.isValidPE) {
    onLog?.('[Error] Load operation failed! Check file path or PE format (Must be 32-bit PE32).');
    return { ctx: null, peInfo };
  }

  onLog?.(`[Loader Info] Target ImageBase: 0x${peInfo.imageBase.toString(16).toUpperCase().padStart(8, '0')}`);
  mem.clear();
  mem.codeBase = peInfo.imageBase >>> 0;
  mem.codeSize = peInfo.sizeOfImage >>> 0;
  mem.stackBase = 0x00100000;
  mem.stackSize = 0x10000;

  // Copy headers
  const headerSlice = buffer.subarray(0, Math.min(peInfo.sizeOfHeaders, buffer.length));
  mem.loadBlock(peInfo.imageBase, headerSlice);

  // Copy sections
  for (const sec of peInfo.sections) {
    if (sec.sizeOfRawData > 0 && sec.pointerToRawData < buffer.length) {
      const rawEnd = Math.min(sec.pointerToRawData + sec.sizeOfRawData, buffer.length);
      const rawSlice = buffer.subarray(sec.pointerToRawData, rawEnd);
      mem.loadBlock((peInfo.imageBase + sec.virtualAddress) >>> 0, rawSlice);
    }
  }

  const entryAddr = (peInfo.imageBase + peInfo.entryPointRVA) >>> 0;
  onLog?.(
    `[Loader Success] Loaded at 0x${peInfo.imageBase.toString(16).toUpperCase().padStart(8, '0')}. Entry: 0x${peInfo.entryPointRVA.toString(16).toUpperCase().padStart(8, '0')} (VA: 0x${entryAddr.toString(16).toUpperCase().padStart(8, '0')})`
  );
  const ctx = createInitialCPUContext(entryAddr, mem.stackBase + mem.stackSize - 0x20);
  return { ctx, peInfo };
}

export interface IntegrationTestResult {
  passed: boolean;
  checks: number;
  failures: number;
  details: string[];
}

// Directly mirrors tests/cpu_integration_tests.c
export function runCpuIntegrationTests(): IntegrationTestResult {
  let checks = 0;
  let failures = 0;
  const details: string[] = [];

  const check = (expr: boolean, label: string) => {
    checks++;
    if (!expr) {
      failures++;
      details.push(`FAIL: ${label}`);
    } else {
      details.push(`PASS: ${label}`);
    }
  };

  const mem = new VirtualMemory();

  // 1. ALU + Stack + Flags test from tests/cpu_integration_tests.c
  const alu = new Uint8Array([
    0xb8, 40, 0, 0, 0, // mov eax, 40
    0x83, 0xc0, 2,     // add eax, 2
    0x50,              // push eax
    0x5b,              // pop ebx
    0x31, 0xc9,        // xor ecx, ecx
    0x83, 0xfb, 42,    // cmp ebx, 42
  ]);
  let cpu = LoadMem(alu, mem, 0x00400000);
  const initialEsp = cpu.GPR[4] >>> 0;
  const decoded = ParseInstruction((addr) => mem.readU8(addr), cpu.EIP);
  check(decoded.instr_len === 5, 'ParseInstruction(alu) == 5 && decoded.instr_len == 5');
  check(runcpu(cpu, mem, 6) === 0, 'runcpu(&cpu, 6) == 0');
  check(cpu.GPR[0] === 42, 'cpu.EAX.I32 == 42');
  check(cpu.GPR[3] === 42, 'cpu.EBX.I32 == 42');
  check(cpu.GPR[1] === 0, 'cpu.ECX.I32 == 0');
  check(cpu.EFLAGS.ZF === 1, 'cpu.EFLAGS.ZF == 1');
  check(cpu.GPR[4] === initialEsp, 'cpu.ESP.I32 == initial stack top');
  check(cpu.EIP === 0x00400000 + 15, 'cpu.EIP == alu + 15');

  // 2. Branch test from tests/cpu_integration_tests.c
  const branch = new Uint8Array([
    0xeb, 5,           // jmp +5
    0xb8, 99, 0, 0, 0, // mov eax, 99 (skipped)
    0xb8, 42, 0, 0, 0, // mov eax, 42
  ]);
  cpu = LoadMem(branch, mem, 0x00400000);
  check(runcpu(cpu, mem, 2) === 0, 'runcpu(&cpu, 2) == 0 (branch)');
  check(cpu.GPR[0] === 42, 'cpu.EAX.I32 == 42 (branch skipped mov eax, 99)');
  check(cpu.EIP === 0x00400000 + 12, 'cpu.EIP == branch + 12');

  // 3. FPU test from tests/cpu_integration_tests.c
  const fpu = new Uint8Array([
    0xd9, 0xe8,        // fld1
    0xd9, 0xe8,        // fld1
    0xde, 0xc1,        // faddp st(1), st(0)
  ]);
  cpu = LoadMem(fpu, mem, 0x00400000);
  check(runcpu(cpu, mem, 3) === 0, 'runcpu(&cpu, 3) == 0 (fpu)');
  check(cpu.FPU.Regs[cpu.FPU.SW.TOP] === 2.0, 'cpu.FPU.Regs[cpu.FPU.SW.TOP] == 2.0');

  details.push(`TinyX86 + shared decoder: ${checks} checks, ${failures} failures`);
  return {
    passed: failures === 0,
    checks,
    failures,
    details,
  };
}
