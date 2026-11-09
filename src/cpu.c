#include "armv4t/cpu.h"
#include "armv4t/mmu.h"
#include "cpu.h"
#include "utils.h"

#include <stdint.h>
#include <string.h>

#define NO_INST 0

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
    armv4t_branch(cpu, vector);
}

static void take_interrupt(armv4t_cpu *cpu) {
    if (unlikely(cpu->_internal.fiq_line | cpu->_internal.irq_line)) {
        if (cpu->_internal.fiq_line && !armv4t_get_flag(cpu, FLAG_FIQ)) {
            take_exception(cpu, MODE_FIQ, VEC_FIQ, cpu->regs[REG_PC] + 4);
        } else if (cpu->_internal.irq_line && !armv4t_get_flag(cpu, FLAG_IRQ)) {
            take_exception(cpu, MODE_IRQ, VEC_IRQ, cpu->regs[REG_PC] + 4);
        }
    }
}

static uint32_t *get_sp_lr_bank(armv4t_cpu *cpu, armv4t_mode mode) {
    switch (mode) {
    case MODE_FIQ:
        return cpu->_internal.sp_lr.fiq;
    case MODE_IRQ:
        return cpu->_internal.sp_lr.irq;
    case MODE_SVC:
        return cpu->_internal.sp_lr.svc;
    case MODE_ABT:
        return cpu->_internal.sp_lr.abt;
    case MODE_UND:
        return cpu->_internal.sp_lr.und;
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
        swap_bank(&cpu->regs[8], cpu->_internal.r8_r12, 5);
    }
}

FORCE_INLINE
static uint32_t read_page_inst(const uint8_t *page, uint32_t addr, bool thumb) {
    uint32_t offset = addr % ARMV4T_REQUEST_PAGE_SIZE;
    if (thumb) {
        uint16_t inst;
        memcpy(&inst, page + offset, 2);
        return inst;
    } else {
        uint32_t inst;
        memcpy(&inst, page + offset, 4);
        return inst;
    }
}

FORCE_INLINE
static armv4t_fsr load_inst(armv4t_mmu *mmu, uint32_t addr, uint32_t *inst, bool thumb) {
    if (thumb) {
        uint16_t value;
        armv4t_fsr fsr = armv4t_ld_16(mmu, addr, &value);
        *inst = value;
        return fsr;
    } else {
        return armv4t_ld_32(mmu, addr, inst);
    }
}

FORCE_INLINE
static uint64_t run_mode(armv4t_cpu *cpu, armv4t_mmu *mmu, uint64_t steps, bool *exception,
                         bool thumb) {
    uint32_t size = thumb ? 2 : 4;
    uint32_t pc = align_down(cpu->regs[REG_PC], size);

    uint8_t *page;
    uint32_t page_addr = 1; // Misaligned to force a page update
    uint32_t next_inst = NO_INST;

    for (uint64_t i = 0; i < steps; i++) {
        uint32_t inst = next_inst;
        uint32_t next_pc = pc + size;

        if (unlikely(inst == NO_INST)) {
            uint32_t addr = align_down(pc, ARMV4T_REQUEST_PAGE_SIZE);
            if (unlikely(addr != page_addr)) {
                page = armv4t_request_page(mmu, addr);
                page_addr = addr;
            }

            if (page != NULL) {
                inst = read_page_inst(page, pc, thumb);
                goto prefetch;
            } else if (unlikely(load_inst(mmu, pc, &inst, thumb) != 0)) {
                take_prefetch_abort_exception(cpu, pc);
                *exception = true;
                return i;
            }
        } else {
        prefetch:
            if (unlikely((next_pc % ARMV4T_REQUEST_PAGE_SIZE) == 0)) {
                next_inst = NO_INST;
            } else {
                next_inst = read_page_inst(page, next_pc, thumb);
            }
        }

        exec_state new_state;
        if (thumb) {
            new_state = thumb_exec(cpu, mmu, (uint16_t)inst, pc);
        } else {
            new_state = arm_exec(cpu, mmu, inst, pc);
        }

        if (unlikely((new_state & STATE_BRANCH) != 0)) {
            if (unlikely((new_state & STATE_EXCEPTION) != 0)) {
                *exception = true;
                return i;
            }

            // Lazily handle interrupts on branch
            take_interrupt(cpu);

            if (unlikely(armv4t_get_flag(cpu, FLAG_THUMB) != thumb)) {
                return i;
            }

            pc = cpu->regs[REG_PC];
            next_inst = NO_INST;
        } else {
            pc = next_pc;
        }
    }

    cpu->regs[REG_PC] = pc;
    return steps;
}

void armv4t_cpu_init(armv4t_cpu *cpu) {
    memset(cpu, 0, sizeof(armv4t_cpu));
    cpu->cpsr = MODE_SYS;
}

void armv4t_set_mode(armv4t_cpu *cpu, armv4t_mode mode) {
    armv4t_mode current = armv4t_get_mode(cpu);
    if (mode != current) {
        switch_mode_banks(cpu, current, mode);
        cpu->cpsr = (cpu->cpsr & ~0x1F) | mode;
    }
}

uint64_t armv4t_run(armv4t_cpu *cpu, armv4t_mmu *mmu, uint64_t steps, bool halt_on_exception) {
    for (uint64_t i = 0; i < steps; i++) {
        bool exception = false;
        if (armv4t_get_flag(cpu, FLAG_THUMB)) {
            i += run_mode(cpu, mmu, steps - i, &exception, true);
        } else {
            i += run_mode(cpu, mmu, steps - i, &exception, false);
        }

        if (halt_on_exception && unlikely(exception)) {
            return i;
        }
    }

    return steps;
}

void armv4t_restore_spsr(armv4t_cpu *cpu) {
    armv4t_psr spsr = get_spsr(cpu);
    if (spsr != 0) {
        switch_mode_banks(cpu, armv4t_get_mode(cpu), spsr & 0x1F);
        cpu->cpsr = spsr;
    }
}

void take_prefetch_abort_exception(armv4t_cpu *cpu, uint32_t pc) {
    take_exception(cpu, MODE_ABT, VEC_PABT, pc + 4);
}

void take_data_abort_exception(armv4t_cpu *cpu, armv4t_fsr fsr, uint32_t fsa, uint32_t pc) {
    cpu->cp15[5] = fsr;
    cpu->cp15[6] = fsa;
    take_exception(cpu, MODE_ABT, VEC_DABT, pc + 8);
}

void take_undefined_exception(armv4t_cpu *cpu, uint32_t next_pc) {
    take_exception(cpu, MODE_UND, VEC_UND, next_pc);
}

void take_swi_exception(armv4t_cpu *cpu, uint32_t next_pc) {
    take_exception(cpu, MODE_SVC, VEC_SWI, next_pc);
}
