#include "cpu.h"
#include "armv4t/cpu.h"
#include "utils.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

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

static inline uint32_t *get_sp_lr(armv4t_cpu *cpu, armv4t_mode mode) {
    switch (mode) {
    case MODE_USR:
    case MODE_SYS:
        return cpu->_internal->sp_lr.usr_sys;
    case MODE_FIQ:
        return cpu->_internal->sp_lr.fiq;
    case MODE_IRQ:
        return cpu->_internal->sp_lr.irq;
    case MODE_SVC:
        return cpu->_internal->sp_lr.svc;
    case MODE_ABT:
        return cpu->_internal->sp_lr.abt;
    case MODE_UND:
        return cpu->_internal->sp_lr.und;
    default:
        unreachable();
    }
}

static inline uint32_t *get_r8_r12(armv4t_cpu *cpu, armv4t_mode mode) {
    return mode == MODE_FIQ ? cpu->_internal->r8_r12_fiq : cpu->_internal->r8_r12_other;
}

static void switch_mode_banks(armv4t_cpu *cpu, armv4t_mode from, armv4t_mode to) {
    if ((to & from) == (MODE_USR & MODE_SYS)) {
        return;
    }

    uint32_t *from_sp_lr = get_sp_lr(cpu, from);
    uint32_t *to_sp_lr = get_sp_lr(cpu, to);
    memcpy(from_sp_lr, &cpu->regs[REG_SP], 2 * sizeof(uint32_t));
    memcpy(&cpu->regs[REG_SP], to_sp_lr, 2 * sizeof(uint32_t));

    if ((from == MODE_FIQ) != (to == MODE_FIQ)) {
        uint32_t *from_r8_r12 = get_r8_r12(cpu, from);
        uint32_t *to_r8_r12 = get_r8_r12(cpu, to);
        memcpy(from_r8_r12, &cpu->regs[8], 5 * sizeof(uint32_t));
        memcpy(&cpu->regs[8], to_r8_r12, 5 * sizeof(uint32_t));
    }
}

bool armv4t_cpu_init(armv4t_cpu **cpu, struct armv4t_mmu *mmu) {
    *cpu = calloc(1, sizeof(armv4t_cpu));
    if (!*cpu) {
        return false;
    }

    // TODO: Initialize internal

    (*cpu)->_mmu = mmu;
    armv4t_set_mode(*cpu, MODE_USR);
    return true;
}

void armv4t_cpu_destroy(armv4t_cpu *cpu) {
    free(cpu);
}

void armv4t_step(armv4t_cpu *cpu) {
    if (get_flag(cpu, FLAG_THUMB)) {
        thumb_step(cpu);
    } else {
        arm_step(cpu);
    }
}

bool has_cond(armv4t_cpu *cpu, int cond) {
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

armv4t_mode armv4t_set_mode(armv4t_cpu *cpu, armv4t_mode mode) {
    armv4t_mode current = armv4t_get_mode(cpu);
    if (mode != current) {
        switch_mode_banks(cpu, current, mode);
        cpu->cpsr = (cpu->cpsr & ~0x1F) | mode;
    }
    
    return current;
}

void restore_spsr(armv4t_cpu *cpu) {
    armv4t_psr spsr;
    armv4t_mode mode = armv4t_get_mode(cpu);
    switch (mode) {
    case MODE_FIQ:
        spsr = cpu->_internal->spsr.fiq;
        break;
    case MODE_IRQ:
        spsr = cpu->_internal->spsr.irq;
        break;
    case MODE_SVC:
        spsr = cpu->_internal->spsr.svc;
        break;
    case MODE_ABT:
        spsr = cpu->_internal->spsr.abt;
        break;
    case MODE_UND:
        spsr = cpu->_internal->spsr.und;
        break;
    default:
        spsr = cpu->cpsr;
        break;
    }

    switch_mode_banks(cpu, mode, spsr & 0x1F);
    cpu->cpsr = spsr;
}
