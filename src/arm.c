#include "armv4t/cpu.h"
#include "armv4t/mmu.h"
#include "cpu.h"
#include "utils.h"

#include <stdint.h>

#define BRANCH_EX_MASK 0x0FFFFFF0
#define BRANCH_EX_VALUE 0x012FFF10
#define BRANCH_MASK 0x0E000000
#define BRANCH_VALUE 0x0A000000
#define DATA_SHIFT_IMM_MASK 0x0E000010
#define DATA_SHIFT_IMM_VALUE 0x00000000
#define DATA_SHIFT_REG_MASK 0x0E000090
#define DATA_SHIFT_REG_VALUE 0x00000010
#define DATA_IMM_MASK 0x0E000000
#define DATA_IMM_VALUE 0x02000000
#define MRS_MASK 0x0FB00FF0
#define MRS_VALUE 0x01000000
#define MSR_REG_MASK 0x0FB00FF0
#define MSR_REG_VALUE 0x01200000
#define MSR_IMM_MASK 0x0FB0F000
#define MSR_IMM_VALUE 0x0320F000
#define MUL_MASK 0x0FC000F0
#define MUL_VALUE 0x00000090
#define MUL_LONG_MASK 0x0F8000F0
#define MUL_LONG_VALUE 0x00800090
#define SINGLE_XFER_REG_MASK 0x0E000010
#define SINGLE_XFER_REG_VALUE 0x06000000
#define SINGLE_XFER_IMM_MASK 0x0E000000
#define SINGLE_XFER_IMM_VALUE 0x04000000
#define HALFWORD_XFER_REG_MASK 0x0E400F90
#define HALFWORD_XFER_REG_VALUE 0x00000090
#define HALFWORD_XFER_IMM_MASK 0x0E400090
#define HALFWORD_XFER_IMM_VALUE 0x00400090
#define BLOCK_XFER_MASK 0x0E000000
#define BLOCK_XFER_VALUE 0x08000000
#define SWAP_MASK 0x0FB00FF0
#define SWAP_VALUE 0x01000090
#define SWI_MASK 0x0F000000
#define SWI_VALUE 0x0F000000

enum {
    OP_AND = 0,
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
    SHIFT_LSL = 0,
    SHIFT_LSR,
    SHIFT_ASR,
    SHIFT_ROR,
    SHIFT_RRX,
};

/**
 * A register read helper for operations which expect PC + #8 due to prefetching.
 */
static uint32_t reg_pc8(const armv4t_cpu *cpu, uint32_t reg, uint32_t pc) {
    return reg == REG_PC ? pc + 8 : cpu->regs[reg];
}

/**
 * A register read helper for operations which expect PC + #12 due to prefetching.
 * https://problemkaputt.de/gbatek-arm-opcodes-data-processing-alu.htm
 * https://problemkaputt.de/gbatek-arm-opcodes-memory-single-data-transfer-ldr-str-pld.htm
 * https://problemkaputt.de/gbatek-arm-opcodes-memory-halfword-doubleword-and-signed-data-transfer.htm
 * https://problemkaputt.de/gbatek-arm-opcodes-memory-block-data-transfer-ldm-stm.htm
 */
static uint32_t reg_pc12(const armv4t_cpu *cpu, uint32_t reg, uint32_t pc) {
    return reg == REG_PC ? pc + 12 : cpu->regs[reg];
}

static uint32_t add_with_carry(uint32_t a, uint32_t b, bool carry_in, bool *carry_out,
                               bool *overflow) {
    uint64_t unsigned_sum = (uint64_t)a + b + carry_in;
    int64_t signed_sum = (int64_t)(int32_t)a + (int64_t)(int32_t)b + carry_in;
    uint32_t result = unsigned_sum;

    *carry_out = unsigned_sum >> 32;
    *overflow = signed_sum != (int64_t)(int32_t)result;
    return result;
}

