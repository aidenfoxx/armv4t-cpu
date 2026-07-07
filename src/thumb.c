#include "cpu.h"
#include <stdint.h>

// TODO: Implement
void thumb_step(armv4t_cpu *cpu, uint32_t inst) {
   (void)inst;
    raise_undefined(cpu);
}
