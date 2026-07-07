#include "cpu.h"
#include "armv4t/cpu.h"
#include "armv4t/mmu.h"

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

enum {
    VEC_UND = 0x04,
    VEC_SWI = 0x08,
    VEC_PABT = 0x0C,
    VEC_DABT = 0x10,
    VEC_IRQ = 0x18,
    VEC_FIQ = 0x1C,
};

static void take_exception(armv4t_cpu *cpu, armv4t_mode mode, uint32_t vector, uint32_t lr) {
    armv4t_psr cpsr = cpu->cpsr;
    armv4t_set_mode(cpu, mode);
    set_spsr(cpu, cpsr);

    armv4t_set_flag(cpu, FLAG_THUMB, false);
    armv4t_set_flag(cpu, FLAG_IRQ, true);
    if (mode == MODE_FIQ) {
        armv4t_set_flag(cpu, FLAG_FIQ, true);
    }
    
    cpu->regs[REG_LR] = lr;
    cpu->regs[REG_PC] = vector;
}

static uint32_t *get_sp_lr(armv4t_cpu *cpu, armv4t_mode mode) {
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
        // We treat invalid modes as a no-op
        return &cpu->regs[REG_SP];
    }
}

static uint32_t *get_r8_r12(armv4t_cpu *cpu, armv4t_mode mode) {
    return mode == MODE_FIQ ? cpu->_internal->r8_r12_fiq : cpu->_internal->r8_r12_other;
}

static void switch_mode_banks(armv4t_cpu *cpu, armv4t_mode from, armv4t_mode to) {
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

static bool modes_share_banks(armv4t_mode a, armv4t_mode b) {
    bool a_usr_sys = a == MODE_USR || a == MODE_SYS;
    bool b_usr_sys = b == MODE_USR || b == MODE_SYS;
    return a == b || (a_usr_sys && b_usr_sys);
}

bool armv4t_cpu_init(armv4t_cpu **cpu, struct armv4t_mmu *mmu) {
    *cpu = calloc(1, sizeof(armv4t_cpu));
    if (*cpu == NULL) {
        return false;
    }

    struct armv4t_internal *internal = calloc(1, sizeof(struct armv4t_internal));
    if (internal == NULL) {
        free(*cpu);
        return false;
    }
    
    (*cpu)->cpsr = MODE_SYS;
    (*cpu)->_mmu = mmu;
    (*cpu)->_internal = internal;
    return true;
}


void armv4t_cpu_destroy(armv4t_cpu *cpu) {
    free(cpu->_internal);
    free(cpu);
}

void armv4t_set_mode(armv4t_cpu *cpu, armv4t_mode mode) {
    armv4t_mode current = armv4t_get_mode(cpu);
    if (!modes_share_banks(current, mode)) {
        switch_mode_banks(cpu, current, mode);
    }

    if (mode != current) {
        cpu->cpsr = (cpu->cpsr & ~0x1F) | mode;
    }
}

void armv4t_step(armv4t_cpu *cpu) {
    if (get_flag(cpu, FLAG_THUMB)) {
        thumb_step(cpu);
    } else {
        arm_step(cpu);
    }
}

bool has_cond(armv4t_cpu *cpu, uint32_t cond) {
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

armv4t_psr get_spsr(armv4t_cpu *cpu) {
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

void set_spsr(armv4t_cpu *cpu, armv4t_psr spsr) {
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

void restore_spsr(armv4t_cpu *cpu) {
    armv4t_psr spsr = get_spsr(cpu);
    if (spsr != 0) {
        switch_mode_banks(cpu, armv4t_get_mode(cpu), spsr & 0x1F);
        cpu->cpsr = spsr;
    }
}

void raise_irq(armv4t_cpu *cpu) {
    if (!get_flag(cpu, FLAG_IRQ)) {
        take_exception(cpu, MODE_IRQ, VEC_IRQ, cpu->regs[REG_PC] + 4);
    }
}

void raise_fiq(armv4t_cpu *cpu) {
    if (!get_flag(cpu, FLAG_FIQ)) {
        take_exception(cpu, MODE_FIQ, VEC_FIQ, cpu->regs[REG_PC] + 4);
    }
}

void raise_data_abort(armv4t_cpu *cpu, armv4t_fsr fsr, uint32_t fsa) {
    cpu->cp15[5] = fsr;
    cpu->cp15[6] = fsa;
    uint32_t lr = cpu->regs[REG_PC] + (get_flag(cpu, FLAG_THUMB) ? 4 : 0);
    take_exception(cpu, MODE_ABT, VEC_DABT, lr);
}

void raise_prefetch_abort(armv4t_cpu *cpu) {
    take_exception(cpu, MODE_ABT, VEC_PABT, cpu->regs[REG_PC] + 4);
}

void raise_undefined(armv4t_cpu *cpu) {
    uint32_t lr = cpu->regs[REG_PC] - (get_flag(cpu, FLAG_THUMB) ? 2 : 4);
    take_exception(cpu, MODE_UND, VEC_UND, lr);
}

void raise_swi(armv4t_cpu *cpu) {
    uint32_t lr = cpu->regs[REG_PC] - (get_flag(cpu, FLAG_THUMB) ? 2 : 4);
    take_exception(cpu, MODE_SVC, VEC_SWI, lr);
}
