import { DecodeContext, OpcodeEntry, OperandType } from './types';

const GPR8_NAMES = ['al', 'cl', 'dl', 'bl', 'ah', 'ch', 'dh', 'bh'];
const GPR16_NAMES = ['ax', 'cx', 'dx', 'bx', 'sp', 'bp', 'si', 'di'];
const GPR32_NAMES = ['eax', 'ecx', 'edx', 'ebx', 'esp', 'ebp', 'esi', 'edi'];
const SEG_NAMES = ['es', 'cs', 'ss', 'ds', 'fs', 'gs'];
const CC_NAMES = [
  'o', 'no', 'b', 'nb', 'z', 'nz', 'be', 'a',
  's', 'ns', 'p', 'np', 'l', 'ge', 'le', 'g'
];
const GRP1_NAMES = ['add', 'or', 'adc', 'sbb', 'and', 'sub', 'xor', 'cmp'];
const GRP2_NAMES = ['rol', 'ror', 'rcl', 'rcr', 'shl', 'shr', 'sal', 'sar'];
const GRP3_NAMES = ['test', 'test', 'not', 'neg', 'mul', 'imul', 'div', 'idiv'];

function entry(
  mnemonic: string,
  op1: OperandType = OperandType.NONE,
  op2: OperandType = OperandType.NONE,
  op3: OperandType = OperandType.NONE,
  hasModRM = false
): OpcodeEntry {
  return { mnemonic, op1, op2, op3, hasModRM };
}

function getTwoByteEntry(op: number, reg: number): OpcodeEntry {
  if (op >= 0x40 && op <= 0x4f) {
    return entry(`cmov${CC_NAMES[op & 0x0f]}`, OperandType.Gv, OperandType.Ev, OperandType.NONE, true);
  }
  if (op >= 0x80 && op <= 0x8f) {
    return entry(`j${CC_NAMES[op & 0x0f]}`, OperandType.Jz);
  }
  if (op >= 0x90 && op <= 0x9f) {
    return entry(`set${CC_NAMES[op & 0x0f]}`, OperandType.Eb, OperandType.NONE, OperandType.NONE, true);
  }
  switch (op) {
    case 0xa0: return entry('push', OperandType.OP_FS);
    case 0xa1: return entry('pop', OperandType.OP_FS);
    case 0xa8: return entry('push', OperandType.OP_GS);
    case 0xa9: return entry('pop', OperandType.OP_GS);
    case 0xaf: return entry('imul', OperandType.Gv, OperandType.Ev, OperandType.NONE, true);
    case 0xb6: return entry('movzx', OperandType.Gv, OperandType.Eb, OperandType.NONE, true);
    case 0xb7: return entry('movzx', OperandType.Gv, OperandType.Ew, OperandType.NONE, true);
    case 0xbe: return entry('movsx', OperandType.Gv, OperandType.Eb, OperandType.NONE, true);
    case 0xbf: return entry('movsx', OperandType.Gv, OperandType.Ew, OperandType.NONE, true);
    default: return entry(`db 0fh, ${op.toString(16).padStart(2, '0')}h`);
  }
}