static uint32_t lsl_c(uint32_t value, uint32_t shift, bool *carry_out) {
    if (shift >= 32) {
        *carry_out = shift == 32 && get_bit(value, 0);
        return 0;
    }

    *carry_out = get_bit(value, 32 - shift);
    return value << shift;
}

static uint32_t lsr_c(uint32_t value, uint32_t shift, bool *carry_out) {
    if (shift >= 32) {
        *carry_out = shift == 32 && get_bit(value, 31);
        return 0;
    }

    *carry_out = get_bit(value, shift - 1);
    return value >> shift;
}

static uint32_t asr_c(uint32_t value, uint32_t shift, bool *carry_out) {
    if (shift > 32) {
        shift = 32;
    }

    *carry_out = get_bit(value, shift - 1);
    return (int64_t)(int32_t)value >> shift;
}

static uint32_t ror_c(uint32_t value, uint32_t shift, bool *carry_out) {
    uint32_t result = (value >> (shift & 31)) | (value << (-shift & 31));
    *carry_out = get_bit(result, 31);
    return result;
}

static uint32_t rrx_c(uint32_t value, bool carry_in, bool *carry_out) {
    *carry_out = get_bit(value, 0);
    return ((uint32_t)carry_in << 31) | (value >> 1);
}

static uint32_t shift_c(uint32_t value, uint32_t type, uint32_t amount, bool carry_in,
                        bool *carry_out) {
    if (amount == 0) {
        *carry_out = carry_in;
        return value;
    }

    switch (type) {
    case SHIFT_LSL:
        return lsl_c(value, amount, carry_out);
    case SHIFT_LSR:
        return lsr_c(value, amount, carry_out);
    case SHIFT_ASR:
        return asr_c(value, amount, carry_out);
    case SHIFT_ROR:
        return ror_c(value, amount, carry_out);
    case SHIFT_RRX:
        return rrx_c(value, carry_in, carry_out);
    default:
        unreachable();
    }
}

static uint32_t shift(uint32_t value, uint32_t type, uint32_t amount, bool carry_in) {
    bool _carry_out;
    return shift_c(value, type, amount, carry_in, &_carry_out);
}

