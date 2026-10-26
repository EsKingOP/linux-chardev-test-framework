/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * chardev_i2c.h — I2C EEPROM (AT24C256) interface for chardev driver
 *
 * Provides byte-granular and page-granular read/write to the AT24C256
 * 32 KB EEPROM over I2C.  Page size is 64 bytes.  Caller is responsible
 * for alignment when doing page writes.
 *
 * Copyright (C) 2025 Example Embedded Systems
 */

#ifndef _CHARDEV_I2C_H
#define _CHARDEV_I2C_H

#ifdef __KERNEL__

#include <linux/i2c.h>
#include <linux/types.h>

/* -----------------------------------------------------------------------
 * EEPROM geometry (AT24C256)
 * --------------------------------------------------------------------- */

#define EEPROM_PAGE_SIZE    64      /* Write page size in bytes             */
#define EEPROM_TOTAL_SIZE   32768   /* Total capacity: 256 Kbit = 32 KB    */
#define EEPROM_ADDR_BYTES   2       /* 16-bit internal address              */

/* I2C write-cycle time: 5 ms max per AT24C256 datasheet, §7 */
#define EEPROM_WRITE_CYCLE_MS  5

/* -----------------------------------------------------------------------
 * API
 * --------------------------------------------------------------------- */

/**
 * chardev_i2c_read() - Read @len bytes from EEPROM at @addr into @buf.
 *
 * Performs a 2-byte register-address write followed by an I2C read.
 * Handles reads that span page boundaries transparently.
 *
 * @client: I2C client bound to the EEPROM device.
 * @addr:   Byte address within the EEPROM (0 .. EEPROM_TOTAL_SIZE-1).
 * @buf:    Kernel buffer to read into.
 * @len:    Number of bytes to read.
 *
 * Returns number of bytes read on success, negative errno on failure.
 */
int chardev_i2c_read(struct i2c_client *client, u16 addr,
		     u8 *buf, size_t len);

/**
 * chardev_i2c_write() - Write @len bytes from @buf to EEPROM at @addr.
 *
 * Splits writes on 64-byte page boundaries as required by the AT24C256.
 * Waits EEPROM_WRITE_CYCLE_MS between page writes.
 *
 * @client: I2C client bound to the EEPROM device.
 * @addr:   Byte address within the EEPROM.
 * @buf:    Kernel buffer containing data to write.
 * @len:    Number of bytes to write.
 *
 * Returns 0 on success, negative errno on failure.
 */
int chardev_i2c_write(struct i2c_client *client, u16 addr,
		      const u8 *buf, size_t len);

/**
 * chardev_i2c_sync() - Force flush of any pending write cache to EEPROM.
 *
 * Currently a no-op (the driver does not maintain a write cache), but
 * provided for ABI compatibility with the CHARDEV_IOC_EEPROM_SYNC ioctl.
 *
 * @client: I2C client.
 *
 * Returns 0 always.
 */
int chardev_i2c_sync(struct i2c_client *client);

/**
 * chardev_i2c_probe() - Probe callback registered with the I2C subsystem.
 *
 * Called when the kernel matches a device tree node (compatible =
 * "atmel,at24c256") or a board-file entry to this driver.
 */
int chardev_i2c_probe(struct i2c_client *client,
		      const struct i2c_device_id *id);

/**
 * chardev_i2c_remove() - Remove callback; inverse of probe.
 */
int chardev_i2c_remove(struct i2c_client *client);

#endif /* __KERNEL__ */

#endif /* _CHARDEV_I2C_H */
