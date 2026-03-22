# ARMv4T CPU

A simple, Linux capable, ARMv4T (ARM920T) interpreter written in C.

The system requires the consumer to provide an MMU implementation via `armv4t/mmu.h`, which defines the required 8/16/32-bit load/store operations.

## Features

- [x] ARM instruction set.
- [ ] Thumb instruction set.
- [ ] Big-endian mode.

### Memory & Exceptions

- [x] MMU interface.
  - [x] Load/store interface.
  - [x] Data aborts.
- [x] Undefined instruction aborts.
- [x] Execution aborts.
- [x] Interrupts (IRQ/FIQ/SWI).
- [x] Fault status/address registers (c5, c6).

The system implements CP15, but many of the registers must be handled by the consumer MMU implementation. See the [example](example/) project.

## Notes

- Does not support compiling for big-endian targets.
