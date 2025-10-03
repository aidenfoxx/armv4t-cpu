#include "cpu.h"

#include <string.h>
#include <stdbool.h>

enum {
    COND_EQ,
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

void cpu_init(cpu_t *cpu, mmu_t *mmu) {
    memset(cpu, 0, sizeof(cpu_t));
    cpu->mmu = mmu;
    cpu->cpsr.m = MODE_USER;
}

void cpu_step(cpu_t *cpu) {
    cpu->cpsr.t ? thumb_step(cpu) : arm_step(cpu);
}

bool has_cond(cpu_t *cpu, int cond) {
    switch (cond) {
    case COND_EQ:
        return cpu->cpsr.z;
    case COND_NE:
        return !cpu->cpsr.z;
    case COND_CS:
        return cpu->cpsr.c;
    case COND_CC:
        return !cpu->cpsr.c;
    case COND_MI:
        return cpu->cpsr.n;
    case COND_PL:
        return !cpu->cpsr.n;
    case COND_VS:
        return cpu->cpsr.v;
    case COND_VC:
        return !cpu->cpsr.v;
    case COND_HI:
        return cpu->cpsr.c && !cpu->cpsr.z;
    case COND_LS:
        return !cpu->cpsr.c || cpu->cpsr.z;
    case COND_GE:
        return cpu->cpsr.n == cpu->cpsr.v;
    case COND_LT:
        return cpu->cpsr.n != cpu->cpsr.v;
    case COND_GT:
        return !cpu->cpsr.z && cpu->cpsr.n == cpu->cpsr.v;
    case COND_LE:
        return cpu->cpsr.z || cpu->cpsr.n != cpu->cpsr.v;
    case COND_AL:
        return true;
    }

    return false;
}