function getOneByteEntry(op: number, reg: number, mod: number, rm: number, pfxOpSize: number): OpcodeEntry {
  // Standard ALU blocks: 0x00..0x3D
  if (op <= 0x3d && (op & 7) <= 5) {
    const aluIdx = (op >> 3) & 7;
    const mnem = GRP1_NAMES[aluIdx];
    switch (op & 7) {
      case 0: return entry(mnem, OperandType.Eb, OperandType.Gb, OperandType.NONE, true);
      case 1: return entry(mnem, OperandType.Ev, OperandType.Gv, OperandType.NONE, true);
      case 2: return entry(mnem, OperandType.Gb, OperandType.Eb, OperandType.NONE, true);
      case 3: return entry(mnem, OperandType.Gv, OperandType.Ev, OperandType.NONE, true);
      case 4: return entry(mnem, OperandType.OP_AL, OperandType.Ib);
      case 5: return entry(mnem, OperandType.OP_rAX, OperandType.Iz);
    }
  }

  switch (op) {
    case 0x06: return entry('push', OperandType.OP_ES);
    case 0x07: return entry('pop', OperandType.OP_ES);
    case 0x0e: return entry('push', OperandType.OP_CS);
    case 0x16: return entry('push', OperandType.OP_SS);
    case 0x17: return entry('pop', OperandType.OP_SS);
    case 0x1e: return entry('push', OperandType.OP_DS);
    case 0x1f: return entry('pop', OperandType.OP_DS);
  }

  if (op >= 0x40 && op <= 0x47) return entry('inc', OperandType.OP_rAX + (op - 0x40));
  if (op >= 0x48 && op <= 0x4f) return entry('dec', OperandType.OP_rAX + (op - 0x48));
  if (op >= 0x50 && op <= 0x57) return entry('push', OperandType.OP_rAX + (op - 0x50));
  if (op >= 0x58 && op <= 0x5f) return entry('pop', OperandType.OP_rAX + (op - 0x58));

  if (op === 0x60) return entry(pfxOpSize === 0x66 ? 'pusha' : 'pushad');
  if (op === 0x61) return entry(pfxOpSize === 0x66 ? 'popa' : 'popad');
  if (op === 0x68) return entry('push', OperandType.Iz);
  if (op === 0x6a) return entry('push', OperandType.Ib);

  if (op >= 0x70 && op <= 0x7f) {
    return entry(`j${CC_NAMES[op & 0x0f]}`, OperandType.Jb);
  }

  // Group 1 (80..83)
  if (op === 0x80) return entry(GRP1_NAMES[reg], OperandType.Eb, OperandType.Ib, OperandType.NONE, true);
  if (op === 0x81) return entry(GRP1_NAMES[reg], OperandType.Ev, OperandType.Iz, OperandType.NONE, true);
  if (op === 0x82) return entry(GRP1_NAMES[reg], OperandType.Eb, OperandType.Ib, OperandType.NONE, true);
  if (op === 0x83) return entry(GRP1_NAMES[reg], OperandType.Ev, OperandType.Ib, OperandType.NONE, true);

  if (op === 0x84) return entry('test', OperandType.Eb, OperandType.Gb, OperandType.NONE, true);
  if (op === 0x85) return entry('test', OperandType.Ev, OperandType.Gv, OperandType.NONE, true);
  if (op === 0x86) return entry('xchg', OperandType.Eb, OperandType.Gb, OperandType.NONE, true);
  if (op === 0x87) return entry('xchg', OperandType.Ev, OperandType.Gv, OperandType.NONE, true);
  if (op === 0x88) return entry('mov', OperandType.Eb, OperandType.Gb, OperandType.NONE, true);
  if (op === 0x89) return entry('mov', OperandType.Ev, OperandType.Gv, OperandType.NONE, true);
  if (op === 0x8a) return entry('mov', OperandType.Gb, OperandType.Eb, OperandType.NONE, true);
  if (op === 0x8b) return entry('mov', OperandType.Gv, OperandType.Ev, OperandType.NONE, true);
  if (op === 0x8c) return entry('mov', OperandType.Ew, OperandType.Sw, OperandType.NONE, true);
  if (op === 0x8d) return entry('lea', OperandType.Gv, OperandType.M, OperandType.NONE, true);
  if (op === 0x8e) return entry('mov', OperandType.Sw, OperandType.Ew, OperandType.NONE, true);

  if (op === 0x90) return entry('nop');
  if (op >= 0x91 && op <= 0x97) return entry('xchg', OperandType.OP_rAX, OperandType.OP_rAX + (op - 0x90));
  if (op === 0x98) return entry(pfxOpSize === 0x66 ? 'cbw' : 'cwde');
  if (op === 0x99) return entry(pfxOpSize === 0x66 ? 'cwd' : 'cdq');
  if (op === 0x9c) return entry('pushf');
  if (op === 0x9d) return entry('popf');
  if (op === 0x9e) return entry('sahf');
  if (op === 0x9f) return entry('lahf');

  if (op === 0xa0) return entry('mov', OperandType.OP_AL, OperandType.Ob);
  if (op === 0xa1) return entry('mov', OperandType.OP_rAX, OperandType.Ov);
  if (op === 0xa2) return entry('mov', OperandType.Ob, OperandType.OP_AL);
  if (op === 0xa3) return entry('mov', OperandType.Ov, OperandType.OP_rAX);
  if (op === 0xa4) return entry('movsb');
  if (op === 0xa5) return entry(pfxOpSize === 0x66 ? 'movsw' : 'movsd');
  if (op === 0xa6) return entry('cmpsb');
  if (op === 0xa7) return entry(pfxOpSize === 0x66 ? 'cmpsw' : 'cmpsd');
  if (op === 0xa8) return entry('test', OperandType.OP_AL, OperandType.Ib);
  if (op === 0xa9) return entry('test', OperandType.OP_rAX, OperandType.Iz);
  if (op === 0xaa) return entry('stosb');
  if (op === 0xab) return entry(pfxOpSize === 0x66 ? 'stosw' : 'stosd');
  if (op === 0xac) return entry('lodsb');
  if (op === 0xad) return entry(pfxOpSize === 0x66 ? 'lodsw' : 'lodsd');
  if (op === 0xae) return entry('scasb');
  if (op === 0xaf) return entry(pfxOpSize === 0x66 ? 'scasw' : 'scasd');

  if (op >= 0xb0 && op <= 0xb7) return entry('mov', OperandType.OP_AL + (op - 0xb0), OperandType.Ib);
  if (op >= 0xb8 && op <= 0xbf) return entry('mov', OperandType.OP_rAX + (op - 0xb8), OperandType.Iv);

  // Group 2 (C0, C1, D0..D3)
  if (op === 0xc0) return entry(GRP2_NAMES[reg], OperandType.Eb, OperandType.Ib, OperandType.NONE, true);
  if (op === 0xc1) return entry(GRP2_NAMES[reg], OperandType.Ev, OperandType.Ib, OperandType.NONE, true);
  if (op === 0xc2) return entry('ret', OperandType.Iw);
  if (op === 0xc3) return entry('ret');
  if (op === 0xc6) return entry('mov', OperandType.Eb, OperandType.Ib, OperandType.NONE, true);
  if (op === 0xc7) return entry('mov', OperandType.Ev, OperandType.Iz, OperandType.NONE, true);
  if (op === 0xc8) return entry('enter', OperandType.Iw, OperandType.Ib);
  if (op === 0xc9) return entry('leave');
  if (op === 0xcc) return entry('int3');
  if (op === 0xcd) return entry('int', OperandType.Ib);

  if (op === 0xd0) return entry(GRP2_NAMES[reg], OperandType.Eb, OperandType.OP_1, OperandType.NONE, true);
  if (op === 0xd1) return entry(GRP2_NAMES[reg], OperandType.Ev, OperandType.OP_1, OperandType.NONE, true);
  if (op === 0xd2) return entry(GRP2_NAMES[reg], OperandType.Eb, OperandType.OP_CL, OperandType.NONE, true);
  if (op === 0xd3) return entry(GRP2_NAMES[reg], OperandType.Ev, OperandType.OP_CL, OperandType.NONE, true);

  // FPU (D8..DF)
  if (op >= 0xd8 && op <= 0xdf) {
    return getFpuEntry(op, mod, reg, rm);
  }

  if (op === 0xe0) return entry('loopne', OperandType.Jb);
  if (op === 0xe1) return entry('loope', OperandType.Jb);
  if (op === 0xe2) return entry('loop', OperandType.Jb);
  if (op === 0xe3) return entry('jecxz', OperandType.Jb);
  if (op === 0xe8) return entry('call', OperandType.Jz);
  if (op === 0xe9) return entry('jmp', OperandType.Jz);
  if (op === 0xeb) return entry('jmp', OperandType.Jb);

  if (op === 0xf4) return entry('hlt');
  if (op === 0xf5) return entry('cmc');

  // Group 3 (F6, F7)
  if (op === 0xf6) {
    if (reg === 0 || reg === 1) return entry('test', OperandType.Eb, OperandType.Ib, OperandType.NONE, true);
    if (reg === 2 || reg === 3) return entry(GRP3_NAMES[reg], OperandType.Eb, OperandType.NONE, OperandType.NONE, true);
    return entry(GRP3_NAMES[reg], OperandType.OP_AL, OperandType.Eb, OperandType.NONE, true);
  }
  if (op === 0xf7) {
    if (reg === 0 || reg === 1) return entry('test', OperandType.Ev, OperandType.Iz, OperandType.NONE, true);
    if (reg === 2 || reg === 3) return entry(GRP3_NAMES[reg], OperandType.Ev, OperandType.NONE, OperandType.NONE, true);
    return entry(GRP3_NAMES[reg], OperandType.OP_rAX, OperandType.Ev, OperandType.NONE, true);
  }

  if (op === 0xf8) return entry('clc');
  if (op === 0xf9) return entry('stc');
  if (op === 0xfa) return entry('cli');
  if (op === 0xfb) return entry('sti');
  if (op === 0xfc) return entry('cld');
  if (op === 0xfd) return entry('std');

  if (op === 0xfe) {
    const m = reg === 0 ? 'inc' : reg === 1 ? 'dec' : 'grp4';
    return entry(m, OperandType.Eb, OperandType.NONE, OperandType.NONE, true);
  }
  if (op === 0xff) {
    const grp5 = ['inc', 'dec', 'call', 'call far', 'jmp', 'jmp far', 'push', 'grp5'];
    return entry(grp5[reg], OperandType.Ev, OperandType.NONE, OperandType.NONE, true);
  }

  return entry(`db ${op.toString(16).padStart(2, '0')}h`);
}

