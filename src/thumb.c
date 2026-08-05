#include "cpu.h"
#include <stdint.h>

// TODO: Implement
void thumb_step(armv4t_cpu *cpu) {
    take_undefined_exception(cpu, cpu->regs[REG_PC] + 2);
}
