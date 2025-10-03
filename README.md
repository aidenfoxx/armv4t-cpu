# ARMv4T CPU

A simple ARMv4T interpreter written in C.

The core requires the consumer to provide an MMU implementation via `armv4t/mmu.h`, which defines the required 8/16/32-bit load/store operations.

## Features

- [x] ARM instruction set
- [ ] Thumb instruction set
- [ ] Big-endian mode

### Memory & Exceptions

- [x] MMU interface
  - [x] Load/store interface
  - [x] Prefetch/data aborts
- [x] Undefined instruction handling
- [ ] Execution aborts
- [ ] Interrupts (IRQ/FIQ/SWI)

### System Control (CP15)

- [-] Partial support
  - [ ] Control register (MMU enable, caches, endianness, alignment faults)
  - [ ] Translation table base
  - [ ] Domain access control
  - [x] Fault status/address (c5, c6)
  - [ ] Cache/TLB ops
  - [ ] Identification registers