function getFpuEntry(op: number, mod: number, reg: number, rm: number): OpcodeEntry {
  const fpuArith = ['fadd', 'fmul', 'fcom', 'fcomp', 'fsub', 'fsubr', 'fdiv', 'fdivr'];
  const fpuIntArith = ['fiadd', 'fimul', 'ficom', 'ficomp', 'fisub', 'fisubr', 'fidiv', 'fidivr'];
  if (mod !== 3) {
    if (op === 0xd8 || op === 0xdc) return entry(fpuArith[reg], OperandType.M, OperandType.NONE, OperandType.NONE, true);
    if (op === 0xda || op === 0xde) return entry(fpuIntArith[reg], OperandType.M, OperandType.NONE, OperandType.NONE, true);
    if (op === 0xd9) {
      const d9m = ['fld', 'fpu', 'fst', 'fstp', 'fldenv', 'fldcw', 'fstenv', 'fnstcw'];
      return entry(d9m[reg], OperandType.M, OperandType.NONE, OperandType.NONE, true);
    }
    if (op === 0xdd) {
      const ddm = ['fld', 'fisttp', 'fst', 'fstp', 'frstor', 'fpu', 'fsave', 'fstsw'];
      return entry(ddm[reg], OperandType.M, OperandType.NONE, OperandType.NONE, true);
    }
    if (op === 0xdb) {
      const dbm = ['fild', 'fisttp', 'fist', 'fistp', 'fpu', 'fld', 'fpu', 'fstp'];
      return entry(dbm[reg], OperandType.M, OperandType.NONE, OperandType.NONE, true);
    }
    if (op === 0xdf) {
      const dfm = ['fild', 'fisttp', 'fist', 'fistp', 'fbld', 'fild', 'fbstp', 'fistp'];
      return entry(dfm[reg], OperandType.M, OperandType.NONE, OperandType.NONE, true);
    }
  } else {
    if (op === 0xd8) return entry(`${fpuArith[reg]} st(0), st(${rm})`, OperandType.NONE, OperandType.NONE, OperandType.NONE, true);
    if (op === 0xdc) return entry(`${fpuArith[reg]} st(${rm}), st(0)`, OperandType.NONE, OperandType.NONE, OperandType.NONE, true);
    if (op === 0xde) {
      if (reg === 3 && rm === 1) return entry('fcompp', OperandType.NONE, OperandType.NONE, OperandType.NONE, true);
      const deNames = ['faddp', 'fmulp', 'fcomp', 'fcompp', 'fsubrp', 'fsubp', 'fdivrp', 'fdivp'];
      return entry(`${deNames[reg]} st(${rm}), st(0)`, OperandType.NONE, OperandType.NONE, OperandType.NONE, true);
    }
    if (op === 0xda && reg === 5 && rm === 1) {
      return entry('fucompp', OperandType.NONE, OperandType.NONE, OperandType.NONE, true);
    }
    if (op === 0xdb && reg === 4) {
      if (rm === 2) return entry('fclex', OperandType.NONE, OperandType.NONE, OperandType.NONE, true);
      if (rm === 3) return entry('finit', OperandType.NONE, OperandType.NONE, OperandType.NONE, true);
    }
    if (op === 0xdf && reg === 4 && rm === 0) {
      return entry('fnstsw ax', OperandType.NONE, OperandType.NONE, OperandType.NONE, true);
    }
    if (op === 0xd9) {
      if (reg === 4 && rm === 0) return entry('fchs', OperandType.NONE, OperandType.NONE, OperandType.NONE, true);
      if (reg === 4 && rm === 1) return entry('fabs', OperandType.NONE, OperandType.NONE, OperandType.NONE, true);
      if (reg === 5) {
        const consts = ['fld1', 'fldl2t', 'fldl2e', 'fldpi', 'fldlg2', 'fldln2', 'fldz', 'fpu'];
        return entry(consts[rm], OperandType.NONE, OperandType.NONE, OperandType.NONE, true);
      }
    }
  }
  return entry('fpu', OperandType.NONE, OperandType.NONE, OperandType.NONE, true);
}

