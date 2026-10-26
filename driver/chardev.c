// SPDX-License-Identifier: GPL-2.0-only
/*
 * chardev.c — Linux kernel character device driver
 *
 * Implements a circular-buffer-backed character device with ioctl
 * control interface and optional I2C EEPROM backend.
 *
 * Tested on: BeagleBone Black, Linux 5.10.168-ti-r72
 *
 * Copyright (C) 2025 Example Embedded Systems
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/uaccess.h>
#include <linux/ioctl.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/types.h>

#include "chardev.h"
#include "chardev_i2c.h"

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Example Embedded Systems");
MODULE_DESCRIPTION("Character device driver with circular buffer and I2C EEPROM backend");
MODULE_VERSION("1.0.0");

/* -----------------------------------------------------------------------
 * Module parameters
 * --------------------------------------------------------------------- */

static int  num_devices = 1;
static uint buf_size    = CHARDEV_BUF_DEFAULT;

module_param(num_devices, int,  0444);
module_param(buf_size,    uint, 0444);
MODULE_PARM_DESC(num_devices, "Number of minor device nodes to create (max 4)");
MODULE_PARM_DESC(buf_size,    "Initial circular buffer size in bytes (default 4096)");

/* -----------------------------------------------------------------------
 * Global driver state
 * --------------------------------------------------------------------- */

static dev_t            chardev_devno;
static struct class    *chardev_class;
static struct chardev_dev chardev_devices[CHARDEV_MAX_MINORS];

/* -----------------------------------------------------------------------
 * File operations — open / release
 * --------------------------------------------------------------------- */

static int chardev_open(struct inode *inode, struct file *filp)
{
	struct chardev_dev *dev;

	dev = container_of(inode->i_cdev, struct chardev_dev, cdev);
	filp->private_data = dev;

	mutex_lock(&dev->lock);
	dev->open_count++;
	mutex_unlock(&dev->lock);

	pr_debug("chardev: opened minor %u (open_count=%d)\n",
		 iminor(inode), dev->open_count);
	return 0;
}

static int chardev_release(struct inode *inode, struct file *filp)
{
	struct chardev_dev *dev = filp->private_data;

	mutex_lock(&dev->lock);
	dev->open_count--;
	mutex_unlock(&dev->lock);

	pr_debug("chardev: released minor %u (open_count=%d)\n",
		 iminor(inode), dev->open_count);
	return 0;
}

/* -----------------------------------------------------------------------
 * File operations — read
 *
 * Copies bytes from the tail of the circular buffer to userspace.
 * Returns the number of bytes copied, 0 if the buffer is empty.
 * --------------------------------------------------------------------- */

static ssize_t chardev_read(struct file *filp, char __user *ubuf,
			    size_t count, loff_t *ppos)
{
	struct chardev_dev *dev = filp->private_data;
	size_t avail, to_end, n;
	ssize_t ret = 0;

	if (count == 0)
		return 0;

	mutex_lock(&dev->lock);

	avail = chardev_buf_used(dev);
	if (avail == 0) {
		ret = 0;
		goto out_unlock;
	}

	n = min(count, avail);

	/* How many contiguous bytes remain before wrapping? */
	to_end = dev->buf_size - dev->tail;

	if (n <= to_end) {
		if (copy_to_user(ubuf, dev->buf + dev->tail, n)) {
			ret = -EFAULT;
			goto out_unlock;
		}
		dev->tail = (dev->tail + n) % dev->buf_size;
	} else {
		/* Two-part copy: tail→end, then 0→(n - to_end) */
		if (copy_to_user(ubuf, dev->buf + dev->tail, to_end)) {
			ret = -EFAULT;
			goto out_unlock;
		}
		if (copy_to_user(ubuf + to_end, dev->buf, n - to_end)) {
			ret = -EFAULT;
			goto out_unlock;
		}
		dev->tail = n - to_end;
	}

	dev->stats.bytes_read += n;
	ret = (ssize_t)n;

out_unlock:
	mutex_unlock(&dev->lock);
	return ret;
}

/* -----------------------------------------------------------------------
 * File operations — write
 *
 * Copies bytes from userspace into the head of the circular buffer.
 * If the buffer is full, oldest data is overwritten and overrun count
 * is incremented (lossy producer model).
 * --------------------------------------------------------------------- */

