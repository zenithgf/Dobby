#include "./ARM64InstructionRelocation.h"

#include "dobby_internal.h"

#include "core/arch/arm64/registers-arm64.h"
#include "core/modules/assembler/assembler-arm64.h"
#include "core/modules/codegen/codegen-arm64.h"

using namespace zz::arm64;

// Compare and branch.
enum CompareBranchOp {
  CompareBranchFixed     = 0x34000000,
  CompareBranchFixedMask = 0x7E000000,
  CompareBranchMask      = 0xFF000000,
};

// Conditional branch.
enum ConditionalBranchOp {
  ConditionalBranchFixed     = 0x54000000,
  ConditionalBranchFixedMask = 0xFE000000,
  ConditionalBranchMask      = 0xFF000010,
};

// Test and branch.
enum TestBranchOp {
  TestBranchFixed     = 0x36000000,
  TestBranchFixedMask = 0x7E000000,
  TestBranchMask      = 0x7F000000,
  TBZ                 = TestBranchFixed | 0x00000000,
  TBNZ                = TestBranchFixed | 0x01000000
};

typedef struct _PseudoDataLabel {
  PseudoLabel label;
  uint64_t address;

public:
  _PseudoDataLabel(uint64_t data) {
    address = data;
  }
} PseudoDataLabel;

PseudoDataLabel *CreatePseudoDataLabel(uint64_t data) {
  return new PseudoDataLabel(data);
}

static inline int64_t SignExtend(unsigned long x, int M, int N) {
#if 0
  char sign_bit      = bit(x, M - 1);
  unsigned long sign_mask = 0 - sign_bit;
  x |= ((sign_mask >> M) << M);
#else
  x = (long)(x << (N - M)) >> (N - M);
#endif
  return x;
}

#if 0
static inline unsigned long set_bit(obj, st, unsigned long bit) {
  return (((~(1 << st)) & obj) | (bit << st));
}
static inline unsigned long set_bits(obj, st, fn, unsigned long bits) {
  return (((~(submask(fn - st) << st)) & obj) | (bits << st));
}
#endif

static inline int64_t decode_imm14_offset(uint32_t instr) {
  int64_t offset;
  {
    int64_t imm19 = bits(instr, 5, 18);
    offset        = (imm19 << 2);
  }
  offset = SignExtend(offset, 2 + 14, 64);
  return offset;
}

static inline int64_t decode_imm19_offset(uint32_t instr) {
  int64_t offset;
  {
    int64_t imm19 = bits(instr, 5, 23);
    offset        = (imm19 << 2);
  }
  offset = SignExtend(offset, 2 + 19, 64);
  return offset;
}

static inline int64_t decode_imm26_offset(uint32_t instr) {
  int64_t offset;
  {
    int64_t imm26 = bits(instr, 0, 25);
    offset        = (imm26 << 2);
  }
  offset = SignExtend(offset, 2 + 26, 64);
  return offset;
}

static inline int64_t decode_immhi_immlo_offset(uint32_t instr) {
  typedef uint32_t instr_t;
  struct {
    instr_t Rd : 5;      // Destination register
    instr_t immhi : 19;  // 19-bit upper immediate
    instr_t dummy_0 : 5; // Must be 10000 == 0x10
    instr_t immlo : 2;   // 2-bit lower immediate
    instr_t op : 1;      // 0 = ADR, 1 = ADRP
  } instr_decode;

  *(instr_t *)&instr_decode = instr;

  int64_t imm = instr_decode.immlo + (instr_decode.immhi << 2);
  imm         = SignExtend(imm, 2 + 19, 64);
  return imm;
}

static inline int64_t decode_immhi_immlo_zero12_offset(uint32_t instr) {
  int64_t imm = decode_immhi_immlo_offset(instr);
  imm         = imm << 12;
  return imm;
}

static inline int decode_rt(uint32_t instr) {
  return bits(instr, 0, 4);
}

static inline int decode_rd(uint32_t instr) {
  return bits(instr, 0, 4);
}