export function ParseInstruction(
  readByte: (addr: number) => number,
  eip: number
): DecodeContext {
  const ctx: DecodeContext = {
    pfx_rep: 0,
    pfx_repne: 0,
    pfx_op_size: 0,
    pfx_addr_size: 0,
    pfx_seg: 0,
    is_two_byte_opcode: false,
    opcode: 0,
    has_modrm: false,
    modrm: 0,
    mod: 0,
    reg: 0,
    rm: 0,
    has_sib: false,
    sib: 0,
    scale: 0,
    index: 0,
    base: 0,
    disp: 0,
    disp_len: 0,
    imm: 0,
    imm2: 0,
    instr_len: 0,
    entry: entry('nop'),
    asm_str: '',
    bytes: [],
    address: eip >>> 0,
  };

  let cur = eip >>> 0;
  const fetchU8 = () => {
    const b = readByte(cur >>> 0) & 0xff;
    ctx.bytes.push(b);
    cur = (cur + 1) >>> 0;
    return b;
  };
  const fetchI8 = () => {
    const b = fetchU8();
    return (b << 24) >> 24;
  };
  const fetchU16 = () => {
    const b0 = fetchU8();
    const b1 = fetchU8();
    return b0 | (b1 << 8);
  };
  const fetchI16 = () => {
    const u = fetchU16();
    return (u << 16) >> 16;
  };
  const fetchI32 = () => {
    const b0 = fetchU8();
    const b1 = fetchU8();
    const b2 = fetchU8();
    const b3 = fetchU8();
    return (b0 | (b1 << 8) | (b2 << 16) | (b3 << 24)) | 0;
  };

  // 1. Parse Prefixes
  while (ctx.bytes.length < 15) {
    const b = readByte(cur >>> 0) & 0xff;
    if (b === 0xf3) {
      ctx.pfx_rep = b;
      fetchU8();
    } else if (b === 0xf2) {
      ctx.pfx_repne = b;
      fetchU8();
    } else if (b === 0x66) {
      ctx.pfx_op_size = b;
      fetchU8();
    } else if (b === 0x67) {
      ctx.pfx_addr_size = b;
      fetchU8();
    } else if (b === 0x26 || b === 0x2e || b === 0x36 || b === 0x3e || b === 0x64 || b === 0x65) {
      ctx.pfx_seg = b;
      fetchU8();
    } else if (b === 0xf0) {
      fetchU8(); // LOCK
    } else {
      break;
    }
  }

  // 2. Opcode
  let op = fetchU8();
  if (op === 0x0f) {
    ctx.is_two_byte_opcode = true;
    op = fetchU8();
  }
  ctx.opcode = op;

  // Peek ModRM if needed to determine entry
  const peekModRM = readByte(cur >>> 0) & 0xff;
  const peekMod = (peekModRM >> 6) & 3;
  const peekReg = (peekModRM >> 3) & 7;
  const peekRm = peekModRM & 7;

  ctx.entry = ctx.is_two_byte_opcode
    ? getTwoByteEntry(op, peekReg)
    : getOneByteEntry(op, peekReg, peekMod, peekRm, ctx.pfx_op_size);

  // 3. ModR/M & SIB & Displacement
  if (ctx.entry.hasModRM) {
    ctx.has_modrm = true;
    ctx.modrm = fetchU8();
    ctx.mod = (ctx.modrm >> 6) & 3;
    ctx.reg = (ctx.modrm >> 3) & 7;
    ctx.rm = ctx.modrm & 7;

    if (ctx.mod !== 3) {
      if (ctx.rm === 4) {
        ctx.has_sib = true;
        ctx.sib = fetchU8();
        ctx.scale = (ctx.sib >> 6) & 3;
        ctx.index = (ctx.sib >> 3) & 7;
        ctx.base = ctx.sib & 7;
      }

      if (ctx.mod === 0) {
        if (ctx.rm === 5 || (ctx.has_sib && ctx.base === 5)) {
          ctx.disp = fetchI32();
          ctx.disp_len = 4;
        }
      } else if (ctx.mod === 1) {
        ctx.disp = fetchI8();
        ctx.disp_len = 1;
      } else if (ctx.mod === 2) {
        ctx.disp = fetchI32();
        ctx.disp_len = 4;
      }
    }
  }

  // 4. Immediates / Offsets
  const ops = [ctx.entry.op1, ctx.entry.op2, ctx.entry.op3];
  const opSize16 = ctx.pfx_op_size === 0x66;
  let immCount = 0;

  for (const opType of ops) {
    if (opType === OperandType.NONE) continue;
    let val: number | null = null;
    switch (opType) {
      case OperandType.Ib:
      case OperandType.Jb:
        val = fetchI8();
        if (!ctx.is_two_byte_opcode && (ctx.opcode >= 0xe0 && ctx.opcode <= 0xe2)) {
          // Note: TinyX86.c line 1807 uses d_ctx->disp_len + (int8_t)d_ctx->imm for LOOP
          ctx.disp_len = (cur - (eip >>> 0)) | 0;
        }
        break;
      case OperandType.Iw:
        val = fetchU16();
        break;
      case OperandType.Iv:
      case OperandType.Iz:
      case OperandType.Jz:
        val = opSize16 ? fetchI16() : fetchI32();
        break;
      case OperandType.Ob:
      case OperandType.Ov:
        val = fetchI32() >>> 0;
        break;
    }
    if (val !== null) {
      if (immCount === 0) ctx.imm = val;
      else ctx.imm2 = val;
      immCount++;
    }
  }

  ctx.instr_len = (cur - (eip >>> 0)) | 0;
  ctx.asm_str = FormatInstruction(ctx);
  return ctx;
}

