#include "cpu.h"

void thumb_step(armv4t_cpu *cpu) {
    uint32_t inst;
    armv4t_fsr fsr = armv4t_ld_32(cpu->_mmu, cpu->regs[REG_PC], &inst);
    if (fsr != 0) {
        raise_prefetch_abort(cpu, fsr, cpu->regs[REG_PC]);
        return;
    }

    // TODO: Implement

    raise_undefined(cpu, inst);
}
