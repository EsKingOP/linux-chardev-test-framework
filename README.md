# linux-chardev-test-framework

A comprehensive Linux kernel character device driver with a full userspace test harness.
Targets the **BeagleBone Black** (ARMv7, Linux 5.10 LTS) with an AT24C256 I2C EEPROM
attached to I2C bus 2 at address `0x50`.

---

## Overview

This project implements:

- A kernel-space **character device driver** (`chardev`) with:
  - Circular-buffer-backed `read`/`write` operations
  - `ioctl` interface for device control (flush, get stats, set buffer size)
  - `mutex`-protected critical sections for SMP safety
  - I2C EEPROM backend via the `chardev_i2c` module

- A userspace **C test framework** (`test_framework`) that:
  - Registers and runs named test cases with pass/fail tracking
  - Provides `ASSERT_EQ`, `ASSERT_NE`, `ASSERT_STR_EQ`, `ASSERT_NULL`, and `ASSERT_ERRNO` macros
  - Supports concurrent test execution via pthreads

---

## Directory Structure

```
linux-chardev-test-framework/
├── driver/
│   ├── Makefile            # Kernel module build system entry point
│   ├── Kbuild              # Kbuild listing obj-m targets
│   ├── chardev.h           # Driver header: ioctl defs, device struct, macros
│   ├── chardev.c           # Main character device driver implementation
│   ├── chardev_i2c.h       # I2C EEPROM interface header
│   └── chardev_i2c.c       # I2C EEPROM read/write implementation
├── userspace/
│   ├── Makefile            # Builds all test binaries
│   ├── test_framework.h    # Test framework API: macros and structs
│   ├── test_framework.c    # Test runner implementation
│   ├── main_test.c         # Main runner: registers and runs all 45 tests
│   └── tests/
│       ├── test_basic_rw.c     # Basic read/write tests (15 test cases)
│       ├── test_ioctl.c        # ioctl interface tests (10 test cases)
│       ├── test_concurrent.c   # 8-thread concurrency tests (10 test cases)
│       └── test_i2c_eeprom.c   # I2C EEPROM tests (10 test cases)
├── scripts/
│   ├── load_driver.sh      # insmod, mknod, chmod
│   ├── unload_driver.sh    # rmmod and cleanup
│   └── capture_logs.sh     # dmesg/strace capture
└── docs/
    └── debugging_notes.md  # Failure mode analysis, observed bugs, fixes
```

---

## Hardware Target

| Item             | Detail                          |
|------------------|---------------------------------|
| Board            | BeagleBone Black Rev C          |
| SoC              | TI AM335x (ARMv7-A Cortex-A8)  |
| Kernel           | Linux 5.10.168-ti-r72           |
| EEPROM           | AT24C256 (32 KB) on I2C2, 0x50 |
| Device node      | `/dev/chardev0`                 |
| Major number     | 240 (static allocation)         |

---

## Build Instructions

### Prerequisites

- ARM cross-compiler: `arm-linux-gnueabihf-gcc`
- Kernel headers for the target kernel (deployed to `/usr/src/linux-headers-*` on BBB, or set `KDIR` to a cross-build tree)
- `make`, `gcc` (for userspace)

### Build the Kernel Module (cross-compile)

```bash
cd driver/
make ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- KDIR=/path/to/bbb/kernel
```

The resulting `chardev.ko` is ready to `scp` to the target.

### Build the Kernel Module (native on BBB)

```bash
cd driver/
make
```

### Build the Userspace Test Binaries

```bash
cd userspace/
make
```

---

## Running on BeagleBone Black

### 1. Transfer and Load the Driver

```bash
# From host
scp driver/chardev.ko root@beaglebone:~/

# On BBB
./scripts/load_driver.sh
```

This calls `insmod`, creates `/dev/chardev0` via `mknod`, and sets permissions.

### 2. Run All Tests

```bash
cd userspace/
./main_test /dev/chardev0
```

### 3. Capture Logs

```bash
./scripts/capture_logs.sh
```

Logs are saved to `logs/dmesg_<timestamp>.txt`.

### 4. Unload the Driver

```bash
./scripts/unload_driver.sh
```

---

## Test Results Summary

**Platform:** BeagleBone Black Rev C, Linux 5.10.168-ti-r72  
**Date:** 2025-11-14  
**Total test cases:** 45  
**Passed:** 44  
**Failed:** 1 (see `docs/debugging_notes.md` — EEPROM page-boundary wrap; fixed in commit `a3f9c12`)

| Suite                | Cases | Passed | Failed |
|----------------------|-------|--------|--------|
| `test_basic_rw`      | 15    | 15     | 0      |
| `test_ioctl`         | 10    | 10     | 0      |
| `test_concurrent`    | 10    | 10     | 0      |
| `test_i2c_eeprom`    | 10    | 9      | 1 *    |

\* `eeprom_page_boundary_wrap`: write crossing a 64-byte page boundary failed on first
run; root cause was missing `msleep(5)` write-cycle delay before second I2C transaction.
Fix applied and verified.

---

## License

GPL-2.0-only. See individual source file headers.
