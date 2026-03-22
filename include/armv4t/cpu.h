#ifndef ARMV4T_CPU_H
#define ARMV4T_CPU_H

#include <stdbool.h>
#include <stdint.h>

#define REG_SP 13
#define REG_LR 14
#define REG_PC 15

typedef enum {
    MODE_USR = 0x10,
    MODE_FIQ,
    MODE_IRQ,
    MODE_SVC,
    MODE_ABT = 0x17,
    MODE_UND = 0x1b,
    MODE_SYS = 0x1f,
} armv4t_mode;

typedef enum {
    FLAG_THUMB = 5,
    FLAG_FIQ,
    FLAG_IRQ,
    FLAG_V = 28,
    FLAG_C,
    FLAG_Z,
    FLAG_N,
} armv4t_flag;

typedef uint32_t armv4t_psr;

typedef struct {
    /* public */
    uint32_t regs[16];
    uint32_t cp15[15];
    armv4t_psr cpsr;
    /* private */
    struct armv4t_mmu *_mmu;
    struct armv4t_internal *_internal;
} armv4t_cpu;

static inline armv4t_mode armv4t_get_mode(const armv4t_cpu *cpu) {
    return (armv4t_mode)(cpu->cpsr & 0x1f);
}

static inline void armv4t_set_mode(armv4t_cpu *cpu, armv4t_mode mode) {
    cpu->cpsr = (cpu->cpsr & ~0x1F) | mode;
}

static inline bool armv4t_get_flag(const armv4t_cpu *cpu, armv4t_flag flag) {
    return (cpu->cpsr >> flag) & 1;
}

static inline void armv4t_set_flag(armv4t_cpu *cpu, armv4t_flag flag, bool value) {
    cpu->cpsr = value ? cpu->cpsr | (1u << flag) : cpu->cpsr & ~(1u << flag);
}

static inline void armv4t_bx(armv4t_cpu *cpu, uint32_t addr) {
    bool thumb = addr & 1;
    armv4t_set_flag(cpu, FLAG_THUMB, thumb);
    cpu->regs[REG_PC] = thumb ? addr & ~1u : addr & ~3u;
}

static inline void armv4t_blx(armv4t_cpu *cpu, uint32_t addr) {
    cpu->regs[REG_LR] = cpu->regs[REG_PC] | armv4t_get_flag(cpu, FLAG_THUMB);
    armv4t_bx(cpu, addr);
}

static inline void armv4t_bxlr(armv4t_cpu *cpu) {
    armv4t_bx(cpu, cpu->regs[REG_LR]);
}

bool armv4t_cpu_init(armv4t_cpu **cpu, struct armv4t_mmu *mmu);
void armv4t_cpu_destroy(armv4t_cpu *cpu);

void armv4t_step(armv4t_cpu *cpu);
void armv4t_reset(armv4t_cpu *cpu);

#endif // ARMV4T_CPU_H
