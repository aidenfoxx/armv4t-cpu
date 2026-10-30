#ifndef CPU_H
#define CPU_H

#include "armv4t/cpu.h"
#include "armv4t/mmu.h"

#include <stdbool.h>
#include <stdint.h>

enum {
    COND_EQ = 0,
    COND_NE,
    COND_CS,
    COND_CC,
    COND_MI,
    COND_PL,
    COND_VS,
    COND_VC,
    COND_HI,
    COND_LS,
    COND_GE,
    COND_LT,
    COND_GT,
    COND_LE,
    COND_AL
};

void arm_exec(armv4t_cpu *cpu, uint32_t inst, uint32_t pc);
void thumb_exec(armv4t_cpu *cpu, uint16_t inst, uint32_t pc);

void take_data_abort_exception(armv4t_cpu *cpu, armv4t_fsr fsr, uint32_t fsa, uint32_t pc);
void take_undefined_exception(armv4t_cpu *cpu, uint32_t next_pc);
void take_swi_exception(armv4t_cpu *cpu, uint32_t next_pc);

void restore_spsr(armv4t_cpu *cpu);

/**
 * Inline helpers.
 */
static inline bool has_cond(armv4t_cpu *cpu, uint32_t cond) {
    if (cond == COND_AL) {
        return true;
    }

    bool n = armv4t_get_flag(cpu, FLAG_N);
    bool z = armv4t_get_flag(cpu, FLAG_Z);
    bool c = armv4t_get_flag(cpu, FLAG_C);
    bool v = armv4t_get_flag(cpu, FLAG_V);

    switch (cond) {
    case COND_EQ:
        return z;
    case COND_NE:
        return !z;
    case COND_CS:
        return c;
    case COND_CC:
        return !c;
    case COND_MI:
        return n;
    case COND_PL:
        return !n;
    case COND_VS:
        return v;
    case COND_VC:
        return !v;
    case COND_HI:
        return c && !z;
    case COND_LS:
        return !c || z;
    case COND_GE:
        return n == v;
    case COND_LT:
        return n != v;
    case COND_GT:
        return !z && n == v;
    case COND_LE:
        return z || n != v;
    default:
        return false;
    }
}

static inline armv4t_psr get_spsr(armv4t_cpu *cpu) {
    armv4t_mode mode = armv4t_get_mode(cpu);
    switch (mode) {
    case MODE_FIQ:
        return cpu->_internal.spsr.fiq;
    case MODE_IRQ:
        return cpu->_internal.spsr.irq;
    case MODE_SVC:
        return cpu->_internal.spsr.svc;
    case MODE_ABT:
        return cpu->_internal.spsr.abt;
    case MODE_UND:
        return cpu->_internal.spsr.und;
    default:
        // Mode doesn't have SPSR
        return 0;
    }
}

static inline void set_spsr(armv4t_cpu *cpu, armv4t_psr spsr) {
    armv4t_mode mode = armv4t_get_mode(cpu);
    switch (mode) {
    case MODE_FIQ:
        cpu->_internal.spsr.fiq = spsr;
        break;
    case MODE_IRQ:
        cpu->_internal.spsr.irq = spsr;
        break;
    case MODE_SVC:
        cpu->_internal.spsr.svc = spsr;
        break;
    case MODE_ABT:
        cpu->_internal.spsr.abt = spsr;
        break;
    case MODE_UND:
        cpu->_internal.spsr.und = spsr;
        break;
    default:
        // Mode doesn't have SPSR
        return;
    }
}

#endif // CPU_H