static uint32_t decode_reg_shift(uint32_t type) {
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

static uint32_t decode_imm_shift(uint32_t type, uint32_t imm5, uint32_t *shift_n) {
    switch (type) {
    case 0:
        *shift_n = imm5;
        return SHIFT_LSL;
    case 1:
        *shift_n = imm5 != 0 ? imm5 : 32;
        return SHIFT_LSR;
    case 2:
        *shift_n = imm5 != 0 ? imm5 : 32;
        return SHIFT_ASR;
    case 3:
        if (imm5 != 0) {
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

static uint32_t expand_imm_c(uint32_t imm12, bool carry_in, bool *carry_out) {
    uint32_t unrotated_value = get_bits(imm12, 0, 8);
    uint32_t count = 2 * get_bits(imm12, 8, 4);
    return shift_c(unrotated_value, SHIFT_ROR, count, carry_in, carry_out);
}

static uint32_t expand_imm(uint32_t imm12, bool carry_in) {
    bool _carry_out;
    return expand_imm_c(imm12, carry_in, &_carry_out);
}

static uint32_t data_op(uint32_t op, uint32_t a, uint32_t b, bool carry_in, bool *carry_out,
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

static uint32_t expand_msr_mask(uint32_t mask) {
    return (get_bit(mask, 0) ? 0xFF : 0) | (get_bit(mask, 3) ? 0xFF000000 : 0);
}

static void branch_ex_inst(armv4t_cpu *cpu, uint32_t inst) {
    uint32_t rn = get_bits(inst, 0, 4);
    armv4t_bx(cpu, cpu->regs[rn]);
}

static void branch_inst(armv4t_cpu *cpu, uint32_t inst, uint32_t pc) {
    bool l = get_bit(inst, 24);
    uint32_t offset = get_bits(inst, 0, 24);
    bool negative = get_bit(offset, 23);

    uint32_t offset_s = negative ? (offset | 0xFF000000) * 4 : offset * 4;

    if (l) { // BL
        cpu->regs[REG_LR] = pc + 4;
    }

    armv4t_branch(cpu, pc + 8 + offset_s);
}

static void data_shift_imm_inst(armv4t_cpu *cpu, uint32_t inst, uint32_t pc) {
    uint32_t opcode = get_bits(inst, 21, 4);
    bool s = get_bit(inst, 20);
    uint32_t rn = get_bits(inst, 16, 4);
    uint32_t rd = get_bits(inst, 12, 4);
    uint32_t rm = get_bits(inst, 0, 4);
    uint32_t imm5 = get_bits(inst, 7, 5);
    uint32_t type = get_bits(inst, 5, 2);

    uint32_t shift_n;
    uint32_t shift_t = decode_imm_shift(type, imm5, &shift_n);

    bool carry = false;
    bool overflow = armv4t_get_flag(cpu, FLAG_V);
    bool carry_in = armv4t_get_flag(cpu, FLAG_C);

    uint32_t op2 = shift_c(reg_pc8(cpu, rm, pc), shift_t, shift_n, carry_in, &carry);
    uint32_t result = data_op(opcode, reg_pc8(cpu, rn, pc), op2, carry_in, &carry, &overflow);

    if (opcode < OP_TST || opcode > OP_CMN) {
        cpu->regs[rd] = result;
    }

    if (rd == REG_PC) {
        if (s) {
            restore_spsr(cpu);
        }

        armv4t_branch(cpu, cpu->regs[REG_PC]);
    } else if (s) {
        armv4t_set_flag(cpu, FLAG_N, get_bit(result, 31));
        armv4t_set_flag(cpu, FLAG_Z, result == 0);
        armv4t_set_flag(cpu, FLAG_C, carry);
        armv4t_set_flag(cpu, FLAG_V, overflow);
    }
}

static void data_shift_reg_inst(armv4t_cpu *cpu, uint32_t inst, uint32_t pc) {
    uint32_t opcode = get_bits(inst, 21, 4);
    bool s = get_bit(inst, 20);
    uint32_t rn = get_bits(inst, 16, 4);
    uint32_t rd = get_bits(inst, 12, 4);
    uint32_t rs = get_bits(inst, 8, 4);
    uint32_t rm = get_bits(inst, 0, 4);
    uint32_t type = get_bits(inst, 5, 2);

    uint32_t shift_t = decode_reg_shift(type);
    uint32_t shift_n = get_bits(cpu->regs[rs], 0, 8);

    bool carry = false;
    bool overflow = armv4t_get_flag(cpu, FLAG_V);
    bool carry_in = armv4t_get_flag(cpu, FLAG_C);

    uint32_t shifted = shift_c(reg_pc12(cpu, rm, pc), shift_t, shift_n, carry_in, &carry);
    uint32_t result = data_op(opcode, reg_pc12(cpu, rn, pc), shifted, carry_in, &carry, &overflow);

    if (opcode < OP_TST || opcode > OP_CMN) {
        cpu->regs[rd] = result;
    }

    if (rd == REG_PC) {
        if (s) {
            restore_spsr(cpu);
        }

        armv4t_branch(cpu, cpu->regs[REG_PC]);
    } else if (s) {
        armv4t_set_flag(cpu, FLAG_N, get_bit(result, 31));
        armv4t_set_flag(cpu, FLAG_Z, result == 0);
        armv4t_set_flag(cpu, FLAG_C, carry);
        armv4t_set_flag(cpu, FLAG_V, overflow);
    }
}

static void data_imm_inst(armv4t_cpu *cpu, uint32_t inst, uint32_t pc) {
    uint32_t opcode = get_bits(inst, 21, 4);
    bool s = get_bit(inst, 20);
    uint32_t rn = get_bits(inst, 16, 4);
    uint32_t rd = get_bits(inst, 12, 4);
    uint32_t imm12 = get_bits(inst, 0, 12);

    bool carry = false;
    bool overflow = armv4t_get_flag(cpu, FLAG_V);
    bool carry_in = armv4t_get_flag(cpu, FLAG_C);

    uint32_t imm32 = expand_imm_c(imm12, carry_in, &carry);
    uint32_t result = data_op(opcode, reg_pc8(cpu, rn, pc), imm32, carry_in, &carry, &overflow);

    if (opcode < OP_TST || opcode > OP_CMN) {
        cpu->regs[rd] = result;
    }

    if (rd == REG_PC) {
        if (s) {
            restore_spsr(cpu);
        }

        armv4t_branch(cpu, cpu->regs[REG_PC]);
    } else if (s) {
        armv4t_set_flag(cpu, FLAG_N, get_bit(result, 31));
        armv4t_set_flag(cpu, FLAG_Z, result == 0);
        armv4t_set_flag(cpu, FLAG_C, carry);
        armv4t_set_flag(cpu, FLAG_V, overflow);
    }
}

static void mrs_inst(armv4t_cpu *cpu, uint32_t inst) {
    bool p = get_bit(inst, 22);
    uint32_t rd = get_bits(inst, 12, 4);
    cpu->regs[rd] = p ? get_spsr(cpu) : cpu->cpsr;
}

static void msr_reg_inst(armv4t_cpu *cpu, uint32_t inst) {
    bool p = get_bit(inst, 22);
    uint32_t field_mask = get_bits(inst, 16, 4);
    uint32_t rm = get_bits(inst, 0, 4);

    uint32_t mask = expand_msr_mask(field_mask);

    if (p) {
        armv4t_psr spsr = get_spsr(cpu);
        set_spsr(cpu, (spsr & ~mask) | (cpu->regs[rm] & mask));
    } else {
        if (armv4t_get_mode(cpu) == MODE_USR) {
            mask &= 0xFF000000;
        }

        armv4t_psr result = (cpu->cpsr & ~mask) | (cpu->regs[rm] & mask);
        armv4t_set_mode(cpu, result & 0x1F);
        cpu->cpsr = result;
    }
}

static void msr_imm_inst(armv4t_cpu *cpu, uint32_t inst) {
    bool p = get_bit(inst, 22);
    uint32_t field_mask = get_bits(inst, 16, 4);
    uint32_t imm12 = get_bits(inst, 0, 12);

    uint32_t imm32 = expand_imm(imm12, armv4t_get_flag(cpu, FLAG_C));
    uint32_t mask = expand_msr_mask(field_mask);

    if (p) {
        armv4t_psr spsr = get_spsr(cpu);
        set_spsr(cpu, (spsr & ~mask) | (imm32 & mask));
    } else {
        if (armv4t_get_mode(cpu) == MODE_USR) {
            mask &= 0xFF000000;
        }

        armv4t_psr result = (cpu->cpsr & ~mask) | (imm32 & mask);
        armv4t_set_mode(cpu, result & 0x1F);
        cpu->cpsr = result;
    }
}

static void mul_inst(armv4t_cpu *cpu, uint32_t inst) {
    bool a = get_bit(inst, 21);
    bool s = get_bit(inst, 20);
    uint32_t rd = get_bits(inst, 16, 4);
    uint32_t rs = get_bits(inst, 8, 4);
    uint32_t rm = get_bits(inst, 0, 4);

    uint32_t result = cpu->regs[rm] * cpu->regs[rs];
    if (a) {
        uint32_t rn = get_bits(inst, 12, 4);
        result += cpu->regs[rn];
    }

    cpu->regs[rd] = result;

    if (s) {
        armv4t_set_flag(cpu, FLAG_N, get_bit(result, 31));
        armv4t_set_flag(cpu, FLAG_Z, result == 0);
    }
}

static void mul_long_inst(armv4t_cpu *cpu, uint32_t inst) {
    bool u = get_bit(inst, 22);
    bool a = get_bit(inst, 21);
    bool s = get_bit(inst, 20);
    uint32_t rd_hi = get_bits(inst, 16, 4);
    uint32_t rd_lo = get_bits(inst, 12, 4);
    uint32_t rs = get_bits(inst, 8, 4);
    uint32_t rm = get_bits(inst, 0, 4);

    uint64_t result;
    if (u) {
        result = (int64_t)(int32_t)cpu->regs[rm] * (int32_t)cpu->regs[rs];
    } else {
        result = (uint64_t)cpu->regs[rm] * cpu->regs[rs];
    }

    if (a) {
        result += ((uint64_t)cpu->regs[rd_hi] << 32) + cpu->regs[rd_lo];
    }

    cpu->regs[rd_hi] = result >> 32;
    cpu->regs[rd_lo] = result;

    if (s) {
        armv4t_set_flag(cpu, FLAG_N, result >> 63);
        armv4t_set_flag(cpu, FLAG_Z, result == 0);
    }
}

static void single_xfer_reg_inst(armv4t_cpu *cpu, uint32_t inst, uint32_t pc) {
    bool p = get_bit(inst, 24);
    bool u = get_bit(inst, 23);
    bool b = get_bit(inst, 22);
    bool w = get_bit(inst, 21);
    bool l = get_bit(inst, 20);
    uint32_t rn = get_bits(inst, 16, 4);
    uint32_t rd = get_bits(inst, 12, 4);
    uint32_t rm = get_bits(inst, 0, 4);
    uint32_t imm5 = get_bits(inst, 7, 5);
    uint32_t type = get_bits(inst, 5, 2);

    uint32_t shift_n;
    uint32_t shift_t = decode_imm_shift(type, imm5, &shift_n);

    bool carry_in = armv4t_get_flag(cpu, FLAG_C);
    uint32_t offset = shift(cpu->regs[rm], shift_t, shift_n, carry_in);
    uint32_t offset_addr = reg_pc8(cpu, rn, pc) + (u ? offset : -offset);
    uint32_t addr = p ? offset_addr : reg_pc8(cpu, rn, pc);

    armv4t_fsr fsr;
    if (l) {
        if (b) {
            uint8_t value;
            if ((fsr = armv4t_ld_8(cpu->_mmu, addr, &value)) == 0) {
                cpu->regs[rd] = value;
            }
        } else {
            fsr = armv4t_ld_32(cpu->_mmu, addr, &cpu->regs[rd]);
        }
    } else {
        uint32_t value = reg_pc12(cpu, rd, pc);
        if (b) {
            fsr = armv4t_st_8(cpu->_mmu, addr, value);
        } else {
            fsr = armv4t_st_32(cpu->_mmu, addr, value);
        }
    }

    if (l && rd == REG_PC) {
        armv4t_branch(cpu, cpu->regs[REG_PC]);
    }

#ifdef ARMV4T_ARM7
    if (!p || w) {
#else
    if ((!p || w) && fsr == 0) {
#endif
        cpu->regs[rn] = offset_addr;
    }

    if (fsr != 0) {
        take_data_abort_exception(cpu, fsr, addr, pc);
    }
}

static void single_xfer_imm_inst(armv4t_cpu *cpu, uint32_t inst, uint32_t pc) {
    bool p = get_bit(inst, 24);
    bool u = get_bit(inst, 23);
    bool b = get_bit(inst, 22);
    bool w = get_bit(inst, 21);
    bool l = get_bit(inst, 20);
    uint32_t rn = get_bits(inst, 16, 4);
    uint32_t rd = get_bits(inst, 12, 4);
    uint32_t offset = get_bits(inst, 0, 12);

    uint32_t offset_addr = reg_pc8(cpu, rn, pc) + (u ? offset : -offset);
    uint32_t addr = p ? offset_addr : reg_pc8(cpu, rn, pc);

    armv4t_fsr fsr;
    if (l) {
        if (b) {
            uint8_t value;
            if ((fsr = armv4t_ld_8(cpu->_mmu, addr, &value)) == 0) {
                cpu->regs[rd] = value;
            }
        } else {
            fsr = armv4t_ld_32(cpu->_mmu, addr, &cpu->regs[rd]);
        }
    } else {
        uint32_t value = reg_pc12(cpu, rd, pc);
        if (b) {
            fsr = armv4t_st_8(cpu->_mmu, addr, value);
        } else {
            fsr = armv4t_st_32(cpu->_mmu, addr, value);
        }
    }

    if (l && rd == REG_PC) {
        armv4t_branch(cpu, cpu->regs[REG_PC]);
    }

#ifdef ARMV4T_ARM7
    if (!p || w) {
#else
    if ((!p || w) && fsr == 0) {
#endif
        cpu->regs[rn] = offset_addr;
    }

    if (fsr != 0) {
        take_data_abort_exception(cpu, fsr, addr, pc);
    }
}

static void halfword_xfer_reg_inst(armv4t_cpu *cpu, uint32_t inst, uint32_t pc) {
    bool p = get_bit(inst, 24);
    bool u = get_bit(inst, 23);
    bool w = get_bit(inst, 21);
    bool l = get_bit(inst, 20);
    uint32_t rn = get_bits(inst, 16, 4);
    uint32_t rd = get_bits(inst, 12, 4);
    uint32_t rm = get_bits(inst, 0, 4);

    bool carry_in = armv4t_get_flag(cpu, FLAG_C);
    uint32_t offset = shift(cpu->regs[rm], SHIFT_LSL, 0, carry_in);
    uint32_t offset_addr = reg_pc8(cpu, rn, pc) + (u ? offset : -offset);
    uint32_t addr = p ? offset_addr : reg_pc8(cpu, rn, pc);

    armv4t_fsr fsr = 0;
    if (l) { // Load
        uint32_t type = get_bits(inst, 5, 2);
        switch (type) {
        case 1: { // Halfword
            uint16_t value;
            if ((fsr = armv4t_ld_16(cpu->_mmu, addr, &value)) == 0) {
                cpu->regs[rd] = value;
            }
            break;
        }
        case 2: { // Signed byte
            uint8_t value;
            if ((fsr = armv4t_ld_8(cpu->_mmu, addr, &value)) == 0) {
                cpu->regs[rd] = (int32_t)(int8_t)value;
            }
            break;
        }
        case 3: { // Signed halfword
            uint16_t value;
            if ((fsr = armv4t_ld_16(cpu->_mmu, addr, &value)) == 0) {
                cpu->regs[rd] = (int32_t)(int16_t)value;
            }
            break;
        }
        }
    } else { // Store
        uint32_t value = reg_pc12(cpu, rd, pc);
        fsr = armv4t_st_16(cpu->_mmu, addr, value);
    }

#ifdef ARMV4T_ARM7
    if (!p || w) {
#else
    if ((!p || w) && fsr == 0) {
#endif
        cpu->regs[rn] = offset_addr;
    }

    if (fsr != 0) {
        take_data_abort_exception(cpu, fsr, addr, pc);
    }
}

static void halfword_xfer_imm_inst(armv4t_cpu *cpu, uint32_t inst, uint32_t pc) {
    bool p = get_bit(inst, 24);
    bool u = get_bit(inst, 23);
    bool w = get_bit(inst, 21);
    bool l = get_bit(inst, 20);
    uint32_t rn = get_bits(inst, 16, 4);
    uint32_t rd = get_bits(inst, 12, 4);
    uint32_t offset = (get_bits(inst, 8, 4) << 4) + get_bits(inst, 0, 4);

    uint32_t offset_addr = reg_pc8(cpu, rn, pc) + (u ? offset : -offset);
    uint32_t addr = p ? offset_addr : reg_pc8(cpu, rn, pc);

    armv4t_fsr fsr = 0;
    if (l) { // Load
        uint32_t type = get_bits(inst, 5, 2);
        switch (type) {
        case 1: { // Halfword
            uint16_t value;
            if ((fsr = armv4t_ld_16(cpu->_mmu, addr, &value)) == 0) {
                cpu->regs[rd] = value;
            }
            break;
        }
        case 2: { // Signed byte
            uint8_t value;
            if ((fsr = armv4t_ld_8(cpu->_mmu, addr, &value)) == 0) {
                cpu->regs[rd] = (int32_t)(int8_t)value;
            }
            break;
        }
        case 3: { // Signed halfword
            uint16_t value;
            if ((fsr = armv4t_ld_16(cpu->_mmu, addr, &value)) == 0) {
                cpu->regs[rd] = (int32_t)(int16_t)value;
            }
            break;
        }
        }
    } else { // Store
        uint32_t value = reg_pc12(cpu, rd, pc);
        fsr = armv4t_st_16(cpu->_mmu, addr, value);
    }

#ifdef ARMV4T_ARM7
    if (!p || w) {
#else
    if ((!p || w) && fsr == 0) {
#endif
        cpu->regs[rn] = offset_addr;
    }

    if (fsr != 0) {
        take_data_abort_exception(cpu, fsr, addr, pc);
    }
}

static void block_xfer_inst(armv4t_cpu *cpu, uint32_t inst, uint32_t pc) {
    bool p = get_bit(inst, 24);
    bool u = get_bit(inst, 23);
    bool s = get_bit(inst, 22);
    bool w = get_bit(inst, 21);
    bool l = get_bit(inst, 20);
    uint32_t rn = get_bits(inst, 16, 4);
    uint32_t rlist = get_bits(inst, 0, 16);

    int rlist_count = popcount32(rlist);
    uint32_t offset = rlist_count * 4;

    uint32_t base_addr = cpu->regs[rn];
    uint32_t offset_addr = base_addr + (u ? offset : -offset);
    uint32_t addr = u ? base_addr : base_addr - offset;
    if (p == u) {
        addr += 4;
    }

    bool rlist_pc = get_bit(rlist, REG_PC);

    armv4t_mode current_mode = armv4t_get_mode(cpu);
    if (s && (!l || !rlist_pc)) {
        armv4t_set_mode(cpu, MODE_USR);
    }

    armv4t_fsr fsr = 0;
    uint32_t fsa;
    for (int i = 0; i < 16; i++, rlist >>= 1) {
        if ((rlist & 1) == 0) {
            continue;
        }

        if (l) { // Load
            if (fsr = armv4t_ld_32(cpu->_mmu, addr, &cpu->regs[i])) {
                fsa = addr;
                break;
            }
        } else { // Store
            if (fsr = armv4t_st_32(cpu->_mmu, addr, reg_pc12(cpu, i, pc))) {
                fsa = addr;
                break;
            }
        }

        addr += 4;
    }

#ifdef ARMV4T_ARM7
    if (w) {
#else
    if (w && fsr == 0) {
#endif
        cpu->regs[rn] = offset_addr;
    }

    armv4t_set_mode(cpu, current_mode);

    if (fsr != 0) {
        take_data_abort_exception(cpu, fsr, fsa, pc);
    } else if (l && rlist_pc) {
        if (s) {
            restore_spsr(cpu);
        }

        armv4t_branch(cpu, cpu->regs[REG_PC]);
    }
}

static void swap_inst(armv4t_cpu *cpu, uint32_t inst, uint32_t pc) {
    bool b = get_bit(inst, 22);
    uint32_t rn = get_bits(inst, 16, 4);
    uint32_t rd = get_bits(inst, 12, 4);
    uint32_t rm = get_bits(inst, 0, 4);

    uint32_t addr = cpu->regs[rn];

    armv4t_fsr fsr;
    uint32_t result = 0;
    if (b) {
        uint8_t value;
        if ((fsr = armv4t_ld_8(cpu->_mmu, addr, &value)) == 0) {
            fsr = armv4t_st_8(cpu->_mmu, addr, cpu->regs[rm]);
            result = value;
        }
    } else {
        if ((fsr = armv4t_ld_32(cpu->_mmu, addr, &result)) == 0) {
            fsr = armv4t_st_32(cpu->_mmu, addr, cpu->regs[rm]);
        }
    }

    if (fsr != 0) {
        take_data_abort_exception(cpu, fsr, addr, pc);
        return;
    }

    cpu->regs[rd] = result;
}

static void swi_inst(armv4t_cpu *cpu, uint32_t pc) {
    take_swi_exception(cpu, pc + 4);
}

void arm_step(armv4t_cpu *cpu) {    
    uint32_t pc = cpu->regs[REG_PC];

    uint32_t inst;
    armv4t_fsr fsr = armv4t_ld_32(cpu->_mmu, pc, &inst);
    if (fsr != 0) {
        take_prefetch_abort_exception(cpu, pc);
        return;
    }

    cpu->regs[REG_PC] += 4;

    uint32_t cond = get_bits(inst, 28, 4);
    if (has_cond(cpu, cond)) {
        if ((inst & BRANCH_EX_MASK) == BRANCH_EX_VALUE) {
            branch_ex_inst(cpu, inst);
        } else if ((inst & BRANCH_MASK) == BRANCH_VALUE) {
            branch_inst(cpu, inst, pc);
        } else if ((inst & MRS_MASK) == MRS_VALUE) {
            mrs_inst(cpu, inst);
        } else if ((inst & MSR_REG_MASK) == MSR_REG_VALUE) {
            msr_reg_inst(cpu, inst);
        } else if ((inst & MSR_IMM_MASK) == MSR_IMM_VALUE) {
            msr_imm_inst(cpu, inst);
        } else if ((inst & DATA_SHIFT_IMM_MASK) == DATA_SHIFT_IMM_VALUE) {
            data_shift_imm_inst(cpu, inst, pc);
        } else if ((inst & DATA_SHIFT_REG_MASK) == DATA_SHIFT_REG_VALUE) {
            data_shift_reg_inst(cpu, inst, pc);
        } else if ((inst & DATA_IMM_MASK) == DATA_IMM_VALUE) {
            data_imm_inst(cpu, inst, pc);
        } else if ((inst & MUL_MASK) == MUL_VALUE) {
            mul_inst(cpu, inst);
        } else if ((inst & MUL_LONG_MASK) == MUL_LONG_VALUE) {
            mul_long_inst(cpu, inst);
        } else if ((inst & SINGLE_XFER_REG_MASK) == SINGLE_XFER_REG_VALUE) {
            single_xfer_reg_inst(cpu, inst, pc);
        } else if ((inst & SINGLE_XFER_IMM_MASK) == SINGLE_XFER_IMM_VALUE) {
            single_xfer_imm_inst(cpu, inst, pc);
        } else if ((inst & SWAP_MASK) == SWAP_VALUE) {
            swap_inst(cpu, inst, pc);
        } else if ((inst & HALFWORD_XFER_REG_MASK) == HALFWORD_XFER_REG_VALUE) {
            halfword_xfer_reg_inst(cpu, inst, pc);
        } else if ((inst & HALFWORD_XFER_IMM_MASK) == HALFWORD_XFER_IMM_VALUE) {
            halfword_xfer_imm_inst(cpu, inst, pc);
        } else if ((inst & BLOCK_XFER_MASK) == BLOCK_XFER_VALUE) {
            block_xfer_inst(cpu, inst, pc);
        } else if ((inst & SWI_MASK) == SWI_VALUE) {
            swi_inst(cpu, pc);
        } else {
            take_undefined_exception(cpu, pc + 4);
        }
    }
}
