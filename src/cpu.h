#ifndef CPU_H
#define CPU_H

#include "armv4t/cpu.h"
#include "armv4t/mmu.h"

#include <stdbool.h>
#include <stdint.h>

static inline void set_nzcv(armv4t_cpu *cpu, unsigned int nzcv) {
    cpu->cpsr = (cpu->cpsr & 0xfffffff) | (nzcv << 28);
}

void arm_step(armv4t_cpu *cpu);
void thumb_step(armv4t_cpu *cpu);

bool has_cond(armv4t_cpu *cpu, int cond);

void raise_irq(armv4t_cpu *cpu);
void raise_fiq(armv4t_cpu *cpu);
void raise_data_abort(armv4t_cpu *cpu, armv4t_fsr fsr, uint32_t fsa);
void raise_prefetch_abort(armv4t_cpu *cpu, armv4t_fsr fsr, uint32_t fsa);
void raise_undefined(armv4t_cpu *cpu, uint32_t inst);
void raise_swi(armv4t_cpu *cpu, uint32_t imm);

#endif // CPU_H