void GenRelocateCode(void *buffer, AssemblyCode *origin, AssemblyCode *relocated) {
  // std::vector<PseudoDataLabel> labels;
  LiteMutableArray *labels = new LiteMutableArray;

  TurboAssembler turbo_assembler_(0);
#define _ turbo_assembler_.

  uint64_t curr_orig_pc = origin->raw_instruction_start();
  uint64_t curr_relo_pc = relocated->raw_instruction_start();

  addr_t buffer_cursor = (addr_t)buffer;
  arm64_inst_t instr   = *(arm64_inst_t *)buffer_cursor;

  int predefined_relocate_size = origin->raw_instruction_size();

  while (buffer_cursor < ((addr_t)buffer + predefined_relocate_size)) {
    int last_relo_offset = turbo_assembler_.GetCodeBuffer()->getSize();

    if ((instr & LoadRegLiteralFixedMask) == LoadRegLiteralFixed) { // ldr x0, #16
      int rt                  = decode_rt(instr);
      char opc                = bits(instr, 30, 31);
      addr64_t memory_address = decode_imm19_offset(instr) + curr_orig_pc;

#define MEM(reg, offset) MemOperand(reg, offset)
      _ nop(); // for debug
      {
        _ Mov(X(17), memory_address); // should we replace with `Ldr` to set  X17 ?
        if (opc == 0b00)
          _ ldr(W(rt), MEM(X(17), 0));
        else if (opc == 0b01)
          _ ldr(X(rt), MEM(X(17), 0));
        else {
          UNIMPLEMENTED();
        }
      }
      _ nop();
    } else if ((instr & PCRelAddressingFixedMask) == PCRelAddressingFixed) {
      int rd = decode_rd(instr);

      int64_t imm              = 0;
      addr64_t runtime_address = 0;
      if ((instr & PCRelAddressingMask) == ADR) {
        imm             = decode_immhi_immlo_offset(instr);
        runtime_address = curr_orig_pc + imm;
      } else {
        imm             = decode_immhi_immlo_zero12_offset(instr);
        runtime_address = ALIGN_FLOOR(curr_orig_pc, (1 << 12)) + imm;
      }

      /* output Mov */
      _ nop();
      {
        _ Mov(X(rd), runtime_address); // should we replace with `Ldr` to set  X17 ?
      }
      _ nop();

    } else if ((instr & UnconditionalBranchFixedMask) == UnconditionalBranchFixed) { // b xxx
      addr_t branch_address               = decode_imm26_offset(instr) + curr_orig_pc;
      PseudoDataLabel *branchAddressLabel = CreatePseudoDataLabel(branch_address);
      labels->pushObject((LiteObject *)branchAddressLabel);

      _ nop();
      {
        _ Ldr(x17, &branchAddressLabel->label); // should we replace with `Mov` to set  X17 ?
        if ((instr & UnconditionalBranchMask) == BL) {
          _ blr(x17);
        } else {
          _ br(x17);
        }
      }
      _ nop();
    } else if ((instr & TestBranchFixedMask) == TestBranchFixed) { // tbz, tbnz
      addr64_t branch_address             = decode_imm14_offset(instr) + curr_orig_pc;
      PseudoDataLabel *branchAddressLabel = CreatePseudoDataLabel(branch_address);
      labels->pushObject((LiteObject *)branchAddressLabel);

      arm64_inst_t branch_instr = instr;

      char op = bit(instr, 24);
      op      = op ^ 1;
      set_bit(branch_instr, 24, op);

      int64_t offset = 4 * 3; // branch_instr; ldr x17, #label; br x17
      uint32_t imm14 = offset >> 2;
      set_bits(branch_instr, 5, 18, imm14);

      _ nop();
      {
        _ Emit(branch_instr);
        {
          _ Ldr(x17, &branchAddressLabel->label); // should we replace with `Mov` to set  X17 ?
          _ br(x17);
        }
      }
      _ nop();

    } else if ((instr & CompareBranchFixedMask) == CompareBranchFixed) { // cbz cbnz
      addr64_t branch_address = decode_imm19_offset(instr) + curr_orig_pc;

      arm64_inst_t branch_instr = instr;

      char op = bit(instr, 24);
      op      = op ^ 1;
      set_bit(branch_instr, 24, op);

      int64_t offset = 4 * 3;
      uint32_t imm19 = offset >> 2;
      set_bits(branch_instr, 5, 23, imm19);

      PseudoDataLabel *branchAddressLabel = CreatePseudoDataLabel(branch_address);
      labels->pushObject((LiteObject *)branchAddressLabel);

      _ nop();
      {
        _ Emit(branch_instr);
        {
          _ Ldr(x17, &branchAddressLabel->label); // should we replace with `Mov` to set  X17 ?
          _ br(x17);
        }
      }
      _ nop();
    } else if ((instr & ConditionalBranchFixedMask) == ConditionalBranchFixed) { // b.cond
      addr64_t branch_address = decode_imm19_offset(instr) + curr_orig_pc;

      arm64_inst_t branch_instr = instr;

      char cond = bits(instr, 0, 3);
      cond      = cond ^ 1;
      set_bits(branch_instr, 0, 3, cond);

      int64_t offset = 4 * 3;
      uint32_t imm19 = offset >> 2;
      set_bits(branch_instr, 5, 23, imm19);

      PseudoDataLabel *branchAddressLabel = CreatePseudoDataLabel(branch_address);
      labels->pushObject((LiteObject *)branchAddressLabel);

      _ nop();
      {
        _ Emit(branch_instr);
        {
          _ Ldr(x17, &branchAddressLabel->label); // should we replace with `Mov` to set  X17 ?
          _ br(x17);
        }
      }
      _ nop();
    } else {
      // origin write the instruction bytes
      _ Emit(instr);
    }

    // Move to next instruction
    curr_orig_pc += 4;
    buffer_cursor += 4;

    {
      // 1 orignal instrution => ? relocated instruction
      int relo_offset = turbo_assembler_.GetCodeBuffer()->getSize();
      int relo_len    = relo_offset - last_relo_offset;
      curr_relo_pc += relo_len;
    }

    instr = *(arm64_inst_t *)buffer_cursor;

    { // check branch in relocate-code range
      LiteCollectionIterator *iter = NULL;
      PseudoDataLabel *dataLabel   = NULL;
      // Realize all the Pseudo-Label-Data
      iter = LiteCollectionIterator::withCollection(labels);
      while ((dataLabel = reinterpret_cast<PseudoDataLabel *>(iter->getNextObject())) != NULL) {
        if (dataLabel->address == curr_orig_pc) {
          FATAL("label(%p) in relo code %p, please enable b-xxx branch plugin.", dataLabel->address, curr_orig_pc);
        }
      }
      delete iter;
    }
  }

  // Branch to the rest of instructions
  CodeGen codegen(&turbo_assembler_);
  codegen.LiteralLdrBranch(curr_orig_pc);

// FIXME: remove
#if 0 // NO STL
  for (auto it : labels) {
    _ PseudoBind(&(it.label));
    _ EmitInt64(it.address);
  }
#endif

  // Realize all the Pseudo-Label-Data
  LiteCollectionIterator *iter = NULL;
  PseudoDataLabel *dataLabel   = NULL;
  iter                         = LiteCollectionIterator::withCollection(labels);
  while ((dataLabel = reinterpret_cast<PseudoDataLabel *>(iter->getNextObject())) != NULL) {
    _ PseudoBind(&(dataLabel->label));
    _ EmitInt64(dataLabel->address);
  }

  // Generate executable code
  {
    AssemblyCode *code = NULL;
    code               = AssemblyCode::FinalizeFromTurboAssember(&turbo_assembler_);
    relocated->reInitWithAddressRange(code->raw_instruction_start(), code->raw_instruction_size());
    delete code;
  }
}
