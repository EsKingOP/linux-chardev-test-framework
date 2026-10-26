// SPDX-License-Identifier: GPL-2.0-only
/*
 * chardev_i2c.c — AT24C256 I2C EEPROM backend for chardev
 *
 * Implements page-aware write splitting, mandatory write-cycle delays,
 * and a read path using i2c_master_send / i2c_master_recv.
 *
 * Reference: Atmel AT24C256C datasheet, Rev. 8568D, §7 (Write Operations)
 *
 * Copyright (C) 2025 Example Embedded Systems
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/string.h>

#include "chardev_i2c.h"

/* -----------------------------------------------------------------------
 * Internal helpers
 * --------------------------------------------------------------------- */

/**
 * eeprom_set_addr() - Send a 2-byte address to the EEPROM (no data).
 *
 * The AT24C256 uses a 16-bit word address sent as two bytes (MSB first)
 * in the same I2C transaction as the write data.  For reads, we send the
 * address as a separate write then follow with a read transaction (a
 * "random read" per the datasheet).
 */
static int eeprom_set_addr(struct i2c_client *client, u16 addr)
{
	u8 addr_buf[2] = { (addr >> 8) & 0xFF, addr & 0xFF };
	int ret;

	ret = i2c_master_send(client, addr_buf, sizeof(addr_buf));
	if (ret < 0)
		return ret;
	if (ret != sizeof(addr_buf))
		return -EIO;
	return 0;
}

/* -----------------------------------------------------------------------
 * chardev_i2c_read
 * --------------------------------------------------------------------- */

int chardev_i2c_read(struct i2c_client *client, u16 addr, u8 *buf, size_t len)
{
	int ret;
	size_t done = 0;

	if (!client || !buf || len == 0)
		return -EINVAL;

	if ((size_t)addr + len > EEPROM_TOTAL_SIZE)
		return -EINVAL;

	/*
	 * Random-read sequence:
	 *   1. Write the 16-bit address (dummy write with no data byte)
	 *   2. Repeated-START (handled by i2c_master_recv)
	 *   3. Read N bytes sequentially (AT24C256 auto-increments address)
	 */
	ret = eeprom_set_addr(client, addr);
	if (ret < 0) {
		dev_err(&client->dev,
			"chardev_i2c: address set failed at 0x%04x (%d)\n",
			addr, ret);
		return ret;
	}

	while (done < len) {
		/* Limit burst to 256 bytes to stay within I2C SMBus compat */
		size_t chunk = min(len - done, (size_t)256);

		ret = i2c_master_recv(client, buf + done, (int)chunk);
		if (ret < 0) {
			dev_err(&client->dev,
				"chardev_i2c: read failed at +%zu (%d)\n",
				done, ret);
			return ret;
		}
		done += (size_t)ret;
	}

	return (int)done;
}
EXPORT_SYMBOL_GPL(chardev_i2c_read);

/* -----------------------------------------------------------------------
 * chardev_i2c_write
 *
 * The AT24C256 requires that a write transaction not cross a 64-byte
 * page boundary.  We split multi-page writes here.
 * --------------------------------------------------------------------- */

int chardev_i2c_write(struct i2c_client *client, u16 addr,
		      const u8 *buf, size_t len)
{
	/*
	 * Build a page-write message: 2 address bytes + up to PAGE_SIZE
	 * data bytes = EEPROM_PAGE_SIZE + 2 bytes maximum.
	 */
	u8 tx[EEPROM_ADDR_BYTES + EEPROM_PAGE_SIZE];
	size_t done = 0;
	int ret;

	if (!client || !buf || len == 0)
		return -EINVAL;

	if ((size_t)addr + len > EEPROM_TOTAL_SIZE)
		return -EINVAL;

	while (done < len) {
		u16 cur_addr  = addr + (u16)done;
		/* Bytes left until end of current page */
		size_t page_offset = cur_addr % EEPROM_PAGE_SIZE;
		size_t page_avail  = EEPROM_PAGE_SIZE - page_offset;
		size_t chunk       = min(len - done, page_avail);

		tx[0] = (cur_addr >> 8) & 0xFF;
		tx[1] =  cur_addr       & 0xFF;
		memcpy(&tx[2], buf + done, chunk);

		ret = i2c_master_send(client, tx, (int)(EEPROM_ADDR_BYTES + chunk));
		if (ret < 0) {
			dev_err(&client->dev,
				"chardev_i2c: write failed at 0x%04x (%d)\n",
				cur_addr, ret);
			return ret;
		}
		if ((size_t)ret != EEPROM_ADDR_BYTES + chunk) {
			dev_err(&client->dev,
				"chardev_i2c: short write at 0x%04x\n", cur_addr);
			return -EIO;
		}

		done += chunk;

		/* Wait for the EEPROM write cycle to complete */
		if (done < len)
			msleep(EEPROM_WRITE_CYCLE_MS);
	}

	/* Final write-cycle wait */
	msleep(EEPROM_WRITE_CYCLE_MS);

	return 0;
}
EXPORT_SYMBOL_GPL(chardev_i2c_write);

/* -----------------------------------------------------------------------
 * chardev_i2c_sync
 * --------------------------------------------------------------------- */

int chardev_i2c_sync(struct i2c_client *client)
{
	/* No write cache — nothing to flush */
	(void)client;
	return 0;
}
EXPORT_SYMBOL_GPL(chardev_i2c_sync);

/* -----------------------------------------------------------------------
 * I2C driver probe / remove
 * --------------------------------------------------------------------- */

static const struct i2c_device_id chardev_i2c_id[] = {
	{ "at24c256", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, chardev_i2c_id);

int chardev_i2c_probe(struct i2c_client *client,
		      const struct i2c_device_id *id)
{
	dev_info(&client->dev,
		 "chardev_i2c: probed AT24C256 at 0x%02x on bus %d\n",
		 client->addr, client->adapter->nr);

	/*
	 * In a real deployment the probe would:
	 *   - Verify the EEPROM is accessible with a dummy read
	 *   - Register the client pointer with the chardev minor it serves
	 *   - Optionally read a header block for device identification
	 *
	 * For now we just confirm I2C communication works.
	 */
	{
		u8 dummy;
		int ret = chardev_i2c_read(client, 0x0000, &dummy, 1);
		if (ret < 0) {
			dev_err(&client->dev,
				"chardev_i2c: EEPROM not responding (%d)\n", ret);
			return ret;
		}
	}

	dev_info(&client->dev, "chardev_i2c: EEPROM ready (%d KB)\n",
		 EEPROM_TOTAL_SIZE / 1024);
	return 0;
}
EXPORT_SYMBOL_GPL(chardev_i2c_probe);

int chardev_i2c_remove(struct i2c_client *client)
{
	dev_info(&client->dev, "chardev_i2c: removed\n");
	return 0;
}
EXPORT_SYMBOL_GPL(chardev_i2c_remove);
