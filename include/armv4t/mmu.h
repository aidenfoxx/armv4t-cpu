#ifndef ARMV4T_MMU_H
#define ARMV4T_MMU_H

#include <stdint.h>

typedef struct mmu mmu_t;

typedef enum {
    MMU_OK,
    MMU_EACCESS,
    MMU_EALIGN,
} mmu_status_t;

static inline mmu_status_t ld_8(mmu_t *mmu, uint32_t addr, uint8_t *dest);
static inline mmu_status_t ld_16(mmu_t *mmu, uint32_t addr, uint16_t *dest);
static inline mmu_status_t ld_32(mmu_t *mmu, uint32_t addr, uint32_t *dest);

static inline mmu_status_t st_8(mmu_t *mmu, uint32_t addr, uint8_t value);
static inline mmu_status_t st_16(mmu_t *mmu, uint32_t addr, uint16_t value);
static inline mmu_status_t st_32(mmu_t *mmu, uint32_t addr, uint32_t value);

#endif // ARMV4T_MMU_H
