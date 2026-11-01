#include "cpu.h"
#include <stdint.h>

// TODO: Implement
exec_state thumb_exec(armv4t_cpu *cpu, uint16_t inst, uint32_t pc) {
    (void)inst;
    take_undefined_exception(cpu, pc + 2);
    return STATE_BRANCH | STATE_EXCEPTION;
}
