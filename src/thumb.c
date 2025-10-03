#include "cpu.h"

#include <stdint.h>
#include <stdbool.h>

void thumb_step(cpu_t *cpu) {
    // TODO: Implement
    cpu->cpsr.t = false;
}
