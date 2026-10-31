import { ALU_Op, CPUContext, DecodeContext, OperandType } from './types';
import { ParseInstruction, FormatInstruction } from './disasm';

export { ParseInstruction, FormatInstruction };

const PAGE_SIZE = 0x10000; // 64KB pages

export class VirtualMemory {
  private pages = new Map<number, Uint8Array>();
  public codeBase = 0x00400000;
  public codeSize = 0x1000;
  public stackBase = 0x00100000;
  public stackSize = 0x10000; // 64KB

  clear(): void {
    this.pages.clear();
  }

  private getPage(addr: number, allocate = true): Uint8Array | undefined {
    const pageIdx = (addr >>> 16) & 0xffff;
    let page = this.pages.get(pageIdx);
    if (!page && allocate) {
      page = new Uint8Array(PAGE_SIZE);
      this.pages.set(pageIdx, page);
    }
    return page;
  }

  readU8(addr: number): number {
    const uAddr = addr >>> 0;
    const page = this.getPage(uAddr, false);
    if (!page) return 0;
    return page[uAddr & 0xffff];
  }

  writeU8(addr: number, val: number): void {
    const uAddr = addr >>> 0;
    const page = this.getPage(uAddr, true)!;
    page[uAddr & 0xffff] = val & 0xff;
  }

  read(addr: number, byteLen: number): number {
    const uAddr = addr >>> 0;
    if (byteLen === 1) {
      return this.readU8(uAddr);
    }
    if (byteLen === 2) {
      return this.readU8(uAddr) | (this.readU8(uAddr + 1) << 8);
    }
    return (
      (this.readU8(uAddr) |
        (this.readU8(uAddr + 1) << 8) |
        (this.readU8(uAddr + 2) << 16) |
        (this.readU8(uAddr + 3) << 24)) >>>
      0
    );
  }

  write(addr: number, val: number, byteLen: number): void {
    const uAddr = addr >>> 0;
    if (byteLen === 1) {
      this.writeU8(uAddr, val & 0xff);
    } else if (byteLen === 2) {
      this.writeU8(uAddr, val & 0xff);
      this.writeU8(uAddr + 1, (val >>> 8) & 0xff);
    } else {
      this.writeU8(uAddr, val & 0xff);
      this.writeU8(uAddr + 1, (val >>> 8) & 0xff);
      this.writeU8(uAddr + 2, (val >>> 16) & 0xff);
      this.writeU8(uAddr + 3, (val >>> 24) & 0xff);
    }
  }

  readFloat(addr: number, byteLen: number): number {
    const buf = new ArrayBuffer(8);
    const u8 = new Uint8Array(buf);
    const dv = new DataView(buf);
    for (let i = 0; i < byteLen; i++) {
      u8[i] = this.readU8(addr + i);
    }
    return byteLen === 4 ? dv.getFloat32(0, true) : dv.getFloat64(0, true);
  }

  writeFloat(addr: number, val: number, byteLen: number): void {
    const buf = new ArrayBuffer(8);
    const u8 = new Uint8Array(buf);
    const dv = new DataView(buf);
    if (byteLen === 4) {
      dv.setFloat32(0, val, true);
    } else {
      dv.setFloat64(0, val, true);
    }
    for (let i = 0; i < byteLen; i++) {
      this.writeU8(addr + i, u8[i]);
    }
  }

  readIntAsDouble(addr: number, byteLen: number): number {
    const raw = this.read(addr, byteLen);
    if (byteLen === 2) {
      return (raw << 16) >> 16;
    }
    return raw | 0;
  }

  writeDoubleAsInt(addr: number, val: number, byteLen: number): void {
    const truncated = Math.trunc(val);
    this.write(addr, truncated, byteLen);
  }

  loadBlock(baseAddr: number, data: Uint8Array | number[]): void {
    for (let i = 0; i < data.length; i++) {
      this.writeU8(baseAddr + i, data[i]);
    }
  }
}