function formatMemAddress(ctx: DecodeContext, ptrPrefix: string): string {
  const parts: string[] = [];
  if (ctx.has_sib) {
    if (!(ctx.mod === 0 && ctx.base === 5)) {
      parts.push(GPR32_NAMES[ctx.base]);
    }
    if (ctx.index !== 4) {
      const scale = 1 << ctx.scale;
      parts.push(scale > 1 ? `${GPR32_NAMES[ctx.index]}*${scale}` : GPR32_NAMES[ctx.index]);
    }
  } else {
    if (!(ctx.mod === 0 && ctx.rm === 5)) {
      parts.push(GPR32_NAMES[ctx.rm]);
    }
  }

  let addrStr = parts.join('+');
  if (ctx.disp_len > 0 || parts.length === 0) {
    if (parts.length === 0) {
      addrStr = `0x${(ctx.disp >>> 0).toString(16).toUpperCase()}`;
    } else if (ctx.disp < 0) {
      addrStr += `-0x${Math.abs(ctx.disp).toString(16).toUpperCase()}`;
    } else if (ctx.disp > 0) {
      addrStr += `+0x${ctx.disp.toString(16).toUpperCase()}`;
    }
  }
  return ptrPrefix ? `${ptrPrefix} [${addrStr}]` : `[${addrStr}]`;
}

