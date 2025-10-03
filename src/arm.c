#include "bitutils.h"
#include "cpu.h"

#include <stddef.h>
#include <stdint.h>

#define BRANCH_EX_MASK 0x0ffffff0
#define BRANCH_EX_VALUE 0x012fff10
#define BRANCH_MASK 0x0e000000
#define BRANCH_VALUE 0x0a000000
#define DATA_REG_MASK 0x0e000010
#define DATA_REG_VALUE 0x00000000
#define DATA_RSR_MASK 0x0e000090
#define DATA_RSR_VALUE 0x00000010
#define DATA_IMM_MASK 0x0e000000
#define DATA_IMM_VALUE 0x02000000
#define PSR_REG_MASK 0x0ffe0ff0
#define PSR_REG_VALUE 0x01080000
#define PSR_IMM_MASK 0x0ffff000
#define PSR_IMM_VALUE 0x0328f000
#define MUL_MASK 0x0fc000f0
#define MUL_VALUE 0x00000090
#define MUL_LONG_MASK 0x0f8000f0
#define MUL_LONG_VALUE 0x00800090
#define SINGLE_XFER_REG_MASK 0x0e000000
#define SINGLE_XFER_REG_VALUE 0x06000000
#define SINGLE_XFER_IMM_MASK 0x0e000000
#define SINGLE_XFER_IMM_VALUE 0x04000000
#define HALFWORD_XFER_REG_MASK 0x0e400f90
#define HALFWORD_XFER_REG_VALUE 0x00000090
#define HALFWORD_XFER_IMM_MASK 0x0e400090
#define HALFWORD_XFER_IMM_VALUE 0x00400090
#define BLOCK_XFER_MASK 0x0e400000
#define BLOCK_XFER_VALUE 0x08000000
#define SWAP_MASK 0x0fb00ff0
#define SWAP_VALUE 0x01000090

#define OP_TST_MASK 0x0c
#define OP_TST_VALUE 0x08

enum {
    OP_AND,
    OP_EOR,
    OP_SUB,
    OP_RSB,
    OP_ADD,
    OP_ADC,
    OP_SBC,
    OP_RSC,
    OP_TST,
    OP_TEQ,
    OP_CMP,
    OP_CMN,
    OP_ORR,
    OP_MOV,
    OP_BIC,
    OP_MVN
};

enum {
    SHIFT_LSL,
    SHIFT_LSR,
    SHIFT_ASR,
    SHIFT_ROR,
    SHIFT_RRX,
};

static uint32_t add_with_carry(uint32_t a, uint32_t b, bool carry_in, bool *carry_out,
                               bool *overflow) {
    uint64_t unsigned_sum = (uint64_t)a + b + carry_in;
    int64_t signed_sum = (int64_t)(int32_t)a + (int64_t)(int32_t)b + carry_in;
    uint32_t result = unsigned_sum;

    *carry_out = result != unsigned_sum;
    *overflow = (int64_t)(int32_t)result != signed_sum;

    return result;
}

static uint32_t shift_c(uint32_t value, int type, int count, bool carry_in, bool *carry_out) {
    if (!count) {
        *carry_out = carry_in;
        return value;
    }

    switch (type) {
    case SHIFT_LSL:
        *carry_out = get_bit(value, 32 - count);
        return value << count;
    case SHIFT_LSR:
        *carry_out = get_bit(value, count - 1);
        return value >> count;
    case SHIFT_ASR:
        *carry_out = get_bit(value, count - 1);
        bool negative = get_bit(value, 31);
        return (negative ? (int64_t)(int32_t)value : value) >> count;
    case SHIFT_ROR:
        *carry_out = get_bit(value, count - 1);
        return ror32(value, count);
    case SHIFT_RRX:
        *carry_out = get_bit(value, 0);
        return (carry_in << 31) | (value >> 1);
    default:
        unreachable();
    }
}

