# ARMv4T CPU

A simple ARMv4T (ARM7T/ARM9T) interpreter written in C.

The system requires the consumer to provide an MMU implementation via `armv4t/mmu.h`, which defines the required 8/16/32-bit load/store operations.

## Features

- [x] ARM instruction set.
- [ ] Thumb instruction set.

### Memory & Exceptions

- [x] MMU interface.
  - [x] Load/store operations.
  - [x] Data aborts.
- [x] Undefined instruction aborts.
- [x] Execution aborts.
- [x] Interrupts (IRQ/FIQ/SWI).
- [x] Fault status/address registers (c5, c6).
- [X] Big-endian mode.

The system implements CP15, but many of the registers must be handled by the consumer MMU implementation. See the [example](example/) project.
