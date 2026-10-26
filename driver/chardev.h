/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * chardev.h — Character device driver public interface
 *
 * Defines ioctl commands, device configuration macros, and the
 * kernel-internal device structure shared between chardev.c and
 * chardev_i2c.c.
 *
 * Copyright (C) 2025 Example Embedded Systems
 */

#ifndef _CHARDEV_H
#define _CHARDEV_H

#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/mutex.h>
#include <linux/i2c.h>
#include <linux/ioctl.h>
#include <linux/types.h>

/* -----------------------------------------------------------------------
 * Module-level constants
 * --------------------------------------------------------------------- */

#define CHARDEV_NAME        "chardev"
#define CHARDEV_CLASS_NAME  "chardev_class"
#define CHARDEV_MAJOR       240          /* Static major; must not conflict  */
#define CHARDEV_MAX_MINORS  4            /* Support up to 4 minor devices    */

/* Circular buffer defaults */
#define CHARDEV_BUF_DEFAULT  4096        /* Default buffer size in bytes     */
#define CHARDEV_BUF_MAX      65536       /* Maximum configurable buffer size */
#define CHARDEV_BUF_MIN      256         /* Minimum configurable buffer size */

/* -----------------------------------------------------------------------
 * ioctl magic and command definitions
 *
 * Magic byte: 0xCD ('C'haracter 'D'evice) — check for conflicts in
 * Documentation/userspace-api/ioctl/ioctl-number.rst before deployment.
 * --------------------------------------------------------------------- */

#define CHARDEV_IOC_MAGIC  'C'

/**
 * CHARDEV_IOC_FLUSH - Discard all pending data in the circular buffer.
 * Argument: none (0)
 */
#define CHARDEV_IOC_FLUSH       _IO(CHARDEV_IOC_MAGIC,  0)

/**
 * CHARDEV_IOC_GET_STATS - Copy struct chardev_stats to userspace.
 * Argument: pointer to struct chardev_stats
 */
#define CHARDEV_IOC_GET_STATS   _IOR(CHARDEV_IOC_MAGIC, 1, struct chardev_stats)

/**
 * CHARDEV_IOC_SET_BUFSIZE - Resize the circular buffer.
 * Argument: pointer to __u32 containing new size in bytes.
 * New size must be within [CHARDEV_BUF_MIN, CHARDEV_BUF_MAX].
 * Returns -EBUSY if data is pending; caller must flush first.
 */
#define CHARDEV_IOC_SET_BUFSIZE _IOW(CHARDEV_IOC_MAGIC, 2, __u32)

/**
 * CHARDEV_IOC_RESET - Reset device to power-on defaults (clears buffer,
 * resets stats, does not affect I2C connection).
 * Argument: none (0)
 */
#define CHARDEV_IOC_RESET       _IO(CHARDEV_IOC_MAGIC,  3)

/**
 * CHARDEV_IOC_EEPROM_SYNC - Force flush of internal write cache to EEPROM.
 * Argument: none (0)
 * Returns 0 on success, -EIO on I2C error.
 */
#define CHARDEV_IOC_EEPROM_SYNC _IO(CHARDEV_IOC_MAGIC,  4)

#define CHARDEV_IOC_MAXNR  4

/* -----------------------------------------------------------------------
 * Userspace-visible structures (safe for copy_to/from_user)
 * --------------------------------------------------------------------- */

/**
 * struct chardev_stats - Device statistics snapshot.
 * @bytes_written:  Total bytes written since last reset.
 * @bytes_read:     Total bytes read since last reset.
 * @overruns:       Number of write overruns (data dropped due to full buf).
 * @buf_size:       Current circular buffer capacity in bytes.
 * @buf_used:       Bytes currently pending in the buffer.
 */
struct chardev_stats {
	__u64 bytes_written;
	__u64 bytes_read;
	__u32 overruns;
	__u32 buf_size;
	__u32 buf_used;
	__u32 _pad;          /* Explicit padding for ABI stability             */
};

/* -----------------------------------------------------------------------
 * Kernel-internal device structure (not exposed to userspace)
 * --------------------------------------------------------------------- */

#ifdef __KERNEL__

/**
 * struct chardev_dev - Per-minor device state.
 * @cdev:       Embedded kernel character device.
 * @lock:       Mutex protecting all mutable fields below.
 * @buf:        Circular buffer (dynamically allocated).
 * @buf_size:   Allocated capacity of @buf.
 * @head:       Write position (producer index).
 * @tail:       Read position (consumer index).
 * @stats:      Accumulated statistics.
 * @i2c_client: I2C client for EEPROM backend (may be NULL).
 * @open_count: Number of current open file descriptors.
 */
struct chardev_dev {
	struct cdev           cdev;
	struct mutex          lock;
	unsigned char        *buf;
	size_t                buf_size;
	size_t                head;
	size_t                tail;
	struct chardev_stats  stats;
	struct i2c_client    *i2c_client;
	int                   open_count;
};

/* Convenience: bytes available to read */
static inline size_t chardev_buf_used(const struct chardev_dev *dev)
{
	return (dev->head - dev->tail + dev->buf_size) % dev->buf_size;
}

/* Convenience: free space in buffer */
static inline size_t chardev_buf_free(const struct chardev_dev *dev)
{
	return dev->buf_size - chardev_buf_used(dev) - 1;
}

#endif /* __KERNEL__ */

#endif /* _CHARDEV_H */