static uint32_t shift(uint32_t value, int type, int count, bool carry_in) {
    bool _carry_out;
    return shift_c(value, type, count, carry_in, &_carry_out);
}

static int decode_reg_shift(int type) {
    switch (type) {
    case 0:
        return SHIFT_LSL;
    case 1:
        return SHIFT_LSR;
    case 2:
        return SHIFT_ASR;
    case 3:
        return SHIFT_ROR;
    default:
        unreachable();
    }
}

static int decode_imm_shift(int type, int imm5, int *shift_n) {
    switch (type) {
    case 0:
        *shift_n = imm5;
        return SHIFT_LSL;
    case 1:
        *shift_n = imm5 ? imm5 : 32;
        return SHIFT_LSR;
    case 2:
        *shift_n = imm5 ? imm5 : 32;
        return SHIFT_ASR;
    case 3:
        if (imm5) {
            *shift_n = imm5;
            return SHIFT_ROR;
        } else {
            *shift_n = 1;
            return SHIFT_RRX;
        }
    default:
        unreachable();
    }
}

static uint32_t expand_imm_c(int imm12, bool carry_in, bool *carry_out) {
    int unrotated_value = get_bits(imm12, 0, 8);
    int count = 2 * get_bits(imm12, 8, 4);
    return shift_c(unrotated_value, SHIFT_ROR, count, carry_in, carry_out);
}

static uint32_t expand_imm(int imm12, bool carry_in) {
    bool _carry_out;
    return expand_imm_c(imm12, carry_in, &_carry_out);
}

static uint32_t data_op(int op, uint32_t a, uint32_t b, bool carry_in, bool *carry_out,
                        bool *overflow) {
    switch (op) {
    case OP_AND:
    case OP_TST:
        return a & b;
    case OP_EOR:
    case OP_TEQ:
        return a ^ b;
    case OP_SUB:
    case OP_CMP:
        return add_with_carry(a, ~b, 1, carry_out, overflow);
    case OP_RSB:
        return add_with_carry(~a, b, 1, carry_out, overflow);
    case OP_ADD:
    case OP_CMN:
        return add_with_carry(a, b, 0, carry_out, overflow);
    case OP_ADC:
        return add_with_carry(a, b, carry_in, carry_out, overflow);
    case OP_SBC:
        return add_with_carry(a, ~b, carry_in, carry_out, overflow);
    case OP_RSC:
        return add_with_carry(~a, b, carry_in, carry_out, overflow);
    case OP_ORR:
        return a | b;
    case OP_MOV:
        return b;
    case OP_BIC:
        return a & ~b;
    case OP_MVN:
        return ~b;
    default:
        unreachable();
    }
}

static void branch_ex_inst(cpu_t *cpu, uint32_t inst) {
    int rn = get_bits(inst, 0, 4);
    cpu->pc = cpu->regs[rn];
}

static void branch_inst(cpu_t *cpu, uint32_t inst) {
    bool l = get_bit(inst, 24);
    int offset = get_bits(inst, 0, 24);
    bool negative = get_bit(offset, 23);

    uint32_t offset_s = negative ? (offset | 0xff000000) << 2 : (uint32_t)offset << 2;

    if (l) { // BL
        cpu->lr = cpu->pc - 4;
    }

    cpu->pc += offset_s;
}

