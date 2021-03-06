#ifndef INSTRUCTION_RELOCATION_X64_H
#define INSTRUCTION_RELOCATION_X64_H

#include "ExecMemory/AssemblyCode.h"

#include "core/arch/x64/constants-x64.h"

namespace zz {
namespace x64 {

// Generate the relocated instruction
AssemblyCode *GenRelocateCode(void *buffer, int *relocate_size, addr_t from_pc, addr_t to_pc);

} // namespace x64
} // namespace zz

#endif