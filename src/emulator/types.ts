export enum OperandType {
  NONE = 0,
  Eb,
  Ew,
  Ev,
  Gb,
  Gw,
  Gv,
  Gz,
  Sw,
  Ib,
  Iw,
  Iv,
  Iz,
  Jb,
  Jz,
  Ob,
  Ov,
  M,
  OP_AL,
  OP_CL,
  OP_DL,
  OP_BL,
  OP_AH,
  OP_CH,
  OP_DH,
  OP_BH,
  OP_DX,
  OP_rAX,
  OP_rCX,
  OP_rDX,
  OP_rBX,
  OP_rSP,
  OP_rBP,
  OP_rSI,
  OP_rDI,
  OP_ES,
  OP_CS,
  OP_SS,
  OP_DS,
  OP_FS,
  OP_GS,
  OP_1,
}

export enum ALU_Op {
  ALU_ADD = 0,
  ALU_OR,
  ALU_ADC,
  ALU_SBB,
  ALU_AND,
  ALU_SUB,
  ALU_XOR,
  ALU_CMP,
  ALU_INC,
  ALU_DEC,
}

export interface OpcodeEntry {
  mnemonic: string;
  op1: OperandType;
  op2: OperandType;
  op3: OperandType;
  hasModRM?: boolean;
}

export interface DecodeContext {
  pfx_rep: number;
  pfx_repne: number;
  pfx_op_size: number;
  pfx_addr_size: number;
  pfx_seg: number;
  is_two_byte_opcode: boolean;
  opcode: number;
  has_modrm: boolean;
  modrm: number;
  mod: number;
  reg: number;
  rm: number;
  has_sib: boolean;
  sib: number;
  scale: number;
  index: number;
  base: number;
  disp: number;
  disp_len: number;
  imm: number;
  imm2: number;
  instr_len: number;
  entry: OpcodeEntry;
  asm_str: string;
  bytes: number[];
  address: number;
}

export interface EFLAGSBits {
  CF: number;
  PF: number;
  AF: number;
  ZF: number;
  SF: number;
  TF: number;
  IF: number;
  DF: number;
  OF: number;
  IOPL: number;
  NT: number;
}

export interface FPUStatusWord {
  IE: number;
  DE: number;
  ZE: number;
  OE: number;
  UE: number;
  PE: number;
  SF: number;
  ES: number;
  C0: number;
  C1: number;
  C2: number;
  TOP: number;
  C3: number;
  B: number;
}

export interface FPUContext {
  Regs: number[]; // 8 physical double registers R0..R7
  SW: FPUStatusWord;
  CW: number;
  TW: number;
}

export interface CPUContext {
  // GPRs: 0=EAX, 1=ECX, 2=EDX, 3=EBX, 4=ESP, 5=EBP, 6=ESI, 7=EDI
  GPR: Uint32Array;
  EIP: number;
  EFLAGS: EFLAGSBits;
  seg: Uint16Array; // 0=ES, 1=CS, 2=SS, 3=DS, 4=FS, 5=GS
  FPU: FPUContext;
  Halted: boolean;
}

export interface PESectionInfo {
  name: string;
  virtualAddress: number;
  virtualSize: number;
  pointerToRawData: number;
  sizeOfRawData: number;
  characteristics: number;
}

export interface PEHeaderInfo {
  isValidPE: boolean;
  machine: number;
  numberOfSections: number;
  entryPointRVA: number;
  imageBase: number;
  sizeOfImage: number;
  sizeOfHeaders: number;
  sections: PESectionInfo[];
}
