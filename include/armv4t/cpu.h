#ifndef ARMV4T_CPU_H
#define ARMV4T_CPU_H

#include "armv4t/mmu.h"

#include <assert.h>
#include <stdint.h>

#define REG_SP 13
#define REG_LR 14
#define REG_PC 15

typedef enum {
    MODE_USER = 0x10,
    MODE_FIQ,        // Unimplemented
    MODE_IRQ,        // Unimplemented
    MODE_SUPERVISOR, // Unimplemented
    MODE_ABORT,
    MODE_UNDEFINED,
    MODE_SYSTEM, // Unimplemented
} cpu_mode_t;

typedef union {
    uint32_t regs[15];
    struct {
        uint32_t _;
        uint32_t __;
        uint32_t ___;
        uint32_t ____;
        uint32_t c5;
        uint32_t c6;
    };
} cp15_t;

typedef union {
    uint32_t value;
    struct {
        uint8_t m : 5;
        uint8_t t : 1;
        uint32_t _ : 22;
        uint8_t v : 1;
        uint8_t c : 1;
        uint8_t z : 1;
        uint8_t n : 1;
    };
    struct {
        uint32_t __ : 28;
        uint32_t nzcv : 4;
    };
} psr_t;

typedef struct {
    mmu_t *mmu;
    union {
        uint32_t regs[16];
        struct {
            uint32_t _[13];
            uint32_t sp;
            uint32_t lr;
            uint32_t pc;
        };
    };
    psr_t cpsr;
    struct {
        psr_t abt;
        psr_t und;
    } spsr;
    cp15_t cp15;
} cpu_t;

static inline cpu_mode_t get_mode(cpu_t *cpu) {
    return cpu->cpsr.m;
}

static inline uint32_t get_reg(const cpu_t *cpu, int reg) {
    assert(reg <= 15);
    return cpu->regs[reg];
}

static inline void set_reg(cpu_t *cpu, int reg, uint32_t value) {
    assert(reg <= 15);
    cpu->regs[reg] = value;
}

/**
 * NOTE: Only cp5 and cp6 are supported
 */
static inline uint32_t get_cp15(cpu_t *cpu, int reg) {
    assert(reg <= 15);
    return cpu->cp15.regs[reg];
}

static inline void bxlr(cpu_t *cpu) {
    cpu->pc = cpu->lr;
    cpu->cpsr.t = cpu->pc & 0x01;
    cpu->pc &= 0xfffffffe;
}

void cpu_init(cpu_t *cpu, mmu_t *mmu);
void cpu_step(cpu_t *cpu);

#endif // ARMV4T_CPU_H