export function createInitialCPUContext(entryPoint = 0x00400000, stackTop = 0x0010ffe0): CPUContext {
  return {
    GPR: new Uint32Array([0, 0, 0, 0, stackTop >>> 0, stackTop >>> 0, 0, 0]),
    EIP: entryPoint >>> 0,
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
    seg: new Uint16Array([0x23, 0x1b, 0x23, 0x23, 0x00, 0x00]), // ES, CS, SS, DS, FS, GS
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

export function cloneCPUContext(ctx: CPUContext): CPUContext {
  return {
    GPR: new Uint32Array(ctx.GPR),
    EIP: ctx.EIP >>> 0,
    EFLAGS: { ...ctx.EFLAGS },
    seg: new Uint16Array(ctx.seg),
    FPU: {
      Regs: [...ctx.FPU.Regs],
      SW: { ...ctx.FPU.SW },
      CW: ctx.FPU.CW,
      TW: ctx.FPU.TW,
    },
    Halted: ctx.Halted,
  };
}

export function ReadGPR(ctx: CPUContext, regIndex: number, size: number): number {
  if (regIndex < 0 || regIndex >= 8) return 0;
  const full = ctx.GPR[regIndex] >>> 0;
  if (size === 32) return full;
  if (size === 16) return full & 0xffff;
  if (size === 8) {
    if (regIndex < 4) return full & 0xff;
    return ((ctx.GPR[regIndex - 4] >>> 8) & 0xff) >>> 0;
  }
  return 0;
}

export function WriteGPR(ctx: CPUContext, regIndex: number, size: number, value: number): boolean {
  if (regIndex < 0 || regIndex >= 8) return false;
  const uVal = value >>> 0;
  if (size === 32) {
    ctx.GPR[regIndex] = uVal;
  } else if (size === 16) {
    ctx.GPR[regIndex] = ((ctx.GPR[regIndex] & 0xffff0000) | (uVal & 0xffff)) >>> 0;
  } else if (size === 8) {
    if (regIndex < 4) {
      ctx.GPR[regIndex] = ((ctx.GPR[regIndex] & 0xffffff00) | (uVal & 0xff)) >>> 0;
    } else {
      const target = regIndex - 4;
      ctx.GPR[target] = ((ctx.GPR[target] & 0xffff00ff) | ((uVal & 0xff) << 8)) >>> 0;
    }
  }
  return true;
}

export function PackEFLAGS(ctx: CPUContext): number {
  let raw = 0x00000002; // Bit 1 always 1
  if (ctx.EFLAGS.CF) raw |= 1 << 0;
  if (ctx.EFLAGS.PF) raw |= 1 << 2;
  if (ctx.EFLAGS.AF) raw |= 1 << 4;
  if (ctx.EFLAGS.ZF) raw |= 1 << 6;
  if (ctx.EFLAGS.SF) raw |= 1 << 7;
  if (ctx.EFLAGS.TF) raw |= 1 << 8;
  if (ctx.EFLAGS.IF) raw |= 1 << 9;
  if (ctx.EFLAGS.DF) raw |= 1 << 10;
  if (ctx.EFLAGS.OF) raw |= 1 << 11;
  raw |= (ctx.EFLAGS.IOPL & 3) << 12;
  if (ctx.EFLAGS.NT) raw |= 1 << 14;
  return raw >>> 0;
}

export function UnpackEFLAGS(ctx: CPUContext, val: number): void {
  ctx.EFLAGS.CF = (val >>> 0) & 1;
  ctx.EFLAGS.PF = (val >>> 2) & 1;
  ctx.EFLAGS.AF = (val >>> 4) & 1;
  ctx.EFLAGS.ZF = (val >>> 6) & 1;
  ctx.EFLAGS.SF = (val >>> 7) & 1;
  ctx.EFLAGS.TF = (val >>> 8) & 1;
  ctx.EFLAGS.IF = (val >>> 9) & 1;
  ctx.EFLAGS.DF = (val >>> 10) & 1;
  ctx.EFLAGS.OF = (val >>> 11) & 1;
  ctx.EFLAGS.IOPL = (val >>> 12) & 3;
  ctx.EFLAGS.NT = (val >>> 14) & 1;
}

export function PackFPUSW(ctx: CPUContext): number {
  const sw = ctx.FPU.SW;
  let val = 0;
  if (sw.IE) val |= 1 << 0;
  if (sw.DE) val |= 1 << 1;
  if (sw.ZE) val |= 1 << 2;
  if (sw.OE) val |= 1 << 3;
  if (sw.UE) val |= 1 << 4;
  if (sw.PE) val |= 1 << 5;
  if (sw.SF) val |= 1 << 6;
  if (sw.ES) val |= 1 << 7;
  if (sw.C0) val |= 1 << 8;
  if (sw.C1) val |= 1 << 9;
  if (sw.C2) val |= 1 << 10;
  val |= (sw.TOP & 7) << 11;
  if (sw.C3) val |= 1 << 14;
  if (sw.B) val |= 1 << 15;
  return val & 0xffff;
}

function getSTIndex(ctx: CPUContext, i: number): number {
  return (ctx.FPU.SW.TOP + i) & 7;
}

function getST(ctx: CPUContext, i: number): number {
  return ctx.FPU.Regs[getSTIndex(ctx, i)];
}

function setST(ctx: CPUContext, i: number, val: number): void {
  ctx.FPU.Regs[getSTIndex(ctx, i)] = val;
}

function FPU_PUSH(ctx: CPUContext, val: number): void {
  ctx.FPU.SW.TOP = (ctx.FPU.SW.TOP - 1) & 7;
  setST(ctx, 0, val);
}

function FPU_POP(ctx: CPUContext): number {
  const val = getST(ctx, 0);
  ctx.FPU.SW.TOP = (ctx.FPU.SW.TOP + 1) & 7;
  return val;
}

function UpdateFPUFlags(ctx: CPUContext, v1: number, v2: number): void {
  ctx.FPU.SW.C0 = 0;
  ctx.FPU.SW.C2 = 0;
  ctx.FPU.SW.C3 = 0;
  if (Number.isNaN(v1) || Number.isNaN(v2)) {
    ctx.FPU.SW.C0 = 1;
    ctx.FPU.SW.C2 = 1;
    ctx.FPU.SW.C3 = 1;
  } else if (v1 > v2) {
    // all 0
  } else if (v1 === v2) {
    ctx.FPU.SW.C3 = 1;
  } else if (v1 < v2) {
    ctx.FPU.SW.C0 = 1;
  }
}

export function GetEffectiveAddress(ctx: CPUContext, d_ctx: DecodeContext): number {
  let base = 0;
  let index = 0;
  if (d_ctx.has_sib) {
    if (!(d_ctx.base === 5 && d_ctx.mod === 0)) {
      base = ReadGPR(ctx, d_ctx.base, 32);
    }
    if (d_ctx.index !== 4) {
      index = ReadGPR(ctx, d_ctx.index, 32);
    }
  } else {
    if (d_ctx.mod === 0 && d_ctx.rm === 5) {
      base = 0;
    } else {
      base = ReadGPR(ctx, d_ctx.rm, 32);
    }
  }
  return (base + index * (1 << d_ctx.scale) + d_ctx.disp) >>> 0;
}

function GetOperandBitSize(d_ctx: DecodeContext, type: OperandType): number {
  const defOpSize = d_ctx.pfx_op_size === 0x66 ? 16 : 32;
  switch (type) {
    case OperandType.Eb:
    case OperandType.Gb:
    case OperandType.OP_AL:
    case OperandType.OP_CL:
    case OperandType.OP_DL:
    case OperandType.OP_BL:
    case OperandType.OP_AH:
    case OperandType.OP_CH:
    case OperandType.OP_DH:
    case OperandType.OP_BH:
    case OperandType.Ib:
      return 8;
    case OperandType.Ew:
    case OperandType.Gw:
    case OperandType.OP_DX:
    case OperandType.Iw:
      return 16;
    case OperandType.Ev:
    case OperandType.Gv:
    case OperandType.Gz:
    case OperandType.OP_rAX:
    case OperandType.OP_rCX:
    case OperandType.OP_rDX:
    case OperandType.OP_rBX:
    case OperandType.OP_rSP:
    case OperandType.OP_rBP:
    case OperandType.OP_rSI:
    case OperandType.OP_rDI:
    case OperandType.Iv:
    case OperandType.Iz:
      return defOpSize;
    default:
      return defOpSize;
  }
}

function GetOperandValue(ctx: CPUContext, mem: VirtualMemory, d_ctx: DecodeContext, opIdx: number): number {
  const type = opIdx === 0 ? d_ctx.entry.op1 : opIdx === 1 ? d_ctx.entry.op2 : d_ctx.entry.op3;
  const opSize = d_ctx.pfx_op_size === 0x66 ? 16 : 32;

  switch (type) {
    case OperandType.Gb: return ReadGPR(ctx, d_ctx.reg, 8);
    case OperandType.Gw: return ReadGPR(ctx, d_ctx.reg, 16);
    case OperandType.Gv: return ReadGPR(ctx, d_ctx.reg, opSize);
    case OperandType.OP_AL:
    case OperandType.OP_CL:
    case OperandType.OP_DL:
    case OperandType.OP_BL:
    case OperandType.OP_AH:
    case OperandType.OP_CH:
    case OperandType.OP_DH:
    case OperandType.OP_BH:
      return ReadGPR(ctx, type - OperandType.OP_AL, 8);
    case OperandType.OP_DX:
      return ReadGPR(ctx, 2, 16);
    case OperandType.OP_rAX:
    case OperandType.OP_rCX:
    case OperandType.OP_rDX:
    case OperandType.OP_rBX:
    case OperandType.OP_rSP:
    case OperandType.OP_rBP:
    case OperandType.OP_rSI:
    case OperandType.OP_rDI:
      return ReadGPR(ctx, type - OperandType.OP_rAX, opSize);
    case OperandType.Sw:
      return d_ctx.reg < 6 ? ctx.seg[d_ctx.reg] : 0;
    case OperandType.OP_ES: return ctx.seg[0];
    case OperandType.OP_CS: return ctx.seg[1];
    case OperandType.OP_SS: return ctx.seg[2];
    case OperandType.OP_DS: return ctx.seg[3];
    case OperandType.OP_FS: return ctx.seg[4];
    case OperandType.OP_GS: return ctx.seg[5];
    case OperandType.Eb:
    case OperandType.Ew:
    case OperandType.Ev: {
      if (d_ctx.mod === 3) {
        const sizeE = type === OperandType.Eb ? 8 : type === OperandType.Ew ? 16 : opSize;
        return ReadGPR(ctx, d_ctx.rm, sizeE);
      }
      const addr = GetEffectiveAddress(ctx, d_ctx);
      const byteLen = type === OperandType.Eb ? 1 : type === OperandType.Ew ? 2 : opSize / 8;
      return mem.read(addr, byteLen);
    }
    case OperandType.Ib:
    case OperandType.Iw:
    case OperandType.Iv:
    case OperandType.Iz:
      return d_ctx.imm >>> 0;
    default:
      return 0;
  }
}

function SetOperandValue(ctx: CPUContext, mem: VirtualMemory, d_ctx: DecodeContext, opIdx: number, value: number): void {
  const type = opIdx === 0 ? d_ctx.entry.op1 : d_ctx.entry.op2;
  const opSize = d_ctx.pfx_op_size === 0x66 ? 16 : 32;
  const uVal = value >>> 0;

  switch (type) {
    case OperandType.Gb: WriteGPR(ctx, d_ctx.reg, 8, uVal); break;
    case OperandType.Gw: WriteGPR(ctx, d_ctx.reg, 16, uVal); break;
    case OperandType.Gv: WriteGPR(ctx, d_ctx.reg, opSize, uVal); break;
    case OperandType.Eb:
    case OperandType.Ew:
    case OperandType.Ev: {
      if (d_ctx.mod === 3) {
        const sizeE = type === OperandType.Eb ? 8 : type === OperandType.Ew ? 16 : opSize;
        WriteGPR(ctx, d_ctx.rm, sizeE, uVal);
      } else {
        const addr = GetEffectiveAddress(ctx, d_ctx);
        const byteLen = type === OperandType.Eb ? 1 : type === OperandType.Ew ? 2 : opSize / 8;
        mem.write(addr, uVal, byteLen);
      }
      break;
    }
    case OperandType.OP_AL:
    case OperandType.OP_CL:
    case OperandType.OP_DL:
    case OperandType.OP_BL:
    case OperandType.OP_AH:
    case OperandType.OP_CH:
    case OperandType.OP_DH:
    case OperandType.OP_BH:
      WriteGPR(ctx, type - OperandType.OP_AL, 8, uVal);
      break;
    case OperandType.OP_DX:
      WriteGPR(ctx, 2, 16, uVal);
      break;
    case OperandType.OP_rAX:
    case OperandType.OP_rCX:
    case OperandType.OP_rDX:
    case OperandType.OP_rBX:
    case OperandType.OP_rSP:
    case OperandType.OP_rBP:
    case OperandType.OP_rSI:
    case OperandType.OP_rDI:
      WriteGPR(ctx, type - OperandType.OP_rAX, opSize, uVal);
      break;
    case OperandType.Sw:
      if (d_ctx.reg < 6) ctx.seg[d_ctx.reg] = uVal & 0xffff;
      break;
    case OperandType.OP_ES: ctx.seg[0] = uVal & 0xffff; break;
    case OperandType.OP_CS: ctx.seg[1] = uVal & 0xffff; break;
    case OperandType.OP_SS: ctx.seg[2] = uVal & 0xffff; break;
    case OperandType.OP_DS: ctx.seg[3] = uVal & 0xffff; break;
    case OperandType.OP_FS: ctx.seg[4] = uVal & 0xffff; break;
    case OperandType.OP_GS: ctx.seg[5] = uVal & 0xffff; break;
  }
}

function CalcPF(res: number): number {
  let count = 0;
  const b = res & 0xff;
  for (let i = 0; i < 8; i++) {
    if (b & (1 << i)) count++;
  }
  return count % 2 === 0 ? 1 : 0;
}

function UpdateLogicFlags(ctx: CPUContext, res: number, size: number): void {
  const mask = size === 8 ? 0xff : size === 16 ? 0xffff : 0xffffffff;
  const signMask = size === 8 ? 0x80 : size === 16 ? 0x8000 : 0x80000000;
  const truncated = (res & mask) >>> 0;
  ctx.EFLAGS.ZF = truncated === 0 ? 1 : 0;
  ctx.EFLAGS.SF = (truncated & signMask) !== 0 ? 1 : 0;
  ctx.EFLAGS.PF = CalcPF(truncated);
}

function UpdateEFLAGS(
  ctx: CPUContext,
  res: number,
  dest: number,
  src: number,
  size: number,
  op: ALU_Op
): void {
  const signMask = size === 8 ? 0x80 : size === 16 ? 0x8000 : 0x80000000;
  const mask = size === 8 ? 0xff : size === 16 ? 0xffff : 0xffffffff;
  const truncatedRes = (res & mask) >>> 0;
  const uDest = (dest & mask) >>> 0;
  const uSrc = (src & mask) >>> 0;

  ctx.EFLAGS.ZF = truncatedRes === 0 ? 1 : 0;
  ctx.EFLAGS.SF = (truncatedRes & signMask) !== 0 ? 1 : 0;
  ctx.EFLAGS.PF = CalcPF(truncatedRes);

  if (op === ALU_Op.ALU_AND || op === ALU_Op.ALU_OR || op === ALU_Op.ALU_XOR) {
    ctx.EFLAGS.CF = 0;
    ctx.EFLAGS.OF = 0;
    ctx.EFLAGS.AF = 0;
  } else if (op === ALU_Op.ALU_ADD || op === ALU_Op.ALU_ADC) {
    ctx.EFLAGS.CF = truncatedRes < uDest || (op === ALU_Op.ALU_ADC && ctx.EFLAGS.CF && truncatedRes === uDest) ? 1 : 0;
    ctx.EFLAGS.OF = (((uDest ^ truncatedRes) & (uSrc ^ truncatedRes)) & signMask) !== 0 ? 1 : 0;
    ctx.EFLAGS.AF = ((uDest ^ uSrc ^ truncatedRes) & 0x10) !== 0 ? 1 : 0;
  } else if (op === ALU_Op.ALU_SUB || op === ALU_Op.ALU_CMP || op === ALU_Op.ALU_SBB) {
    ctx.EFLAGS.CF = uDest < uSrc ? 1 : 0;
    ctx.EFLAGS.OF = (((uDest ^ uSrc) & (uDest ^ truncatedRes)) & signMask) !== 0 ? 1 : 0;
    ctx.EFLAGS.AF = ((uDest ^ uSrc ^ truncatedRes) & 0x10) !== 0 ? 1 : 0;
  } else if (op === ALU_Op.ALU_INC) {
    ctx.EFLAGS.OF = (((uDest ^ truncatedRes) & (uSrc ^ truncatedRes)) & signMask) !== 0 ? 1 : 0;
    ctx.EFLAGS.AF = ((uDest ^ uSrc ^ truncatedRes) & 0x10) !== 0 ? 1 : 0;
  } else if (op === ALU_Op.ALU_DEC) {
    ctx.EFLAGS.OF = (((uDest ^ uSrc) & (uDest ^ truncatedRes)) & signMask) !== 0 ? 1 : 0;
    ctx.EFLAGS.AF = ((uDest ^ uSrc ^ truncatedRes) & 0x10) !== 0 ? 1 : 0;
  }
}

export function CheckCondition(ctx: CPUContext, conditionCode: number): boolean {
  switch (conditionCode & 0x0f) {
    case 0x0: return ctx.EFLAGS.OF === 1;
    case 0x1: return ctx.EFLAGS.OF === 0;
    case 0x2: return ctx.EFLAGS.CF === 1;
    case 0x3: return ctx.EFLAGS.CF === 0;
    case 0x4: return ctx.EFLAGS.ZF === 1;
    case 0x5: return ctx.EFLAGS.ZF === 0;
    case 0x6: return (ctx.EFLAGS.CF | ctx.EFLAGS.ZF) === 1;
    case 0x7: return (ctx.EFLAGS.CF | ctx.EFLAGS.ZF) === 0;
    case 0x8: return ctx.EFLAGS.SF === 1;
    case 0x9: return ctx.EFLAGS.SF === 0;
    case 0xa: return ctx.EFLAGS.PF === 1;
    case 0xb: return ctx.EFLAGS.PF === 0;
    case 0xc: return ctx.EFLAGS.SF !== ctx.EFLAGS.OF;
    case 0xd: return ctx.EFLAGS.SF === ctx.EFLAGS.OF;
    case 0xe: return ctx.EFLAGS.ZF === 1 || ctx.EFLAGS.SF !== ctx.EFLAGS.OF;
    case 0xf: return ctx.EFLAGS.ZF === 0 && ctx.EFLAGS.SF === ctx.EFLAGS.OF;
    default: return false;
  }
}

function Exec_ALU_Generic(
  ctx: CPUContext,
  mem: VirtualMemory,
  d_ctx: DecodeContext,
  op: ALU_Op,
  isCompare: boolean
): void {
  const sizeD = GetOperandBitSize(d_ctx, d_ctx.entry.op1);
  const sizeS = GetOperandBitSize(d_ctx, d_ctx.entry.op2);
  let dest = GetOperandValue(ctx, mem, d_ctx, 0);
  let src = GetOperandValue(ctx, mem, d_ctx, 1);

  const maskD = sizeD === 8 ? 0xff : sizeD === 16 ? 0xffff : 0xffffffff;
  const maskS = sizeS === 8 ? 0xff : sizeS === 16 ? 0xffff : 0xffffffff;
  dest = (dest & maskD) >>> 0;
  src = (src & maskS) >>> 0;

  let res = 0;
  const cfIn = ctx.EFLAGS.CF;
  switch (op) {
    case ALU_Op.ALU_ADD: res = (dest + src) >>> 0; break;
    case ALU_Op.ALU_OR: res = (dest | src) >>> 0; break;
    case ALU_Op.ALU_ADC: res = (dest + src + cfIn) >>> 0; break;
    case ALU_Op.ALU_SBB: res = (dest - src - cfIn) >>> 0; break;
    case ALU_Op.ALU_AND: res = (dest & src) >>> 0; break;
    case ALU_Op.ALU_CMP:
    case ALU_Op.ALU_SUB: res = (dest - src) >>> 0; break;
    case ALU_Op.ALU_XOR: res = (dest ^ src) >>> 0; break;
    case ALU_Op.ALU_INC: res = (dest + 1) >>> 0; break;
    case ALU_Op.ALU_DEC: res = (dest - 1) >>> 0; break;
  }
  const truncatedRes = (res & maskD) >>> 0;
  UpdateEFLAGS(ctx, truncatedRes, dest, src, sizeD, op);
  if (!isCompare) {
    SetOperandValue(ctx, mem, d_ctx, 0, truncatedRes);
  }
}

function Exec_Group1(ctx: CPUContext, mem: VirtualMemory, d_ctx: DecodeContext): void {
  switch (d_ctx.reg) {
    case 0: Exec_ALU_Generic(ctx, mem, d_ctx, ALU_Op.ALU_ADD, false); break;
    case 1: Exec_ALU_Generic(ctx, mem, d_ctx, ALU_Op.ALU_OR, false); break;
    case 2: Exec_ALU_Generic(ctx, mem, d_ctx, ALU_Op.ALU_ADC, false); break;
    case 3: Exec_ALU_Generic(ctx, mem, d_ctx, ALU_Op.ALU_SBB, false); break;
    case 4: Exec_ALU_Generic(ctx, mem, d_ctx, ALU_Op.ALU_AND, false); break;
    case 5: Exec_ALU_Generic(ctx, mem, d_ctx, ALU_Op.ALU_SUB, false); break;
    case 6: Exec_ALU_Generic(ctx, mem, d_ctx, ALU_Op.ALU_XOR, false); break;
    case 7: Exec_ALU_Generic(ctx, mem, d_ctx, ALU_Op.ALU_CMP, true); break;
  }
}

function Exec_Group2(ctx: CPUContext, mem: VirtualMemory, d_ctx: DecodeContext): void {
  const size = GetOperandBitSize(d_ctx, d_ctx.entry.op1);
  let val = GetOperandValue(ctx, mem, d_ctx, 0);
  const mask = size === 32 ? 0xffffffff : (1 << size) - 1;
  val = (val & mask) >>> 0;
  const msbMask = (1 << (size - 1)) >>> 0;

  let count = 0;
  if (d_ctx.opcode === 0xc0 || d_ctx.opcode === 0xc1) count = d_ctx.imm >>> 0;
  else if (d_ctx.opcode === 0xd0 || d_ctx.opcode === 0xd1) count = 1;
  else if (d_ctx.opcode === 0xd2 || d_ctx.opcode === 0xd3) count = ReadGPR(ctx, 1, 8);
  count &= 0x1f;
  if (count === 0) return;

  let res = val;
  const originalMsb = (val & msbMask) >>> 0;

  switch (d_ctx.reg) {
    case 0: // ROL
      for (let i = 0; i < count; i++) {
        const bit = (res & msbMask) !== 0 ? 1 : 0;
        ctx.EFLAGS.CF = bit;
        res = ((res << 1) | bit) >>> 0;
      }
      if (count === 1) ctx.EFLAGS.OF = ((res & msbMask) !== 0 ? 1 : 0) ^ ctx.EFLAGS.CF;
      break;
    case 1: // ROR
      for (let i = 0; i < count; i++) {
        const bit = res & 1;
        res = (res >>> 1) & mask;
        ctx.EFLAGS.CF = bit;
        if (bit) res = (res | msbMask) >>> 0;
      }
      if (count === 1) {
        const msb = (res >>> (size - 1)) & 1;
        const msbMinus1 = (res >>> (size - 2)) & 1;
        ctx.EFLAGS.OF = msb ^ msbMinus1;
      }
      break;
    case 2: // RCL
      for (let i = 0; i < count; i++) {
        const bit = (res & msbMask) !== 0 ? 1 : 0;
        res = (res << 1) >>> 0;
        if (ctx.EFLAGS.CF) res |= 1;
        ctx.EFLAGS.CF = bit;
      }
      if (count === 1) ctx.EFLAGS.OF = ((res & msbMask) !== 0 ? 1 : 0) ^ ctx.EFLAGS.CF;
      break;
    case 3: // RCR
      for (let i = 0; i < count; i++) {
        const bit = res & 1;
        res = (res >>> 1) & mask;
        if (ctx.EFLAGS.CF) res = (res | msbMask) >>> 0;
        ctx.EFLAGS.CF = bit;
      }
      if (count === 1) {
        const msb = (res >>> (size - 1)) & 1;
        const msbMinus1 = (res >>> (size - 2)) & 1;
        ctx.EFLAGS.OF = msb ^ msbMinus1;
      }
      break;
    case 4:
    case 6: // SHL / SAL
      for (let i = 0; i < count; i++) {
        ctx.EFLAGS.CF = (res & msbMask) !== 0 ? 1 : 0;
        res = (res << 1) >>> 0;
      }
      if (count === 1) ctx.EFLAGS.OF = ctx.EFLAGS.CF ^ ((res & msbMask) !== 0 ? 1 : 0);
      break;
    case 5: // SHR
      for (let i = 0; i < count; i++) {
        ctx.EFLAGS.CF = res & 1;
        res = (res >>> 1) & mask;
      }
      if (count === 1) ctx.EFLAGS.OF = originalMsb !== 0 ? 1 : 0;
      break;
    case 7: // SAR
      for (let i = 0; i < count; i++) {
        ctx.EFLAGS.CF = res & 1;
        res = (res >>> 1) & mask;
        if (originalMsb !== 0) res = (res | msbMask) >>> 0;
      }
      if (count === 1) ctx.EFLAGS.OF = 0;
      break;
  }

  res = (res & mask) >>> 0;
  UpdateLogicFlags(ctx, res, size);
  SetOperandValue(ctx, mem, d_ctx, 0, res);
}

function Exec_Group3(ctx: CPUContext, mem: VirtualMemory, d_ctx: DecodeContext, onLog?: (s: string) => void): void {
  const size = GetOperandBitSize(d_ctx, d_ctx.entry.op1);
  let src = d_ctx.reg >= 4 ? GetOperandValue(ctx, mem, d_ctx, 1) : GetOperandValue(ctx, mem, d_ctx, 0);
  if (size === 8) src &= 0xff;
  else if (size === 16) src &= 0xffff;
  src >>>= 0;

  switch (d_ctx.reg) {
    case 0:
    case 1:
      Exec_ALU_Generic(ctx, mem, d_ctx, ALU_Op.ALU_AND, true);
      break;
    case 2: // NOT
      SetOperandValue(ctx, mem, d_ctx, 0, (~src) >>> 0);
      break;
    case 3: { // NEG
      const res = (0 - src) >>> 0;
      SetOperandValue(ctx, mem, d_ctx, 0, res);
      UpdateEFLAGS(ctx, res, 0, src, size, ALU_Op.ALU_SUB);
      break;
    }
    case 4: { // MUL
      if (size === 8) {
        const al = ReadGPR(ctx, 0, 8);
        const res = al * src;
        WriteGPR(ctx, 0, 16, res);
        ctx.EFLAGS.CF = ctx.EFLAGS.OF = (res & 0xff00) !== 0 ? 1 : 0;
      } else if (size === 16) {
        const ax = ReadGPR(ctx, 0, 16);
        const res = ax * src;
        WriteGPR(ctx, 2, 16, (res >>> 16) & 0xffff);
        WriteGPR(ctx, 0, 16, res & 0xffff);
        ctx.EFLAGS.CF = ctx.EFLAGS.OF = ((res >>> 16) & 0xffff) !== 0 ? 1 : 0;
      } else {
        const eax = BigInt(ReadGPR(ctx, 0, 32));
        const res = eax * BigInt(src >>> 0);
        const edxVal = Number((res >> 32n) & 0xffffffffn) >>> 0;
        const eaxVal = Number(res & 0xffffffffn) >>> 0;
        WriteGPR(ctx, 2, 32, edxVal);
        WriteGPR(ctx, 0, 32, eaxVal);
        ctx.EFLAGS.CF = ctx.EFLAGS.OF = edxVal !== 0 ? 1 : 0;
      }
      break;
    }
    case 5: { // IMUL
      if (size === 32) {
        const eax = BigInt(ReadGPR(ctx, 0, 32) | 0);
        const val = BigInt(src | 0);
        const res = eax * val;
        const edxVal = Number((res >> 32n) & 0xffffffffn) >>> 0;
        const eaxVal = Number(res & 0xffffffffn) >>> 0;
        WriteGPR(ctx, 2, 32, edxVal);
        WriteGPR(ctx, 0, 32, eaxVal);
        const signExt = BigInt(eaxVal | 0);
        ctx.EFLAGS.CF = ctx.EFLAGS.OF = res !== signExt ? 1 : 0;
      } else if (size === 16) {
        const ax = (ReadGPR(ctx, 0, 16) << 16) >> 16;
        const val = (src << 16) >> 16;
        const res = ax * val;
        WriteGPR(ctx, 2, 16, (res >> 16) & 0xffff);
        WriteGPR(ctx, 0, 16, res & 0xffff);
        ctx.EFLAGS.CF = ctx.EFLAGS.OF = res !== ((res << 16) >> 16) ? 1 : 0;
      } else {
        const al = (ReadGPR(ctx, 0, 8) << 24) >> 24;
        const val = (src << 24) >> 24;
        const res = al * val;
        WriteGPR(ctx, 0, 16, res & 0xffff);
        ctx.EFLAGS.CF = ctx.EFLAGS.OF = res !== ((res << 24) >> 24) ? 1 : 0;
      }
      break;
    }
    case 6: { // DIV
      if (src === 0) {
        onLog?.(`[CPU Error] Division by Zero at EIP=0x${ctx.EIP.toString(16).toUpperCase().padStart(8, '0')}`);
        return;
      }
      if (size === 32) {
        const dividend = (BigInt(ReadGPR(ctx, 2, 32)) << 32n) | BigInt(ReadGPR(ctx, 0, 32));
        const divisor = BigInt(src >>> 0);
        const quo = dividend / divisor;
        const rem = dividend % divisor;
        if (quo > 0xffffffffn) {
          onLog?.(`[CPU Error] quotient overflow at EIP=0x${ctx.EIP.toString(16).toUpperCase().padStart(8, '0')}`);
          return;
        }
        WriteGPR(ctx, 0, 32, Number(quo) >>> 0);
        WriteGPR(ctx, 2, 32, Number(rem) >>> 0);
      } else if (size === 16) {
        const dividend = ((ReadGPR(ctx, 2, 16) << 16) | ReadGPR(ctx, 0, 16)) >>> 0;
        const quo = Math.floor(dividend / src);
        const rem = dividend % src;
        if (quo > 0xffff) return;
        WriteGPR(ctx, 0, 16, quo);
        WriteGPR(ctx, 2, 16, rem);
      } else {
        const dividend = ReadGPR(ctx, 0, 16);
        const quo = Math.floor(dividend / src);
        const rem = dividend % src;
        if (quo > 0xff) return;
        WriteGPR(ctx, 0, 8, quo);
        WriteGPR(ctx, 4, 8, rem); // AH
      }
      break;
    }
    case 7: { // IDIV
      if (src === 0) {
        onLog?.(`[CPU Error] Division by Zero at EIP=0x${ctx.EIP.toString(16).toUpperCase().padStart(8, '0')}`);
        return;
      }
      if (size === 32) {
        const hi = BigInt(ReadGPR(ctx, 2, 32) | 0);
        const lo = BigInt(ReadGPR(ctx, 0, 32) >>> 0);
        const dividend = (hi << 32n) | lo;
        const divisor = BigInt(src | 0);
        const quo = dividend / divisor;
        const rem = dividend % divisor;
        if (quo > 2147483647n || quo < -2147483648n) {
          onLog?.(`[CPU Error] IDIV Quotient Overflow at EIP=0x${ctx.EIP.toString(16).toUpperCase().padStart(8, '0')}`);
          return;
        }
        WriteGPR(ctx, 0, 32, Number(quo & 0xffffffffn) >>> 0);
        WriteGPR(ctx, 2, 32, Number(rem & 0xffffffffn) >>> 0);
      } else if (size === 16) {
        const dividend = (ReadGPR(ctx, 2, 16) << 16) | ReadGPR(ctx, 0, 16);
        const divisor = (src << 16) >> 16;
        const quo = Math.trunc(dividend / divisor);
        const rem = dividend % divisor;
        if (quo > 32767 || quo < -32768) return;
        WriteGPR(ctx, 0, 16, quo & 0xffff);
        WriteGPR(ctx, 2, 16, rem & 0xffff);
      } else {
        const dividend = (ReadGPR(ctx, 0, 16) << 16) >> 16;
        const divisor = (src << 24) >> 24;
        const quo = Math.trunc(dividend / divisor);
        const rem = dividend % divisor;
        if (quo > 127 || quo < -128) return;
        WriteGPR(ctx, 0, 8, quo & 0xff);
        WriteGPR(ctx, 4, 8, rem & 0xff);
      }
      break;
    }
  }
}

function Exec_StringOp(ctx: CPUContext, mem: VirtualMemory, d_ctx: DecodeContext): void {
  const bitSz = (d_ctx.opcode & 1) ? (d_ctx.pfx_op_size === 0x66 ? 16 : 32) : 8;
  const byteLen = bitSz / 8;
  const step = ctx.EFLAGS.DF === 0 ? byteLen : -byteLen;
  const esi = ctx.GPR[6] >>> 0;
  const edi = ctx.GPR[7] >>> 0;

  switch (d_ctx.opcode & 0xfe) {
    case 0xa4: { // MOVS
      mem.write(edi, mem.read(esi, byteLen), byteLen);
      ctx.GPR[6] = (esi + step) >>> 0;
      ctx.GPR[7] = (edi + step) >>> 0;
      break;
    }
    case 0xa6: { // CMPS
      const valSrc = mem.read(esi, byteLen);
      const valDest = mem.read(edi, byteLen);
      const res = (valDest - valSrc) >>> 0;
      UpdateEFLAGS(ctx, res, valDest, valSrc, bitSz, ALU_Op.ALU_SUB);
      ctx.GPR[7] = (edi + step) >>> 0;
      ctx.GPR[6] = (esi + step) >>> 0;
      break;
    }
    case 0xaa: { // STOS
      const val = ReadGPR(ctx, 0, bitSz);
      mem.write(edi, val, byteLen);
      ctx.GPR[7] = (edi + step) >>> 0;
      break;
    }
    case 0xac: { // LODS
      const val = mem.read(esi, byteLen);
      WriteGPR(ctx, 0, bitSz, val);
      ctx.GPR[6] = (esi + step) >>> 0;
      break;
    }
    case 0xae: { // SCAS
      const valDest = mem.read(edi, byteLen);
      const valSrc = ReadGPR(ctx, 0, bitSz);
      const res = (valDest - valSrc) >>> 0;
      UpdateEFLAGS(ctx, res, valDest, valSrc, bitSz, ALU_Op.ALU_SUB);
      ctx.GPR[7] = (edi + step) >>> 0;
      break;
    }
  }
}

function Exec_REP_StringOp(ctx: CPUContext, mem: VirtualMemory, d_ctx: DecodeContext): boolean {
  if ((ctx.GPR[1] >>> 0) === 0) return false;
  Exec_StringOp(ctx, mem, d_ctx);
  ctx.GPR[1] = (ctx.GPR[1] - 1) >>> 0;
  let shouldContinue = (ctx.GPR[1] >>> 0) > 0;
  const isCompareOp = (d_ctx.opcode & 0xa6) === 0xa6;
  if (isCompareOp && shouldContinue) {
    if (d_ctx.pfx_rep) shouldContinue = ctx.EFLAGS.ZF === 1;
    else if (d_ctx.pfx_repne) shouldContinue = ctx.EFLAGS.ZF === 0;
  }
  return shouldContinue;
}

function Exec_FPU(ctx: CPUContext, mem: VirtualMemory, d_ctx: DecodeContext): void {
  const opGroup = d_ctx.opcode;
  const subOp = d_ctx.reg;
  const isMem = d_ctx.mod !== 3;

  if (isMem) {
    const addr = GetEffectiveAddress(ctx, d_ctx);
    if (opGroup === 0xd8 || opGroup === 0xda || opGroup === 0xdc || opGroup === 0xde) {
      let src = 0.0;
      if (opGroup === 0xd8) src = mem.readFloat(addr, 4);
      else if (opGroup === 0xdc) src = mem.readFloat(addr, 8);
      else if (opGroup === 0xda) src = mem.readIntAsDouble(addr, 4);
      else if (opGroup === 0xde) src = mem.readIntAsDouble(addr, 2);

      const st0 = getST(ctx, 0);
      switch (subOp) {
        case 0: setST(ctx, 0, st0 + src); break;
        case 1: setST(ctx, 0, st0 * src); break;
        case 2: UpdateFPUFlags(ctx, st0, src); break;
        case 3: UpdateFPUFlags(ctx, st0, src); FPU_POP(ctx); break;
        case 4: setST(ctx, 0, st0 - src); break;
        case 5: setST(ctx, 0, src - st0); break;
        case 6: setST(ctx, 0, st0 / src); break;
        case 7: setST(ctx, 0, src / st0); break;
      }
    } else {
      let size = 4;
      let isInt = false;
      if (opGroup === 0xd9) size = 4;
      else if (opGroup === 0xdd) size = 8;
      else if (opGroup === 0xdb) { size = 4; isInt = true; }
      else if (opGroup === 0xdf) { size = 2; isInt = true; }

      switch (subOp) {
        case 0:
          FPU_PUSH(ctx, isInt ? mem.readIntAsDouble(addr, size) : mem.readFloat(addr, size));
          break;
        case 2:
          if (isInt) mem.writeDoubleAsInt(addr, getST(ctx, 0), size);
          else mem.writeFloat(addr, getST(ctx, 0), size);
          break;
        case 3:
          if (isInt) mem.writeDoubleAsInt(addr, FPU_POP(ctx), size);
          else mem.writeFloat(addr, FPU_POP(ctx), size);
          break;
        case 5:
          if (opGroup === 0xdf) {
            const low = BigInt(mem.read(addr, 4) >>> 0);
            const high = BigInt(mem.read(addr + 4, 4) | 0);
            const val64 = (high << 32n) | low;
            FPU_PUSH(ctx, Number(val64));
          } else if (opGroup === 0xd9) {
            ctx.FPU.CW = mem.read(addr, 2) & 0xffff;
          }
          break;
        case 7:
          if (opGroup === 0xdf) {
            const val64 = BigInt(Math.trunc(FPU_POP(ctx)));
            mem.write(addr, Number(val64 & 0xffffffffn) >>> 0, 4);
            mem.write(addr + 4, Number((val64 >> 32n) & 0xffffffffn) >>> 0, 4);
          } else if (opGroup === 0xd9) {
            mem.write(addr, ctx.FPU.CW & 0xffff, 2);
          } else if (opGroup === 0xdd) {
            mem.write(addr, PackFPUSW(ctx), 2);
          }
          break;
      }
    }
  } else {
    const i = d_ctx.rm;
    if (opGroup === 0xd8) {
      const st0 = getST(ctx, 0);
      const sti = getST(ctx, i);
      switch (subOp) {
        case 0: setST(ctx, 0, st0 + sti); break;
        case 1: setST(ctx, 0, st0 * sti); break;
        case 2: UpdateFPUFlags(ctx, st0, sti); break;
        case 3: UpdateFPUFlags(ctx, st0, sti); FPU_POP(ctx); break;
        case 4: setST(ctx, 0, st0 - sti); break;
        case 5: setST(ctx, 0, sti - st0); break;
        case 6: setST(ctx, 0, st0 / sti); break;
        case 7: setST(ctx, 0, sti / st0); break;
      }
    } else if (opGroup === 0xdc) {
      const st0 = getST(ctx, 0);
      const sti = getST(ctx, i);
      switch (subOp) {
        case 0: setST(ctx, i, sti + st0); break;
        case 1: setST(ctx, i, sti * st0); break;
        case 4: setST(ctx, i, st0 - sti); break;
        case 5: setST(ctx, i, sti - st0); break;
        case 6: setST(ctx, i, st0 / sti); break;
        case 7: setST(ctx, i, sti / st0); break;
      }
    } else if (opGroup === 0xde) {
      const st0 = getST(ctx, 0);
      const sti = getST(ctx, i);
      switch (subOp) {
        case 0: setST(ctx, i, sti + st0); FPU_POP(ctx); break;
        case 1: setST(ctx, i, sti * st0); FPU_POP(ctx); break;
        case 3:
          if (i === 1) {
            UpdateFPUFlags(ctx, st0, sti);
            FPU_POP(ctx);
            FPU_POP(ctx);
          }
          break;
        case 4: setST(ctx, i, st0 - sti); FPU_POP(ctx); break;
        case 5: setST(ctx, i, sti - st0); FPU_POP(ctx); break;
        case 6: setST(ctx, i, st0 / sti); FPU_POP(ctx); break;
        case 7: setST(ctx, i, sti / st0); FPU_POP(ctx); break;
      }
    } else if (opGroup === 0xda) {
      if (subOp === 5 && i === 1) {
        UpdateFPUFlags(ctx, getST(ctx, 0), getST(ctx, 1));
        FPU_POP(ctx);
        FPU_POP(ctx);
      }
    } else if (opGroup === 0xdb) {
      if (subOp === 4) {
        if (i === 2) {
          ctx.FPU.SW.IE = ctx.FPU.SW.DE = ctx.FPU.SW.ZE = ctx.FPU.SW.OE = ctx.FPU.SW.UE = ctx.FPU.SW.PE = ctx.FPU.SW.SF = ctx.FPU.SW.ES = ctx.FPU.SW.B = 0;
        } else if (i === 3) {
          ctx.FPU.SW.TOP = 0;
          ctx.FPU.CW = 0x037f;
        }
      }
    } else if (opGroup === 0xdf && subOp === 4 && i === 0) {
      WriteGPR(ctx, 0, 16, PackFPUSW(ctx));
    } else if (opGroup === 0xd9) {
      if (subOp === 4) {
        if (i === 0) setST(ctx, 0, -getST(ctx, 0));
        else if (i === 1) setST(ctx, 0, Math.abs(getST(ctx, 0)));
      } else if (subOp === 5) {
        switch (i) {
          case 0: FPU_PUSH(ctx, 1.0); break;
          case 1: FPU_PUSH(ctx, Math.log2(10.0)); break;
          case 2: FPU_PUSH(ctx, Math.log2(Math.E)); break;
          case 3: FPU_PUSH(ctx, Math.PI); break;
          case 4: FPU_PUSH(ctx, Math.log10(2.0)); break;
          case 5: FPU_PUSH(ctx, Math.log(2.0)); break;
          case 6: FPU_PUSH(ctx, 0.0); break;
        }
      }
    }
  }
}

export function ExecuteInstruction(
  ctx: CPUContext,
  mem: VirtualMemory,
  d_ctx: DecodeContext,
  onLog?: (msg: string) => void
): boolean {
  let jumped = false;
  const op = d_ctx.opcode;

  if (d_ctx.is_two_byte_opcode) {
    if (op >= 0x40 && op <= 0x4f) {
      if (CheckCondition(ctx, op)) {
        SetOperandValue(ctx, mem, d_ctx, 0, GetOperandValue(ctx, mem, d_ctx, 1));
      }
      return false;
    }
    if (op >= 0x80 && op <= 0x8f) {
      if (CheckCondition(ctx, op)) {
        ctx.EIP = (ctx.EIP + d_ctx.instr_len + d_ctx.imm) >>> 0;
        return true;
      }
      return false;
    }
    if (op >= 0x90 && op <= 0x9f) {
      SetOperandValue(ctx, mem, d_ctx, 0, CheckCondition(ctx, op) ? 1 : 0);
      return false;
    }
    switch (op) {
      case 0xa0:
      case 0xa8: {
        const val = GetOperandValue(ctx, mem, d_ctx, 0);
        const size = d_ctx.pfx_op_size === 0x66 ? 2 : 4;
        ctx.GPR[4] = (ctx.GPR[4] - size) >>> 0;
        mem.write(ctx.GPR[4], val, size);
        break;
      }
      case 0xa1:
      case 0xa9: {
        const size = d_ctx.pfx_op_size === 0x66 ? 2 : 4;
        const val = mem.read(ctx.GPR[4], size);
        ctx.GPR[4] = (ctx.GPR[4] + size) >>> 0;
        SetOperandValue(ctx, mem, d_ctx, 0, val);
        break;
      }
      case 0xaf: { // IMUL Gv, Ev
        const size = d_ctx.pfx_op_size === 0x66 ? 16 : 32;
        const valDest = GetOperandValue(ctx, mem, d_ctx, 0);
        const valSrc = GetOperandValue(ctx, mem, d_ctx, 1);
        if (size === 16) {
          const d = (valDest << 16) >> 16;
          const s = (valSrc << 16) >> 16;
          const res = d * s;
          SetOperandValue(ctx, mem, d_ctx, 0, res & 0xffff);
          ctx.EFLAGS.CF = ctx.EFLAGS.OF = res !== ((res << 16) >> 16) ? 1 : 0;
        } else {
          const d = BigInt(valDest | 0);
          const s = BigInt(valSrc | 0);
          const res = d * s;
          const res32 = Number(res & 0xffffffffn) >>> 0;
          SetOperandValue(ctx, mem, d_ctx, 0, res32);
          ctx.EFLAGS.CF = ctx.EFLAGS.OF = res !== BigInt(res32 | 0) ? 1 : 0;
        }
        break;
      }
      case 0xb6: { // MOVZX 8
        const val = GetOperandValue(ctx, mem, d_ctx, 1) & 0xff;
        SetOperandValue(ctx, mem, d_ctx, 0, val);
        break;
      }
      case 0xb7: { // MOVZX 16
        const val = GetOperandValue(ctx, mem, d_ctx, 1) & 0xffff;
        SetOperandValue(ctx, mem, d_ctx, 0, val);
        break;
      }
      case 0xbe: { // MOVSX 8
        const val = ((GetOperandValue(ctx, mem, d_ctx, 1) & 0xff) << 24) >> 24;
        SetOperandValue(ctx, mem, d_ctx, 0, val >>> 0);
        break;
      }
      case 0xbf: { // MOVSX 16
        const val = ((GetOperandValue(ctx, mem, d_ctx, 1) & 0xffff) << 16) >> 16;
        SetOperandValue(ctx, mem, d_ctx, 0, val >>> 0);
        break;
      }
      default:
        onLog?.(`[CPU Error] Unimplemented 2-byte opcode: 0x0F ${op.toString(16).toUpperCase().padStart(2, '0')}`);
        ctx.Halted = true;
    }
    return jumped;
  }

  // Single-byte opcodes
  if (
    (op >= 0x88 && op <= 0x8c) ||
    op === 0x8e ||
    (op >= 0xb0 && op <= 0xbf) ||
    op === 0xc6 ||
    op === 0xc7
  ) {
    SetOperandValue(ctx, mem, d_ctx, 0, GetOperandValue(ctx, mem, d_ctx, 1));
    return false;
  }

  if (op === 0xa0) {
    WriteGPR(ctx, 0, 8, mem.read(d_ctx.imm >>> 0, 1));
    return false;
  }
  if (op === 0xa1) {
    const size = d_ctx.pfx_op_size === 0x66 ? 2 : 4;
    WriteGPR(ctx, 0, size * 8, mem.read(d_ctx.imm >>> 0, size));
    return false;
  }
  if (op === 0xa2) {
    mem.write(d_ctx.imm >>> 0, ReadGPR(ctx, 0, 8), 1);
    return false;
  }
  if (op === 0xa3) {
    const size = d_ctx.pfx_op_size === 0x66 ? 2 : 4;
    mem.write(d_ctx.imm >>> 0, ReadGPR(ctx, 0, size * 8), size);
    return false;
  }
  if (op === 0x8d) { // LEA
    SetOperandValue(ctx, mem, d_ctx, 0, GetEffectiveAddress(ctx, d_ctx));
    return false;
  }
  if (op === 0x90) return false; // NOP

  if (op === 0x86 || op === 0x87 || (op >= 0x91 && op <= 0x97)) { // XCHG
    const op1 = GetOperandValue(ctx, mem, d_ctx, 0);
    const op2 = GetOperandValue(ctx, mem, d_ctx, 1);
    SetOperandValue(ctx, mem, d_ctx, 1, op1);
    SetOperandValue(ctx, mem, d_ctx, 0, op2);
    return false;
  }

  if (op === 0x98 || op === 0x99) { // CBW/CWDE/CWD/CDQ
    const opSize = d_ctx.pfx_op_size === 0x66 ? 16 : 32;
    if (op === 0x98) {
      if (opSize === 16) {
        const al = (ReadGPR(ctx, 0, 8) << 24) >> 24;
        WriteGPR(ctx, 0, 16, al & 0xffff);
      } else {
        const ax = (ReadGPR(ctx, 0, 16) << 16) >> 16;
        WriteGPR(ctx, 0, 32, ax >>> 0);
      }
    } else {
      if (opSize === 16) {
        const ax = (ReadGPR(ctx, 0, 16) << 16) >> 16;
        WriteGPR(ctx, 2, 16, ax < 0 ? 0xffff : 0x0000);
      } else {
        const eax = ReadGPR(ctx, 0, 32) | 0;
        WriteGPR(ctx, 2, 32, eax < 0 ? 0xffffffff : 0x00000000);
      }
    }
    return false;
  }

  // PUSH
  if (
    (op >= 0x50 && op <= 0x57) ||
    op === 0x68 ||
    op === 0x6a ||
    op === 0x06 ||
    op === 0x0e ||
    op === 0x16 ||
    op === 0x1e
  ) {
    const val = GetOperandValue(ctx, mem, d_ctx, 0);
    const size = d_ctx.pfx_op_size === 0x66 ? 2 : 4;
    ctx.GPR[4] = (ctx.GPR[4] - size) >>> 0;
    mem.write(ctx.GPR[4], val, size);
    return false;
  }

  // POP
  if ((op >= 0x58 && op <= 0x5f) || op === 0x07 || op === 0x17 || op === 0x1f) {
    const size = d_ctx.pfx_op_size === 0x66 ? 2 : 4;
    const val = mem.read(ctx.GPR[4], size);
    ctx.GPR[4] = (ctx.GPR[4] + size) >>> 0;
    SetOperandValue(ctx, mem, d_ctx, 0, val);
    return false;
  }

  if (op === 0x60) { // PUSHA
    const size = d_ctx.pfx_op_size === 0x66 ? 2 : 4;
    const oldEsp = ctx.GPR[4] >>> 0;
    for (let i = 0; i < 8; i++) {
      ctx.GPR[4] = (ctx.GPR[4] - size) >>> 0;
      const v = i === 4 ? oldEsp : ctx.GPR[i];
      mem.write(ctx.GPR[4], v, size);
    }
    return false;
  }

  if (op === 0x61) { // POPA
    const size = d_ctx.pfx_op_size === 0x66 ? 2 : 4;
    for (let i = 7; i >= 0; i--) {
      const val = mem.read(ctx.GPR[4], size);
      ctx.GPR[4] = (ctx.GPR[4] + size) >>> 0;
      if (i !== 4) {
        WriteGPR(ctx, i, size * 8, val);
      }
    }
    return false;
  }

  if (op === 0x9c) { // PUSHF
    const eflags = PackEFLAGS(ctx);
    ctx.GPR[4] = (ctx.GPR[4] - 4) >>> 0;
    mem.write(ctx.GPR[4], eflags, 4);
    return false;
  }

  if (op === 0x9d) { // POPF
    const eflags = mem.read(ctx.GPR[4], 4);
    ctx.GPR[4] = (ctx.GPR[4] + 4) >>> 0;
    UnpackEFLAGS(ctx, eflags);
    return false;
  }

  if (op === 0xc8) { // ENTER
    const size = d_ctx.imm & 0xffff;
    const level = d_ctx.imm2 & 0x1f;
    ctx.GPR[4] = (ctx.GPR[4] - 4) >>> 0;
    mem.write(ctx.GPR[4], ctx.GPR[5], 4);
    const fp = ctx.GPR[4] >>> 0;
    if (level > 0) {
      let tempFp = ctx.GPR[5] >>> 0;
      for (let i = 1; i < level; i++) {
        tempFp = (tempFp - 4) >>> 0;
        ctx.GPR[4] = (ctx.GPR[4] - 4) >>> 0;
        mem.write(ctx.GPR[4], mem.read(tempFp, 4), 4);
      }
      ctx.GPR[4] = (ctx.GPR[4] - 4) >>> 0;
      mem.write(ctx.GPR[4], fp, 4);
    }
    ctx.GPR[5] = fp;
    ctx.GPR[4] = (ctx.GPR[4] - size) >>> 0;
    return false;
  }

  if (op === 0xc9) { // LEAVE
    ctx.GPR[4] = ctx.GPR[5] >>> 0;
    ctx.GPR[5] = mem.read(ctx.GPR[4], 4);
    ctx.GPR[4] = (ctx.GPR[4] + 4) >>> 0;
    return false;
  }

  // Standard ALU (0x00..0x3D)
  if (op <= 0x3d && (op & 7) <= 5) {
    const aluOps = [
      ALU_Op.ALU_ADD,
      ALU_Op.ALU_OR,
      ALU_Op.ALU_ADC,
      ALU_Op.ALU_SBB,
      ALU_Op.ALU_AND,
      ALU_Op.ALU_SUB,
      ALU_Op.ALU_XOR,
      ALU_Op.ALU_CMP,
    ];
    const aluOp = aluOps[(op >> 3) & 7];
    Exec_ALU_Generic(ctx, mem, d_ctx, aluOp, aluOp === ALU_Op.ALU_CMP);
    return false;
  }

  if (op === 0x80 || op === 0x81 || op === 0x82 || op === 0x83) {
    Exec_Group1(ctx, mem, d_ctx);
    return false;
  }

  if (op === 0x84 || op === 0x85 || op === 0xa8 || op === 0xa9) {
    Exec_ALU_Generic(ctx, mem, d_ctx, ALU_Op.ALU_AND, true);
    return false;
  }

  if (op >= 0x40 && op <= 0x47) { // INC r32
    const reg = op - 0x40;
    const val = ReadGPR(ctx, reg, 32);
    const res = (val + 1) >>> 0;
    UpdateEFLAGS(ctx, res, val, 1, 32, ALU_Op.ALU_INC);
    WriteGPR(ctx, reg, 32, res);
    return false;
  }

  if (op >= 0x48 && op <= 0x4f) { // DEC r32
    const reg = op - 0x48;
    const val = ReadGPR(ctx, reg, 32);
    const res = (val - 1) >>> 0;
    UpdateEFLAGS(ctx, res, val, 1, 32, ALU_Op.ALU_DEC);
    WriteGPR(ctx, reg, 32, res);
    return false;
  }

  if (op === 0xf6 || op === 0xf7) {
    Exec_Group3(ctx, mem, d_ctx, onLog);
    return false;
  }

  if (op === 0xfe) {
    if (d_ctx.reg === 0) Exec_ALU_Generic(ctx, mem, d_ctx, ALU_Op.ALU_INC, false);
    else if (d_ctx.reg === 1) Exec_ALU_Generic(ctx, mem, d_ctx, ALU_Op.ALU_DEC, false);
    return false;
  }

  if (op === 0xc0 || op === 0xc1 || (op >= 0xd0 && op <= 0xd3)) {
    Exec_Group2(ctx, mem, d_ctx);
    return false;
  }

  if (
    op === 0xa4 || op === 0xa5 ||
    op === 0xa6 || op === 0xa7 ||
    op === 0xaa || op === 0xab ||
    op === 0xac || op === 0xad ||
    op === 0xae || op === 0xaf
  ) {
    if (d_ctx.pfx_rep || d_ctx.pfx_repne) {
      return Exec_REP_StringOp(ctx, mem, d_ctx);
    }
    Exec_StringOp(ctx, mem, d_ctx);
    return false;
  }

  // Control flow
  if (op >= 0x70 && op <= 0x7f) {
    if (CheckCondition(ctx, op)) {
      ctx.EIP = (ctx.EIP + d_ctx.instr_len + d_ctx.imm) >>> 0;
      return true;
    }
    return false;
  }

  if (op === 0xe9 || op === 0xeb) {
    ctx.EIP = (ctx.EIP + d_ctx.instr_len + d_ctx.imm) >>> 0;
    return true;
  }

  if (op === 0xe3) { // JECXZ
    if ((ctx.GPR[1] >>> 0) === 0) {
      ctx.EIP = (ctx.EIP + d_ctx.instr_len + d_ctx.imm) >>> 0;
      return true;
    }
    return false;
  }

  if (op === 0xe8) { // CALL rel32
    const retAddr = (ctx.EIP + d_ctx.instr_len) >>> 0;
    ctx.GPR[4] = (ctx.GPR[4] - 4) >>> 0;
    mem.write(ctx.GPR[4], retAddr, 4);
    ctx.EIP = (ctx.EIP + d_ctx.instr_len + d_ctx.imm) >>> 0;
    return true;
  }

  if (op === 0xc3 || op === 0xc2) { // RET / RET imm16
    const retAddr = mem.read(ctx.GPR[4], 4);
    ctx.GPR[4] = (ctx.GPR[4] + 4) >>> 0;
    if (op === 0xc2) {
      ctx.GPR[4] = (ctx.GPR[4] + (d_ctx.imm & 0xffff)) >>> 0;
    }
    ctx.EIP = retAddr >>> 0;
    return true;
  }

  if (op === 0xff) { // Group 5
    const val = GetOperandValue(ctx, mem, d_ctx, 0);
    switch (d_ctx.reg) {
      case 0: Exec_ALU_Generic(ctx, mem, d_ctx, ALU_Op.ALU_INC, false); break;
      case 1: Exec_ALU_Generic(ctx, mem, d_ctx, ALU_Op.ALU_DEC, false); break;
      case 2: { // CALL Ev
        const retAddr = (ctx.EIP + d_ctx.instr_len) >>> 0;
        ctx.GPR[4] = (ctx.GPR[4] - 4) >>> 0;
        mem.write(ctx.GPR[4], retAddr, 4);
        ctx.EIP = val >>> 0;
        jumped = true;
        break;
      }
      case 4: { // JMP Ev
        ctx.EIP = val >>> 0;
        jumped = true;
        break;
      }
      case 6: { // PUSH Ev
        ctx.GPR[4] = (ctx.GPR[4] - 4) >>> 0;
        mem.write(ctx.GPR[4], val, 4);
        break;
      }
    }
    return jumped;
  }

  if (op === 0xe0 || op === 0xe1 || op === 0xe2) { // LOOPxx
    let counter: number;
    if (d_ctx.pfx_addr_size === 0x67) {
      counter = (ReadGPR(ctx, 1, 16) - 1) & 0xffff;
      WriteGPR(ctx, 1, 16, counter);
    } else {
      counter = (ctx.GPR[1] - 1) >>> 0;
      ctx.GPR[1] = counter;
    }
    const notZero = counter !== 0;
    const cond =
      op === 0xe2 ? notZero : op === 0xe1 ? notZero && ctx.EFLAGS.ZF === 1 : notZero && ctx.EFLAGS.ZF === 0;
    if (cond) {
      ctx.EIP = (ctx.EIP + d_ctx.instr_len + d_ctx.imm) >>> 0;
      return true;
    }
    return false;
  }

  if (op === 0xcc || op === 0xcd) { // INT 3 / INT Ib
    const intNum = op === 0xcc ? 3 : d_ctx.imm & 0xff;
    if (intNum === 0x21) {
      const ah = ReadGPR(ctx, 4, 8);
      if (ah === 0x02) {
        const ch = String.fromCharCode(ReadGPR(ctx, 2, 8));
        onLog?.(`[DOS INT 21h/02h] ${ch}`);
      } else if (ah === 0x09) {
        let addr = ctx.GPR[2] >>> 0;
        let out = '';
        let guard = 0;
        while (guard++ < 1024) {
          const b = mem.readU8(addr++);
          if (b === 0x24 || b === 0) break; // '$' or NUL
          out += String.fromCharCode(b);
        }
        onLog?.(`[DOS INT 21h/09h] ${out}`);
      } else if (ah === 0x4c) {
        const exitCode = ReadGPR(ctx, 0, 8);
        onLog?.(`[Program exited with code ${exitCode}]`);
        ctx.Halted = true;
      } else {
        onLog?.(`[TinyX86] Unhandled INT 21h service: AH=0x${ah.toString(16).toUpperCase().padStart(2, '0')}`);
      }
    } else if (intNum === 3) {
      onLog?.(`[TinyX86] Breakpoint Hit (INT 3) at EIP=0x${ctx.EIP.toString(16).toUpperCase().padStart(8, '0')}`);
      ctx.Halted = true;
    } else {
      onLog?.(`[TinyX86] Unhandled Interrupt: 0x${intNum.toString(16).toUpperCase().padStart(2, '0')}`);
    }
    return false;
  }

  if (op >= 0xd8 && op <= 0xdf) {
    Exec_FPU(ctx, mem, d_ctx);
    return false;
  }

  if (op === 0x9e) { // SAHF
    const ah = ReadGPR(ctx, 4, 8);
    ctx.EFLAGS.SF = (ah >>> 7) & 1;
    ctx.EFLAGS.ZF = (ah >>> 6) & 1;
    ctx.EFLAGS.AF = (ah >>> 4) & 1;
    ctx.EFLAGS.PF = (ah >>> 2) & 1;
    ctx.EFLAGS.CF = ah & 1;
    return false;
  }

  if (op === 0x9f) { // LAHF
    let ah = 0x02;
    ah |= (ctx.EFLAGS.SF & 1) << 7;
    ah |= (ctx.EFLAGS.ZF & 1) << 6;
    ah |= (ctx.EFLAGS.AF & 1) << 4;
    ah |= (ctx.EFLAGS.PF & 1) << 2;
    ah |= ctx.EFLAGS.CF & 1;
    WriteGPR(ctx, 4, 8, ah);
    return false;
  }

  if (op === 0xf5) { ctx.EFLAGS.CF ^= 1; return false; }
  if (op === 0xf8) { ctx.EFLAGS.CF = 0; return false; }
  if (op === 0xf9) { ctx.EFLAGS.CF = 1; return false; }
  if (op === 0xfa) { ctx.EFLAGS.IF = 0; return false; }
  if (op === 0xfb) { ctx.EFLAGS.IF = 1; return false; }
  if (op === 0xfc) { ctx.EFLAGS.DF = 0; return false; }
  if (op === 0xfd) { ctx.EFLAGS.DF = 1; return false; }
  if (op === 0xf4) { ctx.Halted = true; return false; }

  onLog?.(`[CPU] Unimplemented Opcode: 0x${op.toString(16).toUpperCase().padStart(2, '0')} at EIP=0x${ctx.EIP.toString(16).toUpperCase().padStart(8, '0')}`);
  ctx.Halted = true;
  return false;
}

export function runcpu(
  ctx: CPUContext,
  mem: VirtualMemory,
  step: number,
  onLog?: (msg: string) => void
): number {
  for (let i = 0; i < step; i++) {
    if (ctx.Halted) {
      onLog?.('[System] CPU Halted.');
      break;
    }
    const d_ctx = ParseInstruction((addr) => mem.readU8(addr), ctx.EIP);
    if (d_ctx.instr_len <= 0) {
      onLog?.(`Error: Decoding failed at EIP: 0x${ctx.EIP.toString(16).toUpperCase().padStart(8, '0')}`);
      return -1;
    }
    const jumped = ExecuteInstruction(ctx, mem, d_ctx, onLog);
    if (!jumped) {
      ctx.EIP = (ctx.EIP + d_ctx.instr_len) >>> 0;
    }
  }
  return 0;
}

export function SetCpuState(ctx: CPUContext, nameIn: string, val: number): boolean {
  const name = nameIn.trim().toUpperCase();
  const uVal = val >>> 0;
  switch (name) {
    case 'EAX': ctx.GPR[0] = uVal; return true;
    case 'ECX': ctx.GPR[1] = uVal; return true;
    case 'EDX': ctx.GPR[2] = uVal; return true;
    case 'EBX': ctx.GPR[3] = uVal; return true;
    case 'ESP': ctx.GPR[4] = uVal; return true;
    case 'EBP': ctx.GPR[5] = uVal; return true;
    case 'ESI': ctx.GPR[6] = uVal; return true;
    case 'EDI': ctx.GPR[7] = uVal; return true;
    case 'EIP': ctx.EIP = uVal; return true;
    case 'AX': WriteGPR(ctx, 0, 16, uVal); return true;
    case 'CX': WriteGPR(ctx, 1, 16, uVal); return true;
    case 'DX': WriteGPR(ctx, 2, 16, uVal); return true;
    case 'BX': WriteGPR(ctx, 3, 16, uVal); return true;
    case 'SP': WriteGPR(ctx, 4, 16, uVal); return true;
    case 'BP': WriteGPR(ctx, 5, 16, uVal); return true;
    case 'SI': WriteGPR(ctx, 6, 16, uVal); return true;
    case 'DI': WriteGPR(ctx, 7, 16, uVal); return true;
    case 'AL': WriteGPR(ctx, 0, 8, uVal); return true;
    case 'CL': WriteGPR(ctx, 1, 8, uVal); return true;
    case 'DL': WriteGPR(ctx, 2, 8, uVal); return true;
    case 'BL': WriteGPR(ctx, 3, 8, uVal); return true;
    case 'AH': WriteGPR(ctx, 4, 8, uVal); return true;
    case 'CH': WriteGPR(ctx, 5, 8, uVal); return true;
    case 'DH': WriteGPR(ctx, 6, 8, uVal); return true;
    case 'BH': WriteGPR(ctx, 7, 8, uVal); return true;
    case 'ES': ctx.seg[0] = uVal & 0xffff; return true;
    case 'CS': ctx.seg[1] = uVal & 0xffff; return true;
    case 'SS': ctx.seg[2] = uVal & 0xffff; return true;
    case 'DS': ctx.seg[3] = uVal & 0xffff; return true;
    case 'FS': ctx.seg[4] = uVal & 0xffff; return true;
    case 'GS': ctx.seg[5] = uVal & 0xffff; return true;
    case 'CF': ctx.EFLAGS.CF = uVal ? 1 : 0; return true;
    case 'PF': ctx.EFLAGS.PF = uVal ? 1 : 0; return true;
    case 'AF': ctx.EFLAGS.AF = uVal ? 1 : 0; return true;
    case 'ZF': ctx.EFLAGS.ZF = uVal ? 1 : 0; return true;
    case 'SF': ctx.EFLAGS.SF = uVal ? 1 : 0; return true;
    case 'TF': ctx.EFLAGS.TF = uVal ? 1 : 0; return true;
    case 'IF': ctx.EFLAGS.IF = uVal ? 1 : 0; return true;
    case 'DF': ctx.EFLAGS.DF = uVal ? 1 : 0; return true;
    case 'OF': ctx.EFLAGS.OF = uVal ? 1 : 0; return true;
    case 'IOPL': ctx.EFLAGS.IOPL = uVal & 3; return true;
    case 'NT': ctx.EFLAGS.NT = uVal ? 1 : 0; return true;
    case 'TOP': ctx.FPU.SW.TOP = uVal & 7; return true;
    default: return false;
  }
}

export function IsStepOverCandidate(d_ctx: DecodeContext): boolean {
  if (d_ctx.is_two_byte_opcode) return false;
  const op = d_ctx.opcode;
  if (op === 0xe8 || op === 0x9a) return true;
  if (op === 0xff && d_ctx.reg === 2) return true;
  if (op >= 0xe0 && op <= 0xe2) return true;
  if (d_ctx.pfx_rep || d_ctx.pfx_repne) return true;
  if (op === 0xcd) return true;
  return false;
}
