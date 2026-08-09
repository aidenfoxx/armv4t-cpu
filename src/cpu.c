#include "cpu.h"
#include "armv4t/cpu.h"
#include "armv4t/mmu.h"
#include "utils.h"

#include <stdint.h>
#include <stdlib.h>

enum {
    VEC_UND = 0x04,
    VEC_SWI = 0x08,
    VEC_PABT = 0x0C,
    VEC_DABT = 0x10,
    VEC_IRQ = 0x18,
    VEC_FIQ = 0x1C,
};

NOINLINE
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
    armv4t_branch(cpu, vector);
}

static uint32_t *get_sp_lr_bank(armv4t_cpu *cpu, armv4t_mode mode) {
    switch (mode) {
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
        // Mode doesn't have banked registers
        return NULL;
    }
}

static void swap_bank(uint32_t *regs, uint32_t *bank, int count) {
    for (int i = 0; i < count; i++) {
        uint32_t temp = regs[i];
        regs[i] = bank[i];
        bank[i] = temp;
    }
}

static void switch_mode_banks(armv4t_cpu *cpu, armv4t_mode from, armv4t_mode to) {
    uint32_t *from_sp_lr = get_sp_lr_bank(cpu, from);
    if (from_sp_lr != NULL) {
        swap_bank(&cpu->regs[REG_SP], from_sp_lr, 2);
    }

    uint32_t *to_sp_lr = get_sp_lr_bank(cpu, to);
    if (to_sp_lr != NULL) {
        swap_bank(&cpu->regs[REG_SP], to_sp_lr, 2);
    }

    if ((from == MODE_FIQ) != (to == MODE_FIQ)) {
        swap_bank(&cpu->regs[8], cpu->_internal->r8_r12, 5);
    }
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
    if (mode != current) {
        switch_mode_banks(cpu, current, mode);
        cpu->cpsr = (cpu->cpsr & ~0x1F) | mode;
    }
}

void armv4t_set_irq(armv4t_cpu *cpu, bool irq) {
    cpu->_internal->irq_line = irq;
}

void armv4t_set_fiq(armv4t_cpu *cpu, bool fiq) {
    cpu->_internal->fiq_line = fiq;
}

void armv4t_step(armv4t_cpu *cpu) {
    if (unlikely(cpu->_internal->fiq_line && !get_flag(cpu, FLAG_FIQ))) {
        uint32_t lr = cpu->regs[REG_PC] + 4;
        take_exception(cpu, MODE_FIQ, VEC_FIQ, lr);
        return;
    }
    if (unlikely(cpu->_internal->irq_line && !get_flag(cpu, FLAG_IRQ))) {
        uint32_t lr = cpu->regs[REG_PC] + 4;
        take_exception(cpu, MODE_IRQ, VEC_IRQ, lr);
        return;
    }

    if (get_flag(cpu, FLAG_THUMB)) {
        thumb_step(cpu);
    } else {
        arm_step(cpu);
    }
}

void take_prefetch_abort_exception(armv4t_cpu *cpu, uint32_t pc) {
    uint32_t lr = pc + 4;
    take_exception(cpu, MODE_ABT, VEC_PABT, lr);
}

void take_data_abort_exception(armv4t_cpu *cpu, armv4t_fsr fsr, uint32_t fsa, uint32_t pc) {
    cpu->cp15[5] = fsr;
    cpu->cp15[6] = fsa;

    uint32_t lr = pc + 8;
    take_exception(cpu, MODE_ABT, VEC_DABT, lr);
}

void take_undefined_exception(armv4t_cpu *cpu, uint32_t next_pc) {
    take_exception(cpu, MODE_UND, VEC_UND, next_pc);
}

void take_swi_exception(armv4t_cpu *cpu, uint32_t next_pc) {
    take_exception(cpu, MODE_SVC, VEC_SWI, next_pc);
}

void restore_spsr(armv4t_cpu *cpu) {
    armv4t_psr spsr = get_spsr(cpu);
    if (spsr != 0) {
        switch_mode_banks(cpu, armv4t_get_mode(cpu), spsr & 0x1F);
        cpu->cpsr = spsr;
    }
}