static ssize_t chardev_write(struct file *filp, const char __user *ubuf,
			     size_t count, loff_t *ppos)
{
	struct chardev_dev *dev = filp->private_data;
	size_t free_space, to_end, n;
	ssize_t ret = 0;

	if (count == 0)
		return 0;

	mutex_lock(&dev->lock);

	free_space = chardev_buf_free(dev);

	if (count > free_space) {
		/* Overrun: advance tail to make room, count the event */
		size_t overflow = count - free_space;
		dev->tail = (dev->tail + overflow) % dev->buf_size;
		dev->stats.overruns++;
		pr_warn_ratelimited("chardev: buffer overrun (%zu bytes dropped)\n",
				    overflow);
	}

	n = min(count, dev->buf_size - 1);  /* Never fill the buffer 100% */
	to_end = dev->buf_size - dev->head;

	if (n <= to_end) {
		if (copy_from_user(dev->buf + dev->head, ubuf, n)) {
			ret = -EFAULT;
			goto out_unlock;
		}
		dev->head = (dev->head + n) % dev->buf_size;
	} else {
		if (copy_from_user(dev->buf + dev->head, ubuf, to_end)) {
			ret = -EFAULT;
			goto out_unlock;
		}
		if (copy_from_user(dev->buf, ubuf + to_end, n - to_end)) {
			ret = -EFAULT;
			goto out_unlock;
		}
		dev->head = n - to_end;
	}

	dev->stats.bytes_written += n;
	ret = (ssize_t)n;

out_unlock:
	mutex_unlock(&dev->lock);
	return ret;
}

/* -----------------------------------------------------------------------
 * File operations — ioctl
 * --------------------------------------------------------------------- */

static long chardev_ioctl(struct file *filp, unsigned int cmd,
			  unsigned long arg)
{
	struct chardev_dev   *dev = filp->private_data;
	struct chardev_stats  stats_snapshot;
	__u32                 new_size;
	unsigned char        *new_buf;
	int                   ret = 0;

	/* Validate magic and command number */
	if (_IOC_TYPE(cmd) != CHARDEV_IOC_MAGIC)
		return -ENOTTY;
	if (_IOC_NR(cmd) > CHARDEV_IOC_MAXNR)
		return -ENOTTY;

	/* Check userspace pointer access rights */
	if (_IOC_DIR(cmd) & _IOC_READ) {
		if (!access_ok((void __user *)arg, _IOC_SIZE(cmd)))
			return -EFAULT;
	}
	if (_IOC_DIR(cmd) & _IOC_WRITE) {
		if (!access_ok((void __user *)arg, _IOC_SIZE(cmd)))
			return -EFAULT;
	}

	switch (cmd) {
	case CHARDEV_IOC_FLUSH:
		mutex_lock(&dev->lock);
		dev->head = 0;
		dev->tail = 0;
		mutex_unlock(&dev->lock);
		pr_debug("chardev: ioctl FLUSH\n");
		break;

	case CHARDEV_IOC_GET_STATS:
		mutex_lock(&dev->lock);
		stats_snapshot          = dev->stats;
		stats_snapshot.buf_size = (u32)dev->buf_size;
		stats_snapshot.buf_used = (u32)chardev_buf_used(dev);
		mutex_unlock(&dev->lock);
		if (copy_to_user((void __user *)arg, &stats_snapshot,
				 sizeof(stats_snapshot)))
			return -EFAULT;
		break;

	case CHARDEV_IOC_SET_BUFSIZE:
		if (copy_from_user(&new_size, (void __user *)arg,
				   sizeof(new_size)))
			return -EFAULT;

		if (new_size < CHARDEV_BUF_MIN || new_size > CHARDEV_BUF_MAX)
			return -EINVAL;

		mutex_lock(&dev->lock);

		if (chardev_buf_used(dev) > 0) {
			mutex_unlock(&dev->lock);
			return -EBUSY;
		}

		new_buf = kmalloc(new_size, GFP_KERNEL);
		if (!new_buf) {
			mutex_unlock(&dev->lock);
			return -ENOMEM;
		}

		kfree(dev->buf);
		dev->buf      = new_buf;
		dev->buf_size = new_size;
		dev->head     = 0;
		dev->tail     = 0;
		dev->stats.buf_size = new_size;

		mutex_unlock(&dev->lock);
		pr_debug("chardev: ioctl SET_BUFSIZE -> %u bytes\n", new_size);
		break;

	case CHARDEV_IOC_RESET:
		mutex_lock(&dev->lock);
		dev->head = 0;
		dev->tail = 0;
		memset(&dev->stats, 0, sizeof(dev->stats));
		dev->stats.buf_size = (u32)dev->buf_size;
		mutex_unlock(&dev->lock);
		pr_debug("chardev: ioctl RESET\n");
		break;

	case CHARDEV_IOC_EEPROM_SYNC:
		if (!dev->i2c_client)
			return -ENODEV;
		ret = chardev_i2c_sync(dev->i2c_client);
		if (ret)
			pr_err("chardev: EEPROM sync failed (%d)\n", ret);
		break;

	default:
		return -ENOTTY;
	}

	return ret;
}

