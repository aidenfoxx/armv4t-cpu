# ARMv4T CPU

A simple ARMv4T (ARM7T/ARM9T) interpreter written in C.

The project does not attempt to handle unpredictable behavior, and consumers requiring silicone accurate behavior may experience instability.

## Features

- [x] ARM instruction set.
- [ ] Thumb instruction set.

### Memory & Exceptions

The library requires consumers to provide an MMU implementation via `armv4t/mmu.h`, which defines the required 8/16/32-bit load/store operations.

- [x] MMU interface.
  - [x] Load/store operations.
  - [x] Data aborts.
- [x] Undefined instruction aborts.
- [x] Execution aborts.
- [x] Interrupts (IRQ/FIQ/SWI).
- [x] Fault status/address registers (c5, c6).
- [X] Big-endian mode.

_* The CPU partially implements CP15, but many of the registers and features must be implemented by the consumer._

## Notes

- It is recommended to enable Link Time Optimization (LTO) for optimal performance.
