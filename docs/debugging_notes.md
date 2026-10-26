# Linux Kernel Character Driver: Debugging & Failure Mode Analysis

This document details failure modes observed during stress testing, race conditions, memory safety validations, and I2C hardware integration on the BeagleBone Black.

---

## 1. Concurrency & Race Conditions

### 1.1 Circular Buffer Head/Tail Corruption Under Multi-Threaded Write
- **Observed Behavior**: High-frequency concurrent writes from 8 user-space threads (`pthread`) caused buffer wraparound overwrites and corrupted read pointers.
- **Root Cause**: The circular buffer indices (`head`, `tail`, `count`) were updated without full atomic locking across both buffer checks and `copy_from_user` blocks.
- **Resolution**:
  - Enforced a per-device `struct mutex` lock wrapping the critical section.
  - Reduced lock hold time by copying data into a temporary stack/kmalloc buffer before holding the mutex for queue insertion.
  - Implemented wait queue (`wake_up_interruptible`) signaling only after the mutex is released to avoid priority inversion.

### 1.2 Deadlock in Nested `ioctl` State Transitions
- **Observed Behavior**: Thread hanging indefinitely when calling `CHARDEV_IOC_RESET` during an active blocking read.
- **Root Cause**: The reset ioctl attempted to acquire the device lock while the reader held it waiting on `wait_event_interruptible`.
- **Resolution**:
  - Unlocked mutex prior to entering `wait_event_interruptible_mutex`.
  - Added cancellation token check on wakeup to gracefully return `-ERESTARTSYS` or `-EINTR`.

---

## 2. Memory Boundaries & Kernel Safety

### 2.1 Fault Injection on Invalid User Pointers
- **Test Case**: `userspace/tests/test_basic_rw.c:test_invalid_user_buffer`
- **Observed Failure**: Memory dereference error when passing `(void *)0xDEADBEEF` to `read()` and `write()`.
- **Fix**: Replaced raw pointer accesses with strict checks using `copy_to_user()` and `copy_from_user()`, evaluating return counts and returning `-EFAULT` cleanly without leaking kernel memory or oopsing.

### 2.2 Boundary Overflow on Large Ioctl Arguments
- **Observed Failure**: User space passing an oversized configuration struct triggered memory corruption past `struct chardev_config`.
- **Fix**: Added `_IOC_SIZE(cmd)` validation against `sizeof(struct chardev_config)` in the `unlocked_ioctl` dispatcher before dispatching to subhandlers.

---

## 3. I2C EEPROM Hardware Integration (BeagleBone Black)

### 3.1 Page Boundary Rollover
- **Observed Failure**: When writing a block crossing the 32-byte page boundary on AT24C256 EEPROMs, writes wrapped around to the beginning of the same page, corrupting data.
- **Fix**: Segmented I2C block writes into sub-transfers aligned to 32-byte page intervals in `chardev_i2c.c:eeprom_write_block`.

### 3.2 Write-Cycle Timing Violation
- **Observed Failure**: Subsequent reads immediately following writes returned NACK on the I2C bus.
- **Fix**: Added write polling / `usleep_range(5000, 10000)` acknowledgment loop to honor the 5ms internal EEPROM write cycle.
