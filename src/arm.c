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
#define PSR_REG_MASK 0x0FFE0FF0
#define PSR_REG_VALUE 0x01080000
#define PSR_IMM_MASK 0x0FFFF000
#define PSR_IMM_VALUE 0x0328F000
#define MUL_MASK 0x0FC000F0
#define MUL_VALUE 0x00000090
#define MUL_LONG_MASK 0x0F8000F0
#define MUL_LONG_VALUE 0x00800090
#define SINGLE_XFER_REG_MASK 0x0E000000
#define SINGLE_XFER_REG_VALUE 0x06000000
#define SINGLE_XFER_IMM_MASK 0x0E000000
#define SINGLE_XFER_IMM_VALUE 0x04000000
#define HALFWORD_XFER_REG_MASK 0x0E400F90
#define HALFWORD_XFER_REG_VALUE 0x00000090
#define HALFWORD_XFER_IMM_MASK 0x0E400090
#define HALFWORD_XFER_IMM_VALUE 0x00400090
#define BLOCK_XFER_MASK 0x0E400000
#define BLOCK_XFER_VALUE 0x08000000
#define SWAP_MASK 0x0FB00FF0
#define SWAP_VALUE 0x01000090

#define OP_TST_MASK 0x0C
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

static uint32_t store_reg(const armv4t_cpu *cpu, int reg) {
    return reg == REG_PC ? cpu->regs[REG_PC] + 4 : cpu->regs[reg];
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

static uint32_t shift_c(uint32_t value, int type, int count, bool carry_in, bool *carry_out) {
    if (count == 0) {
        // TODO: Check that this is correct for all cases
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
        return ((uint32_t)carry_in << 31) | (value >> 1);
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

static void branch_ex_inst(armv4t_cpu *cpu, uint32_t inst) {
    int rn = get_bits(inst, 0, 4);
    armv4t_bx(cpu, cpu->regs[rn]);
}

static void branch_inst(armv4t_cpu *cpu, uint32_t inst) {
    bool l = get_bit(inst, 24);
    int offset = get_bits(inst, 0, 24);
    bool negative = get_bit(offset, 23);

    uint32_t offset_s = negative ? (offset | 0xFF000000) * 4 : (uint32_t)offset * 4;

    if (l) { // BL
        cpu->regs[REG_LR] = cpu->regs[REG_PC] - 4;
    }

    cpu->regs[REG_PC] = (cpu->regs[REG_PC] + offset_s) & ~0x3;
}

static void data_shift_imm_inst(armv4t_cpu *cpu, uint32_t inst, bool *branch) {
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
    bool overflow = armv4t_get_flag(cpu, FLAG_V);
    bool carry_in = armv4t_get_flag(cpu, FLAG_C);

    uint32_t op2 = shift_c(cpu->regs[rm], shift_t, shift_n, carry_in, &carry);
    uint32_t result = data_op(opcode, cpu->regs[rn], op2, carry_in, &carry, &overflow);

    if ((opcode & OP_TST_MASK) != OP_TST_VALUE) {
        if (rd == REG_PC) {
            cpu->regs[REG_PC] = result & ~0x3;
            *branch = true;
        } else {
            cpu->regs[rd] = result;
        }
    }

    if (s && rd != REG_PC) {
        armv4t_set_flag(cpu, FLAG_N, get_bit(result, 31));
        armv4t_set_flag(cpu, FLAG_Z, result == 0);
        armv4t_set_flag(cpu, FLAG_C, carry);
        armv4t_set_flag(cpu, FLAG_V, overflow);
    }
}

static void data_shift_reg_inst(armv4t_cpu *cpu, uint32_t inst, bool *branch) {
    int opcode = get_bits(inst, 21, 4);
    bool s = get_bit(inst, 20);
    int rn = get_bits(inst, 16, 4);
    int rd = get_bits(inst, 12, 4);
    int rs = get_bits(inst, 8, 4);
    int rm = get_bits(inst, 0, 4);
    int type = get_bits(inst, 5, 2);

    int shift_t = decode_reg_shift(type);
    int shift_n = get_bits(cpu->regs[rs], 0, 8);

    bool carry = false;
    bool overflow = armv4t_get_flag(cpu, FLAG_V);
    bool carry_in = armv4t_get_flag(cpu, FLAG_C);

    // https://problemkaputt.de/gbatek-arm-opcodes-data-processing-alu.htm
    uint32_t shifted = shift_c(store_reg(cpu, rm), shift_t, shift_n, carry_in, &carry);
    uint32_t result = data_op(opcode, store_reg(cpu, rn), shifted, carry_in, &carry, &overflow);

    if ((opcode & OP_TST_MASK) != OP_TST_VALUE) {
        if (rd == REG_PC) {
            cpu->regs[REG_PC] = result & ~0x3;
            *branch = true;
        } else {
            cpu->regs[rd] = result;
        }
    }

    if (s && rd != REG_PC) {
        armv4t_set_flag(cpu, FLAG_N, get_bit(result, 31));
        armv4t_set_flag(cpu, FLAG_Z, result == 0);
        armv4t_set_flag(cpu, FLAG_C, carry);
        armv4t_set_flag(cpu, FLAG_V, overflow);
    }
}

static void data_imm_inst(armv4t_cpu *cpu, uint32_t inst, bool *branch) {
    int opcode = get_bits(inst, 21, 4);
    bool s = get_bit(inst, 20);
    int rn = get_bits(inst, 16, 4);
    int rd = get_bits(inst, 12, 4);
    int imm12 = get_bits(inst, 0, 12);

    bool carry = false;
    bool overflow = armv4t_get_flag(cpu, FLAG_V);
    bool carry_in = armv4t_get_flag(cpu, FLAG_C);

    uint32_t imm32 = expand_imm_c(imm12, carry_in, &carry);
    uint32_t result = data_op(opcode, cpu->regs[rn], imm32, carry_in, &carry, &overflow);

    if ((opcode & OP_TST_MASK) != OP_TST_VALUE) {
        if (rd == REG_PC) {
            cpu->regs[REG_PC] = result & ~0x3;
            *branch = true;
        } else {
            cpu->regs[rd] = result;
        }
    }

    if (s && rd != REG_PC) {
        armv4t_set_flag(cpu, FLAG_N, get_bit(result, 31));
        armv4t_set_flag(cpu, FLAG_Z, result == 0);
        armv4t_set_flag(cpu, FLAG_C, carry);
        armv4t_set_flag(cpu, FLAG_V, overflow);
    }
}

static void psr_reg_inst(armv4t_cpu *cpu, uint32_t inst) {
    bool s = get_bit(inst, 21);
    if (s) {
        int rm = get_bits(inst, 0, 4);
        set_nzcv(cpu, get_bits(cpu->regs[rm], 27, 4));
    } else {
        int rd = get_bits(inst, 12, 4);
        cpu->regs[rd] = cpu->cpsr;
    }
}

static void psr_imm_inst(armv4t_cpu *cpu, uint32_t inst) {
    int imm12 = get_bits(inst, 0, 12);
    uint32_t imm32 = expand_imm(imm12, armv4t_get_flag(cpu, FLAG_C));
    set_nzcv(cpu, get_bits(imm32, 27, 4));
}

static void mul_inst(armv4t_cpu *cpu, uint32_t inst) {
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

    if (s) { // TODO: Should this check if rd not PC?
        armv4t_set_flag(cpu, FLAG_N, get_bit(result, 31));
        armv4t_set_flag(cpu, FLAG_Z, result == 0);
    }
}

static void mul_long_inst(armv4t_cpu *cpu, uint32_t inst) {
    bool u = get_bit(inst, 22);
    bool a = get_bit(inst, 21);
    bool s = get_bit(inst, 20);
    int rd_hi = get_bits(inst, 16, 4);
    int rd_lo = get_bits(inst, 12, 4);
    int rs = get_bits(inst, 8, 4);
    int rm = get_bits(inst, 0, 4);

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

    if (s) { // TODO: Should this check if rd not PC?
        armv4t_set_flag(cpu, FLAG_N, result >> 63);
        armv4t_set_flag(cpu, FLAG_Z, result == 0);
    }
}

static void single_xfer_reg_inst(armv4t_cpu *cpu, uint32_t inst, bool *branch) {
    bool p = get_bit(inst, 24);
    bool u = get_bit(inst, 23);
    bool b = get_bit(inst, 22);
    bool w = get_bit(inst, 21);
    bool l = get_bit(inst, 20);
    int rn = get_bits(inst, 16, 4);
    int rd = get_bits(inst, 12, 4);
    int rm = get_bits(inst, 0, 4);
    int imm5 = get_bits(inst, 7, 5);
    int type = get_bits(inst, 5, 2);

    int shift_n;
    int shift_t = decode_imm_shift(type, imm5, &shift_n);

    bool carry_in = armv4t_get_flag(cpu, FLAG_C);
    uint32_t offset = shift(cpu->regs[rm], shift_t, shift_n, carry_in);
    uint32_t offset_addr = cpu->regs[rn] + (u ? offset : -offset);
    uint32_t addr = p ? offset_addr : cpu->regs[rn];

    armv4t_fsr fsr;
    if (l) {
        if (b) {
            uint8_t value;
            if ((fsr = armv4t_ld_8(cpu->_mmu, addr, &value)) == 0) {
                cpu->regs[rd] = value;
            };
        } else {
            fsr = armv4t_ld_32(cpu->_mmu, addr, &cpu->regs[rd]);
        }
    } else {
        uint32_t value = store_reg(cpu, rd);
        if (b) {
            fsr = armv4t_st_8(cpu->_mmu, addr, value);
        } else {
            fsr = armv4t_st_32(cpu->_mmu, addr, value);
        }
    }

    if (l && rd == REG_PC) {
        cpu->regs[REG_PC] &= ~3;
        *branch = true;
    }

#ifdef ARMV4T_ARM7
    if (!p || w) {
#else
    if ((!p || w) && fsr == 0) {
#endif
        cpu->regs[rn] = offset_addr;
    }

    if (fsr != 0) {
        raise_data_abort(cpu, fsr, addr);
    }
}

static void single_xfer_imm_inst(armv4t_cpu *cpu, uint32_t inst, bool *branch) {
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

    armv4t_fsr fsr;
    if (l) {
        if (b) {
            uint8_t value;
            if ((fsr = armv4t_ld_8(cpu->_mmu, addr, &value)) == 0) {
                cpu->regs[rd] = value;
            };
        } else {
            fsr = armv4t_ld_32(cpu->_mmu, addr, &cpu->regs[rd]);
        }
    } else {
        uint32_t value = store_reg(cpu, rd);
        if (b) {
            fsr = armv4t_st_8(cpu->_mmu, addr, value);
        } else {
            fsr = armv4t_st_32(cpu->_mmu, addr, value);
        }
    }

    if (l && rd == REG_PC) {
        cpu->regs[REG_PC] &= ~3;
        *branch = true;
    }

#ifdef ARMV4T_ARM7
    if (!p || w) {
#else
    if ((!p || w) && fsr == 0) {
#endif
        cpu->regs[rn] = offset_addr;
    }

    if (fsr != 0) {
        raise_data_abort(cpu, fsr, addr);
    }
}

static void halfword_xfer_reg_inst(armv4t_cpu *cpu, uint32_t inst) {
    bool p = get_bit(inst, 24);
    bool u = get_bit(inst, 23);
    bool w = get_bit(inst, 21);
    bool l = get_bit(inst, 20);
    int rn = get_bits(inst, 16, 4);
    int rd = get_bits(inst, 12, 4);
    int rm = get_bits(inst, 0, 4);

    bool carry_in = armv4t_get_flag(cpu, FLAG_C);
    uint32_t offset = shift(cpu->regs[rm], SHIFT_LSL, 0, carry_in);
    uint32_t offset_addr = cpu->regs[rn] + (u ? offset : -offset);
    uint32_t addr = p ? offset_addr : cpu->regs[rn];

    armv4t_fsr fsr = 0;
    if (l) { // Load
        int type = get_bits(inst, 5, 2);
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
        fsr = armv4t_st_16(cpu->_mmu, addr, store_reg(cpu, rd));
    }

#ifdef ARMV4T_ARM7
    if (!p || w) {
#else
    if ((!p || w) && fsr == 0) {
#endif
        cpu->regs[rn] = offset_addr;
    }

    if (fsr != 0) {
        raise_data_abort(cpu, fsr, addr);
    }
}

static void halfword_xfer_imm_inst(armv4t_cpu *cpu, uint32_t inst) {
    bool p = get_bit(inst, 24);
    bool u = get_bit(inst, 23);
    bool w = get_bit(inst, 21);
    bool l = get_bit(inst, 20);
    int rn = get_bits(inst, 16, 4);
    int rd = get_bits(inst, 12, 4);
    int offset = (get_bits(inst, 8, 4) << 4) + get_bits(inst, 0, 4);

    uint32_t offset_addr = cpu->regs[rn] + (u ? offset : -offset);
    uint32_t addr = p ? offset_addr : cpu->regs[rn];

    armv4t_fsr fsr = 0;
    if (l) { // Load
        int type = get_bits(inst, 5, 2);
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
        fsr = armv4t_st_16(cpu->_mmu, addr, store_reg(cpu, rd));
    }

#ifdef ARMV4T_ARM7
    if (!p || w) {
#else
    if ((!p || w) && fsr == 0) {
#endif
        cpu->regs[rn] = offset_addr;
    }

    if (fsr != 0) {
        raise_data_abort(cpu, fsr, addr);
    }
}

/**
 * This method handles a number of quirks described at:
 * https://problemkaputt.de/gbatek-arm-opcodes-memory-block-data-transfer-ldm-stm.htm
 */
static void block_xfer_inst(armv4t_cpu *cpu, uint32_t inst, bool *branch) {
    bool p = get_bit(inst, 24);
    bool u = get_bit(inst, 23);
    bool s = get_bit(inst, 22);
    bool w = get_bit(inst, 21);
    bool l = get_bit(inst, 20);
    int rn = get_bits(inst, 16, 4);
    int rlist = get_bits(inst, 0, 16);

    int rlist_count = popcount32(rlist);
    int offset = rlist_count * 4;

    if (rlist == 0) {
        rlist = (1 << REG_PC);
        offset = 0x40;
    } else if (l && (rlist & (1 << rn))) {
        w = false;
    }

    uint32_t base_addr = cpu->regs[rn];
    uint32_t offset_addr = base_addr + (u ? offset : -offset);
    uint32_t addr = u ? base_addr : base_addr - offset;
    if (p == u) {
        addr += 4;
    }

    bool rlist_pc = rlist & (1 << REG_PC);
    int restore_mode = -1;
    if (s && (!l || !rlist_pc)) {
        restore_mode = armv4t_set_mode(cpu, MODE_USR);
    }

    armv4t_fsr fsr;
    uint32_t fsa;
    uint32_t start_addr = addr;
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
            uint32_t value = store_reg(cpu, i);
            if (w && i == rn) {
                value = addr == start_addr ? base_addr : offset_addr;
            }

            if (fsr = armv4t_st_32(cpu->_mmu, addr, value)) {
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

    if (restore_mode != -1) {
        armv4t_set_mode(cpu, restore_mode);
    }

    if (fsr != 0) {
        raise_data_abort(cpu, fsr, fsa);
    } else if (l && rlist_pc) {
        if (s) {
            restore_spsr(cpu);
        }

        cpu->regs[REG_PC] &= armv4t_get_flag(cpu, FLAG_THUMB) ? ~1 : ~3;
        *branch = true;
    }
}

static void swap_inst(armv4t_cpu *cpu, uint32_t inst) {
    bool b = get_bit(inst, 22);
    int rn = get_bits(inst, 16, 4);
    int rd = get_bits(inst, 12, 4);
    int rm = get_bits(inst, 0, 4);

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
        raise_data_abort(cpu, fsr, addr);
        return;
    }

    cpu->regs[rd] = result;
}

void arm_step(armv4t_cpu *cpu) {
    uint32_t inst;
    armv4t_fsr fsr = armv4t_ld_32(cpu->_mmu, cpu->regs[REG_PC], &inst);
    if (fsr != 0) {
        raise_prefetch_abort(cpu, fsr, cpu->regs[REG_PC]);
        return;
    }

    cpu->regs[REG_PC] += 8;

    bool branch = false;
    int cond = get_bits(inst, 28, 4);
    if (has_cond(cpu, cond)) {
        if ((inst & BRANCH_EX_MASK) == BRANCH_EX_VALUE) {
            branch_ex_inst(cpu, inst);
            branch = true;
        } else if ((inst & BRANCH_MASK) == BRANCH_VALUE) {
            branch_inst(cpu, inst);
            branch = true;
        } else if ((inst & DATA_SHIFT_IMM_MASK) == DATA_SHIFT_IMM_VALUE) {
            data_shift_imm_inst(cpu, inst, &branch);
        } else if ((inst & DATA_SHIFT_REG_MASK) == DATA_SHIFT_REG_VALUE) {
            data_shift_reg_inst(cpu, inst, &branch);
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
        } else {
            raise_undefined(cpu, inst);
        }
    }

    if (!branch) {
        cpu->regs[REG_PC] -= 4;
    }
}
