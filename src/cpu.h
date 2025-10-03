#ifndef CPU_H
#define CPU_H

#include "atmv4t/cpu.h"

void arm_step(cpu_t *cpu);
void thumb_step(cpu_t *cpu);

bool has_cond(cpu_t *cpu, int cond);

#endif // CPU_H
