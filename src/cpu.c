#include "cpu.h"
#include "armv4t/cpu.h"

#include <stdlib.h>

// Shorthand to prevent "has_cond" getting too messy
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

bool armv4t_cpu_init(armv4t_cpu **cpu, struct armv4t_mmu *mmu) {
    *cpu = calloc(1, sizeof(armv4t_cpu));
    if (!*cpu) {
        return false;
    }

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
    }

    return false;
}

void restore_spsr(armv4t_cpu *cpu) {
    switch (armv4t_get_mode(cpu)) {
    case MODE_FIQ:
        cpu->cpsr = cpu->spsr.fiq;
        break;
    case MODE_IRQ:
        cpu->cpsr = cpu->spsr.irq;
        break;
    case MODE_SVC:
        cpu->cpsr = cpu->spsr.svc;
        break;
    case MODE_ABT:
        cpu->cpsr = cpu->spsr.abt;
        break;
    case MODE_UND:
        cpu->cpsr = cpu->spsr.und;
        break;
    default:
        break;
    }
}