static void data_reg_inst(cpu_t *cpu, uint32_t inst, bool *branch) {
    int opcode = get_bits(inst, 21, 4);
    bool s = get_bit(inst, 20);
    int rn = get_bits(inst, 16, 4);
    int rd = get_bits(inst, 12, 4);
    int rm = get_bits(inst, 0, 4);
    int imm5 = get_bits(inst, 7, 5);
    int type = get_bits(inst, 5, 2);

    int shift_n;
    int shift_t = decode_imm_shift(type, imm5, &shift_n);

    bool carry = false;
    bool overflow = false;
    uint32_t shifted = shift_c(cpu->regs[rm], shift_t, shift_n, cpu->cpsr.c, &carry);
    uint32_t result = data_op(opcode, cpu->regs[rn], shifted, cpu->cpsr.c, &carry, &overflow);

    if ((opcode & OP_TST_MASK) != OP_TST_VALUE) {
        cpu->regs[rd] = result;
        *branch = rd == REG_PC;
    }

    if (s && rd != REG_PC) {
        cpu->cpsr.n = get_bit(result, 31);
        cpu->cpsr.z = !result;
        cpu->cpsr.c = carry;
        cpu->cpsr.v = overflow;
    }
}

static void data_rsr_inst(cpu_t *cpu, uint32_t inst) {
    int opcode = get_bits(inst, 21, 4);
    bool s = get_bit(inst, 20);
    int rn = get_bits(inst, 16, 4);
    int rs = get_bits(inst, 8, 4);
    int rm = get_bits(inst, 0, 4);
    int type = get_bits(inst, 5, 2);

    int shift_t = decode_reg_shift(type);
    int shift_n = get_bits(cpu->regs[rs], 0, 8);

    bool carry = false;
    bool overflow = false;
    uint32_t shifted = shift_c(cpu->regs[rm], shift_t, shift_n, cpu->cpsr.c, &carry);
    uint32_t result = data_op(opcode, cpu->regs[rn], shifted, cpu->cpsr.c, &carry, &overflow);

    if ((opcode & OP_TST_MASK) != OP_TST_VALUE) {
        int rd = get_bits(inst, 12, 4);
        cpu->regs[rd] = result;
    }

    if (s) {
        cpu->cpsr.n = get_bit(result, 31);
        cpu->cpsr.z = !result;
        cpu->cpsr.c = carry;
        cpu->cpsr.v = overflow;
    }
}

static void data_imm_inst(cpu_t *cpu, uint32_t inst, bool *branch) {
    int opcode = get_bits(inst, 21, 4);
    bool s = get_bit(inst, 20);
    int rn = get_bits(inst, 16, 4);
    int rd = get_bits(inst, 12, 4);
    int imm12 = get_bits(inst, 0, 12);

    bool carry = false;
    bool overflow = false;
    uint32_t imm32 = expand_imm_c(imm12, cpu->cpsr.c, &carry);
    uint32_t result = data_op(opcode, cpu->regs[rn], imm32, cpu->cpsr.c, &carry, &overflow);

    if ((opcode & OP_TST_MASK) != OP_TST_VALUE) {
        cpu->regs[rd] = result;
        *branch = rd == REG_PC;
    }

    if (s && rd != REG_PC) {
        cpu->cpsr.n = get_bit(result, 31);
        cpu->cpsr.z = !result;
        cpu->cpsr.c = carry;
        cpu->cpsr.v = overflow;
    }
}

static void psr_reg_inst(cpu_t *cpu, uint32_t inst) {
    bool s = get_bit(inst, 21);
    if (s) {
        int rm = get_bits(inst, 0, 4);
        cpu->cpsr.nzcv = get_bits(cpu->regs[rm], 27, 4);
    } else {
        int rd = get_bits(inst, 12, 4);
        cpu->regs[rd] = cpu->cpsr.value;
    }
}

static void psr_imm_inst(cpu_t *cpu, uint32_t inst) {
    int imm12 = get_bits(inst, 0, 12);
    uint32_t imm32 = expand_imm(imm12, cpu->cpsr.c);
    cpu->cpsr.nzcv = get_bits(imm32, 27, 4);
}

static void mul_inst(cpu_t *cpu, uint32_t inst) {
    bool a = get_bit(inst, 21);
    bool s = get_bit(inst, 20);
    int rd = get_bits(inst, 16, 4);
    int rs = get_bits(inst, 8, 4);
    int rm = get_bits(inst, 0, 4);

    uint32_t result = cpu->regs[rm] * cpu->regs[rs];
    if (a) {
        int rn = get_bits(inst, 12, 4);
        result += cpu->regs[rn];
    }

    cpu->regs[rd] = result;

    if (s) {
        cpu->cpsr.n = get_bit(result, 31);
        cpu->cpsr.z = !result;
    }
}

