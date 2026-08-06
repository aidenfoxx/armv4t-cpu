# ARMv4T CPU

A simple ARMv4T (ARM7TDMI/ARM9TDMI) interpreter written in C.

The project implements behavior defined by the ARM architecture data sheet. Unpredictable behavior is not handled specially.

## Features

- [x] ARM instruction set.
- [ ] Thumb instruction set.

### Memory & Exceptions

The library requires consumers to provide an MMU implementation via `armv4t/mmu.h`, which defines the nescessary 8/16/32-bit load/store operations. The MMU is responsible for aligned address handling: unaligned word access should either rotate the result or return an alignment fault.

- [x] MMU interface.
  - [x] Load/store operations.
  - [x] Data aborts.
  - [ ] User mode access.
- [x] Exceptions (undefined, SWI, prefetch/data abort).
- [x] Interrupts (IRQ/FIQ).
- [x] Fault status/address registers (c5, c6)*.

_* Other CP15 registers are left to the consumer._

## Notes

- The CPU initializes in SYS mode with interrupts enabled, not the architectural reset state.
- Enable LTO in both this library and the consuming project for optimal performance.