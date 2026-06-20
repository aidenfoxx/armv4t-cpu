#ifndef ARMV4T_MMU_H
#define ARMV4T_MMU_H

#include <stdint.h>

#define MAKE_FSR(fault, domain) (((fault) & 0xF) | (((domain) & 0xF) << 4))

#define FSR_FAULT(fsr) ((fsr) & 0xF)
#define FSR_DOMAIN(fsr) (((fsr) >> 4) & 0xF)

typedef enum {
    FAULT_ALIGN_0 = 0x01,
    FAULT_ALIGN_1 = 0x03,
    FAULT_TRANS_S = 0x05,
    FAULT_TRANS_P = 0x07,
    FAULT_DOMAIN_S = 0x09,
    FAULT_DOMAIN_P = 0x0B,
    FAULT_PERM_S = 0x0D,
    FAULT_PERM_P = 0x0F,
} armv4t_fault;

typedef uint32_t armv4t_fsr;

typedef struct armv4t_mmu armv4t_mmu;

armv4t_fsr armv4t_ld_8(armv4t_mmu *mmu, uint32_t addr, uint8_t *value);
armv4t_fsr armv4t_ld_16(armv4t_mmu *mmu, uint32_t addr, uint16_t *value);
armv4t_fsr armv4t_ld_32(armv4t_mmu *mmu, uint32_t addr, uint32_t *value);

armv4t_fsr armv4t_st_8(armv4t_mmu *mmu, uint32_t addr, uint8_t value);
armv4t_fsr armv4t_st_16(armv4t_mmu *mmu, uint32_t addr, uint16_t value);
armv4t_fsr armv4t_st_32(armv4t_mmu *mmu, uint32_t addr, uint32_t value);

#endif // ARMV4T_MMU_H