static void mul_long_inst(cpu_t *cpu, uint32_t inst) {
    bool a = get_bit(inst, 21);
    bool s = get_bit(inst, 20);
    int rd_hi = get_bits(inst, 16, 4);
    int rd_lo = get_bits(inst, 12, 4);
    int rs = get_bits(inst, 8, 4);
    int rm = get_bits(inst, 0, 4);

    uint64_t result = (uint64_t)cpu->regs[rm] * cpu->regs[rs];
    if (a) {
        result += ((uint64_t)cpu->regs[rd_hi] << 32) + cpu->regs[rd_lo];
    }

    cpu->regs[rd_hi] = result >> 32;
    cpu->regs[rd_lo] = result;

    if (s) {
        cpu->cpsr.n = get_bit(result, 31);
        cpu->cpsr.z = !result;
    }
}

static bool single_xfer_reg_inst(cpu_t *cpu, uint32_t inst, bool *branch) {
    bool p = get_bit(inst, 24);
    bool u = get_bit(inst, 23);
    bool b = get_bit(inst, 22);
    bool w = get_bit(inst, 21);
    bool l = get_bit(inst, 20);
    int rn = get_bits(inst, 16, 4);
    int rd = get_bits(inst, 12, 4);
    int rm = get_bits(inst, 0, 4);
    int imm5 = get_bits(inst, 7, 5);
    int type = get_bits(inst, 4, 2);

    int shift_n;
    int shift_t = decode_imm_shift(type, imm5, &shift_n);

    uint32_t offset = shift(cpu->regs[rm], shift_t, shift_n, cpu->cpsr.c);
    uint32_t offset_addr = cpu->regs[rn] + (u ? offset : -offset);
    uint32_t addr = p ? offset_addr : cpu->regs[rn];

    mmu_fault_t fault;
    if (l) {
        if (b) {
            fault = ld_8(cpu->mmu, addr, (uint8_t *)&cpu->regs[rd]);
        } else {
            fault = ld_32(cpu->mmu, addr, &cpu->regs[rd]);
        }
        *branch = rd == REG_PC;
    } else {
        if (b) {
            fault = st_8(cpu->mmu, addr, cpu->regs[rd]);
        } else {
            fault = st_32(cpu->mmu, addr, cpu->regs[rd]);
        }
    }

    if (!p || w) {
        cpu->regs[rn] = offset_addr;
    }

    return !fault;
}

static bool single_xfer_imm_inst(cpu_t *cpu, uint32_t inst, bool *branch) {
    bool p = get_bit(inst, 24);
    bool u = get_bit(inst, 23);
    bool b = get_bit(inst, 22);
    bool w = get_bit(inst, 21);
    bool l = get_bit(inst, 20);
    int rn = get_bits(inst, 16, 4);
    int rd = get_bits(inst, 12, 4);
    int offset = get_bits(inst, 0, 12);

    uint32_t offset_addr = cpu->regs[rn] + (u ? offset : -offset);
    uint32_t addr = p ? offset_addr : cpu->regs[rn];

    mmu_fault_t fault;
    if (l) {
        if (b) {
            fault = ld_8(cpu->mmu, addr, (uint8_t *)&cpu->regs[rd]);
        } else {
            fault = ld_32(cpu->mmu, addr, &cpu->regs[rd]);
        }
        *branch = rd == REG_PC;
    } else {
        if (b) {
            fault = st_8(cpu->mmu, addr, cpu->regs[rd]);
        } else {
            fault = st_32(cpu->mmu, addr, cpu->regs[rd]);
        }
    }

    if (!p || w) {
        cpu->regs[rn] = offset_addr;
    }

    return !fault;
}

