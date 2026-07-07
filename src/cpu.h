#ifndef CPU_H
#define CPU_H

#include "armv4t/cpu.h"
#include "armv4t/mmu.h"

#include <stdbool.h>
#include <stdint.h>

struct armv4t_internal {
    uint32_t r8_r12_fiq[5];
    uint32_t r8_r12_other[5];
    struct {
        uint32_t usr_sys[2];
        uint32_t fiq[2];
        uint32_t irq[2];
        uint32_t svc[2];
        uint32_t abt[2];
        uint32_t und[2];
    } sp_lr;
    struct {
        armv4t_psr fiq;
        armv4t_psr irq;
        armv4t_psr svc;
        armv4t_psr abt;
        armv4t_psr und;
    } spsr;
};

void arm_step(armv4t_cpu *cpu);
void thumb_step(armv4t_cpu *cpu);

bool has_cond(armv4t_cpu *cpu, uint32_t cond);

void raise_irq(armv4t_cpu *cpu);
void raise_fiq(armv4t_cpu *cpu);
void raise_data_abort(armv4t_cpu *cpu, armv4t_fsr fsr, uint32_t fsa);
void raise_prefetch_abort(armv4t_cpu *cpu);
void raise_undefined(armv4t_cpu *cpu);
void raise_swi(armv4t_cpu *cpu);

armv4t_psr get_spsr(armv4t_cpu *cpu);
void set_spsr(armv4t_cpu *cpu, armv4t_psr spsr);
void restore_spsr(armv4t_cpu *cpu);

#endif // CPU_H