function formatOperand(ctx: DecodeContext, type: OperandType, opIdx: number): string {
  const is16 = ctx.pfx_op_size === 0x66;
  const regNames = is16 ? GPR16_NAMES : GPR32_NAMES;

  switch (type) {
    case OperandType.Gb: return GPR8_NAMES[ctx.reg];
    case OperandType.Gw: return GPR16_NAMES[ctx.reg];
    case OperandType.Gv:
    case OperandType.Gz: return regNames[ctx.reg];
    case OperandType.Sw: return SEG_NAMES[ctx.reg] ?? 'seg?';
    case OperandType.Eb:
      return ctx.mod === 3 ? GPR8_NAMES[ctx.rm] : formatMemAddress(ctx, 'byte ptr');
    case OperandType.Ew:
      return ctx.mod === 3 ? GPR16_NAMES[ctx.rm] : formatMemAddress(ctx, 'word ptr');
    case OperandType.Ev:
      return ctx.mod === 3 ? regNames[ctx.rm] : formatMemAddress(ctx, is16 ? 'word ptr' : 'dword ptr');
    case OperandType.M: {
      let ptr = '';
      if (ctx.opcode === 0xd8 || ctx.opcode === 0xd9 || ctx.opcode === 0xda || ctx.opcode === 0xdb) ptr = 'dword ptr';
      else if (ctx.opcode === 0xdc || ctx.opcode === 0xdd) ptr = 'qword ptr';
      else if (ctx.opcode === 0xde || ctx.opcode === 0xdf) ptr = 'word ptr';
      return formatMemAddress(ctx, ptr);
    }
    case OperandType.OP_AL:
    case OperandType.OP_CL:
    case OperandType.OP_DL:
    case OperandType.OP_BL:
    case OperandType.OP_AH:
    case OperandType.OP_CH:
    case OperandType.OP_DH:
    case OperandType.OP_BH:
      return GPR8_NAMES[type - OperandType.OP_AL];
    case OperandType.OP_DX:
      return 'dx';
    case OperandType.OP_rAX:
    case OperandType.OP_rCX:
    case OperandType.OP_rDX:
    case OperandType.OP_rBX:
    case OperandType.OP_rSP:
    case OperandType.OP_rBP:
    case OperandType.OP_rSI:
    case OperandType.OP_rDI:
      return regNames[type - OperandType.OP_rAX];
    case OperandType.OP_ES: return 'es';
    case OperandType.OP_CS: return 'cs';
    case OperandType.OP_SS: return 'ss';
    case OperandType.OP_DS: return 'ds';
    case OperandType.OP_FS: return 'fs';
    case OperandType.OP_GS: return 'gs';
    case OperandType.OP_1: return '1';
    case OperandType.Ib: {
      const val = opIdx >= 1 && ctx.entry.op1 === OperandType.Iw ? ctx.imm2 : ctx.imm;
      return `0x${(val & 0xff).toString(16).toUpperCase()}`;
    }
    case OperandType.Iw:
      return `0x${(ctx.imm & 0xffff).toString(16).toUpperCase()}`;
    case OperandType.Iv:
    case OperandType.Iz:
      return `0x${(ctx.imm >>> 0).toString(16).toUpperCase()}`;
    case OperandType.Jb:
    case OperandType.Jz: {
      const target = (ctx.address + ctx.instr_len + ctx.imm) >>> 0;
      return `0x${target.toString(16).toUpperCase().padStart(8, '0')}`;
    }
    case OperandType.Ob:
      return `byte ptr [0x${(ctx.imm >>> 0).toString(16).toUpperCase()}]`;
    case OperandType.Ov:
      return `${is16 ? 'word' : 'dword'} ptr [0x${(ctx.imm >>> 0).toString(16).toUpperCase()}]`;
    default:
      return '';
  }
}

export function FormatInstruction(ctx: DecodeContext): string {
  let prefix = '';
  if (ctx.pfx_rep) prefix = 'rep ';
  else if (ctx.pfx_repne) prefix = 'repne ';

  const ops: string[] = [];
  if (ctx.entry.op1 !== OperandType.NONE) ops.push(formatOperand(ctx, ctx.entry.op1, 0));
  if (ctx.entry.op2 !== OperandType.NONE) ops.push(formatOperand(ctx, ctx.entry.op2, 1));
  if (ctx.entry.op3 !== OperandType.NONE) ops.push(formatOperand(ctx, ctx.entry.op3, 2));

  return ops.length > 0
    ? `${prefix}${ctx.entry.mnemonic} ${ops.join(', ')}`
    : `${prefix}${ctx.entry.mnemonic}`;
}