static bool halfword_xfer_reg_inst(cpu_t *cpu, uint32_t inst) {
    bool p = get_bit(inst, 24);
    bool u = get_bit(inst, 23);
    bool w = get_bit(inst, 21);
    bool l = get_bit(inst, 20);
    int rn = get_bits(inst, 16, 4);
    int rd = get_bits(inst, 12, 4);
    int rm = get_bits(inst, 0, 4);

    uint32_t offset = shift(cpu->regs[rm], SHIFT_LSL, 0, cpu->cpsr.c);
    uint32_t offset_addr = cpu->regs[rn] + (u ? offset : -offset);
    uint32_t addr = p ? offset_addr : cpu->regs[rn];

    mmu_fault_t fault;
    if (l) { // Load
        int type = get_bits(inst, 5, 2);
        switch (type) {
        case 1:
            fault = ld_16(cpu->mmu, addr, (uint16_t *)&cpu->regs[rd]); // Halfword
            break;
        case 2:
            fault = ld_8(cpu->mmu, addr, (uint8_t *)&cpu->regs[rd]); // Signed byte
            break;
        case 3:
            fault = ld_32(cpu->mmu, addr, &cpu->regs[rd]); // Signed halfword
            break;
        }
    } else { // Store
        int rd = get_bits(inst, 12, 4);
        int data = get_bits(cpu->regs[rd], 0, 16);
        fault = st_16(cpu->mmu, addr, data);
    }

    if (!p || w) {
        cpu->regs[rn] = offset_addr;
    }

    return !fault;
}

static bool halfword_xfer_imm_inst(cpu_t *cpu, uint32_t inst) {
    bool p = get_bit(inst, 24);
    bool u = get_bit(inst, 23);
    bool w = get_bit(inst, 21);
    bool l = get_bit(inst, 20);
    int rn = get_bits(inst, 16, 4);
    int rd = get_bits(inst, 12, 4);
    int offset = (get_bits(inst, 8, 4) << 4) + get_bits(inst, 0, 4);

    uint32_t offset_addr = cpu->regs[rn] + (u ? offset : -offset);
    uint32_t addr = p ? offset_addr : cpu->regs[rn];

    mmu_fault_t fault;
    if (l) { // Load
        int type = get_bits(inst, 5, 2);
        switch (type) {
        case 1:
            fault = ld_16(cpu->mmu, addr, (uint16_t *)&cpu->regs[rd]); // Halfword
            break;
        case 2:
            fault = ld_8(cpu->mmu, addr, (uint8_t *)&cpu->regs[rd]); // Signed byte
            break;
        case 3:
            fault = ld_32(cpu->mmu, addr, &cpu->regs[rd]); // Signed halfword
            break;
        }
    } else { // Store
        int data = get_bits(cpu->regs[rd], 0, 16);
        fault = st_16(cpu->mmu, addr, data);
    }

    if (!p || w) {
        cpu->regs[rn] = offset_addr;
    }

    return !fault;
}

static bool block_xfer_inst(cpu_t *cpu, uint32_t inst, bool *branch) {
    bool p = get_bit(inst, 24);
    bool u = get_bit(inst, 23);
    bool w = get_bit(inst, 21);
    bool l = get_bit(inst, 20);
    int rn = get_bits(inst, 16, 4);
    int registers = get_bits(inst, 0, 16);

    int offset = popcount32(registers) * 4;
    uint32_t addr = u ? cpu->regs[rn] : cpu->regs[rn] - offset;

    if (p == u) {
        addr += 4;
    }

    mmu_fault_t fault;
    for (int i = 0; i < 16; i++, registers >>= 1) {
        if (registers & 0x01) {
            if (l) { // Load
                if ((fault = ld_32(cpu->mmu, addr, &cpu->regs[i]))) {
                    break;
                }
                *branch = i == REG_PC;
            } else { // Store
                if ((fault = st_32(cpu->mmu, addr, cpu->regs[i]))) {
                    break;
                }
            }

            addr += 4;
        }
    }

    if (w) {
        cpu->regs[rn] += u ? offset : -offset;
    }

    return !fault;
}