/* -----------------------------------------------------------------------
 * File operations table
 * --------------------------------------------------------------------- */

static const struct file_operations chardev_fops = {
	.owner          = THIS_MODULE,
	.open           = chardev_open,
	.release        = chardev_release,
	.read           = chardev_read,
	.write          = chardev_write,
	.unlocked_ioctl = chardev_ioctl,
};

/* -----------------------------------------------------------------------
 * Per-device initialisation / teardown helpers
 * --------------------------------------------------------------------- */

static int chardev_init_one(struct chardev_dev *dev, int minor)
{
	int ret;
	dev_t devno = MKDEV(MAJOR(chardev_devno), minor);

	mutex_init(&dev->lock);

	dev->buf = kmalloc(buf_size, GFP_KERNEL);
	if (!dev->buf)
		return -ENOMEM;

	dev->buf_size = buf_size;
	dev->head     = 0;
	dev->tail     = 0;
	dev->open_count = 0;
	memset(&dev->stats, 0, sizeof(dev->stats));
	dev->stats.buf_size = (u32)buf_size;
	dev->i2c_client = NULL;

	cdev_init(&dev->cdev, &chardev_fops);
	dev->cdev.owner = THIS_MODULE;

	ret = cdev_add(&dev->cdev, devno, 1);
	if (ret) {
		pr_err("chardev: cdev_add failed for minor %d (%d)\n", minor, ret);
		kfree(dev->buf);
		dev->buf = NULL;
		return ret;
	}

	device_create(chardev_class, NULL, devno, NULL,
		      CHARDEV_NAME "%d", minor);

	pr_info("chardev: registered minor %d at %d:%d\n",
		minor, MAJOR(chardev_devno), minor);
	return 0;
}

static void chardev_destroy_one(struct chardev_dev *dev, int minor)
{
	dev_t devno = MKDEV(MAJOR(chardev_devno), minor);

	device_destroy(chardev_class, devno);
	cdev_del(&dev->cdev);
	kfree(dev->buf);
	dev->buf = NULL;
	mutex_destroy(&dev->lock);
}

/* -----------------------------------------------------------------------
 * Module init / exit
 * --------------------------------------------------------------------- */

static int __init chardev_module_init(void)
{
	int ret, i;

	if (num_devices < 1 || num_devices > CHARDEV_MAX_MINORS) {
		pr_err("chardev: invalid num_devices=%d (must be 1..%d)\n",
		       num_devices, CHARDEV_MAX_MINORS);
		return -EINVAL;
	}

	if (buf_size < CHARDEV_BUF_MIN || buf_size > CHARDEV_BUF_MAX) {
		pr_err("chardev: invalid buf_size=%u\n", buf_size);
		return -EINVAL;
	}

	ret = register_chrdev_region(MKDEV(CHARDEV_MAJOR, 0),
				     num_devices, CHARDEV_NAME);
	if (ret) {
		pr_err("chardev: register_chrdev_region failed (%d)\n", ret);
		return ret;
	}
	chardev_devno = MKDEV(CHARDEV_MAJOR, 0);

	chardev_class = class_create(THIS_MODULE, CHARDEV_CLASS_NAME);
	if (IS_ERR(chardev_class)) {
		ret = PTR_ERR(chardev_class);
		pr_err("chardev: class_create failed (%d)\n", ret);
		goto err_unreg_region;
	}

	for (i = 0; i < num_devices; i++) {
		ret = chardev_init_one(&chardev_devices[i], i);
		if (ret) {
			int j;
			for (j = 0; j < i; j++)
				chardev_destroy_one(&chardev_devices[j], j);
			goto err_destroy_class;
		}
	}

	pr_info("chardev: module loaded — %d device(s), buf_size=%u\n",
		num_devices, buf_size);
	return 0;

err_destroy_class:
	class_destroy(chardev_class);
err_unreg_region:
	unregister_chrdev_region(chardev_devno, num_devices);
	return ret;
}

static void __exit chardev_module_exit(void)
{
	int i;

	for (i = 0; i < num_devices; i++)
		chardev_destroy_one(&chardev_devices[i], i);

	class_destroy(chardev_class);
	unregister_chrdev_region(chardev_devno, num_devices);
	pr_info("chardev: module unloaded\n");
}

module_init(chardev_module_init);
module_exit(chardev_module_exit);
