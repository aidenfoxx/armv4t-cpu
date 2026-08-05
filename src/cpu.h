#ifndef CPU_H
#define CPU_H

#include "armv4t/cpu.h"
#include "armv4t/mmu.h"

#include <stdbool.h>
#include <stdint.h>

#define get_flag armv4t_get_flag

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

struct armv4t_internal {
    // TODO: Pick better names than "_pending"
    bool irq_pending;
    bool fiq_pending;
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

void take_fiq_exception(armv4t_cpu *cpu, uint32_t pc);
void take_irq_exception(armv4t_cpu *cpu, uint32_t pc);
void take_prefetch_abort_exception(armv4t_cpu *cpu, uint32_t pc);
void take_data_abort_exception(armv4t_cpu *cpu, armv4t_fsr fsr, uint32_t fsa, uint32_t pc);
void take_undefined_exception(armv4t_cpu *cpu, uint32_t next_pc);
void take_swi_exception(armv4t_cpu *cpu, uint32_t next_pc);

void restore_spsr(armv4t_cpu *cpu);

/**
 * Inline helpers.
 */
static inline bool has_cond(armv4t_cpu *cpu, uint32_t cond) {
    switch (cond) {
    case COND_EQ:
        return get_flag(cpu, FLAG_Z);
    case COND_NE:
        return !get_flag(cpu, FLAG_Z);
    case COND_CS:
        return get_flag(cpu, FLAG_C);
    case COND_CC:
        return !get_flag(cpu, FLAG_C);
    case COND_MI:
        return get_flag(cpu, FLAG_N);
    case COND_PL:
        return !get_flag(cpu, FLAG_N);
    case COND_VS:
        return get_flag(cpu, FLAG_V);
    case COND_VC:
        return !get_flag(cpu, FLAG_V);
    case COND_HI:
        return get_flag(cpu, FLAG_C) && !get_flag(cpu, FLAG_Z);
    case COND_LS:
        return !get_flag(cpu, FLAG_C) || get_flag(cpu, FLAG_Z);
    case COND_GE:
        return get_flag(cpu, FLAG_N) == get_flag(cpu, FLAG_V);
    case COND_LT:
        return get_flag(cpu, FLAG_N) != get_flag(cpu, FLAG_V);
    case COND_GT:
        return !get_flag(cpu, FLAG_Z) && get_flag(cpu, FLAG_N) == get_flag(cpu, FLAG_V);
    case COND_LE:
        return get_flag(cpu, FLAG_Z) || get_flag(cpu, FLAG_N) != get_flag(cpu, FLAG_V);
    case COND_AL:
        return true;
    default:
        return false;
    }
}

static inline armv4t_psr get_spsr(armv4t_cpu *cpu) {
    armv4t_mode mode = armv4t_get_mode(cpu);
    switch (mode) {
    case MODE_FIQ:
        return cpu->_internal->spsr.fiq;
    case MODE_IRQ:
        return cpu->_internal->spsr.irq;
    case MODE_SVC:
        return cpu->_internal->spsr.svc;
    case MODE_ABT:
        return cpu->_internal->spsr.abt;
    case MODE_UND:
        return cpu->_internal->spsr.und;
    default:
        // Mode doesn't have SPSR
        return 0;
    }
}

static inline void set_spsr(armv4t_cpu *cpu, armv4t_psr spsr) {
    armv4t_mode mode = armv4t_get_mode(cpu);
    switch (mode) {
    case MODE_FIQ:
        cpu->_internal->spsr.fiq = spsr;
        break;
    case MODE_IRQ:
        cpu->_internal->spsr.irq = spsr;
        break;
    case MODE_SVC:
        cpu->_internal->spsr.svc = spsr;
        break;
    case MODE_ABT:
        cpu->_internal->spsr.abt = spsr;
        break;
    case MODE_UND:
        cpu->_internal->spsr.und = spsr;
        break;
    default:
        // Mode doesn't have SPSR
        return;
    }
}

#endif // CPU_H