static bool swap_inst(cpu_t *cpu, uint32_t inst) {
    bool b = get_bit(inst, 22);
    int rn = get_bits(inst, 16, 4);
    int rd = get_bits(inst, 12, 4);
    int rm = get_bits(inst, 0, 4);

    int addr = cpu->regs[rn];

    uint32_t result;
    mmu_fault_t fault;
    if (b) {
        fault = ld_8(cpu->mmu, addr, (uint8_t *)&result);
    } else {
        fault = ld_32(cpu->mmu, addr, &result);
    }

    if (b) {
        fault = st_8(cpu->mmu, addr, cpu->regs[rm]);
    } else {
        fault = st_32(cpu->mmu, addr, cpu->regs[rm]);
    }

    cpu->regs[rd] = result;

    return !fault;
}

void arm_step(cpu_t *cpu) {
    uint32_t inst;
    ld_32(cpu->mmu, cpu->pc, &inst);

    cpu->pc += 8;

    bool branch = false;
    int cond = get_bits(inst, 28, 4);

    if (has_cond(cpu, cond)) {
        if ((inst & BRANCH_EX_MASK) == BRANCH_EX_VALUE) {
            branch_ex_inst(cpu, inst);
            branch = true;
        } else if ((inst & BRANCH_MASK) == BRANCH_VALUE) {
            branch_inst(cpu, inst);
            branch = true;
        } else if ((inst & DATA_REG_MASK) == DATA_REG_VALUE) {
            data_reg_inst(cpu, inst, &branch);
        } else if ((inst & DATA_RSR_MASK) == DATA_RSR_VALUE) {
            data_rsr_inst(cpu, inst);
        } else if ((inst & DATA_IMM_MASK) == DATA_IMM_VALUE) {
            data_imm_inst(cpu, inst, &branch);
        } else if ((inst & PSR_REG_MASK) == PSR_REG_VALUE) {
            psr_reg_inst(cpu, inst);
        } else if ((inst & PSR_IMM_MASK) == PSR_IMM_VALUE) {
            psr_imm_inst(cpu, inst);
        } else if ((inst & MUL_MASK) == MUL_VALUE) {
            mul_inst(cpu, inst);
        } else if ((inst & MUL_LONG_MASK) == MUL_LONG_VALUE) {
            mul_long_inst(cpu, inst);
        } else if ((inst & SINGLE_XFER_REG_MASK) == SINGLE_XFER_REG_VALUE) {
            single_xfer_reg_inst(cpu, inst, &branch);
        } else if ((inst & SINGLE_XFER_IMM_MASK) == SINGLE_XFER_IMM_VALUE) {
            single_xfer_imm_inst(cpu, inst, &branch);
        } else if ((inst & HALFWORD_XFER_REG_MASK) == HALFWORD_XFER_REG_VALUE) {
            halfword_xfer_reg_inst(cpu, inst);
        } else if ((inst & HALFWORD_XFER_IMM_MASK) == HALFWORD_XFER_IMM_VALUE) {
            halfword_xfer_imm_inst(cpu, inst);
        } else if ((inst & BLOCK_XFER_MASK) == BLOCK_XFER_VALUE) {
            block_xfer_inst(cpu, inst, &branch);
        } else if ((inst & SWAP_MASK) == SWAP_VALUE) {
            swap_inst(cpu, inst);
        } // else invalid instruction
    }

    if (branch) {
        cpu->cpsr.t = cpu->pc & 0x01;
        cpu->pc &= 0xfffffffe;
    } else {
        cpu->pc -= 4;
    }
}
