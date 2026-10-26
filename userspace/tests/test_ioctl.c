// SPDX-License-Identifier: GPL-2.0-only
/*
 * test_ioctl.c — ioctl interface tests for the chardev character device
 *
 * Suite: test_ioctl (10 test cases)
 *
 * Tests:
 *  01. ioctl_flush                  — CHARDEV_IOC_FLUSH clears pending data
 *  02. ioctl_get_stats_initial      — GET_STATS returns sane initial values
 *  03. ioctl_get_stats_after_write  — bytes_written increments correctly
 *  04. ioctl_get_stats_after_read   — bytes_read increments correctly
 *  05. ioctl_set_bufsize_valid      — SET_BUFSIZE to 1024 succeeds
 *  06. ioctl_set_bufsize_too_small  — SET_BUFSIZE below minimum returns EINVAL
 *  07. ioctl_set_bufsize_too_large  — SET_BUFSIZE above maximum returns EINVAL
 *  08. ioctl_set_bufsize_busy       — SET_BUFSIZE with pending data returns EBUSY
 *  09. ioctl_reset                  — RESET clears stats and buffer
 *  10. ioctl_invalid_cmd            — Unknown ioctl number returns ENOTTY
 *
 * Copyright (C) 2025 Example Embedded Systems
 */

#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>
#include <stdint.h>

#include "../test_framework.h"

/* Pull in the ioctl definitions from the driver header.
 * On the build host the kernel headers are not available, so we replicate
 * the ioctl magic inline using the same user-visible structs. */
#include <linux/ioctl.h>

#define CHARDEV_IOC_MAGIC       'C'
#define CHARDEV_BUF_MIN         256
#define CHARDEV_BUF_MAX         65536

struct chardev_stats {
	uint64_t bytes_written;
	uint64_t bytes_read;
	uint32_t overruns;
	uint32_t buf_size;
	uint32_t buf_used;
	uint32_t _pad;
};

#define CHARDEV_IOC_FLUSH       _IO(CHARDEV_IOC_MAGIC,  0)
#define CHARDEV_IOC_GET_STATS   _IOR(CHARDEV_IOC_MAGIC, 1, struct chardev_stats)
#define CHARDEV_IOC_SET_BUFSIZE _IOW(CHARDEV_IOC_MAGIC, 2, uint32_t)
#define CHARDEV_IOC_RESET       _IO(CHARDEV_IOC_MAGIC,  3)

/* -----------------------------------------------------------------------
 * Helpers
 * --------------------------------------------------------------------- */

static int open_dev(void) { return open(tf_device_path, O_RDWR); }

static void drain_device(int fd)
{
	char tmp[256];
	while (read(fd, tmp, sizeof(tmp)) > 0)
		;
}

/* -----------------------------------------------------------------------
 * Tests
 * --------------------------------------------------------------------- */

static int test_ioctl_flush(void)
{
	int fd = open_dev();
	ASSERT_GE_ZERO(fd);

	const char msg[] = "data to be flushed";
	ASSERT_GE_ZERO((int)write(fd, msg, sizeof(msg)));

	ASSERT_EQ(ioctl(fd, CHARDEV_IOC_FLUSH), 0);

	/* Buffer should now be empty */
	char rbuf[64];
	ASSERT_EQ(read(fd, rbuf, sizeof(rbuf)), 0);

	close(fd);
	return 0;
}

static int test_ioctl_get_stats_initial(void)
{
	int fd = open_dev();
	ASSERT_GE_ZERO(fd);

	/* Reset device so stats are at baseline */
	ASSERT_EQ(ioctl(fd, CHARDEV_IOC_RESET), 0);

	struct chardev_stats stats;
	memset(&stats, 0xFF, sizeof(stats));
	ASSERT_EQ(ioctl(fd, CHARDEV_IOC_GET_STATS, &stats), 0);

	ASSERT_EQ(stats.bytes_written, 0ULL);
	ASSERT_EQ(stats.bytes_read,    0ULL);
	ASSERT_EQ(stats.overruns,      0U);
	ASSERT_TRUE(stats.buf_size >= CHARDEV_BUF_MIN);
	ASSERT_EQ(stats.buf_used, 0U);

	close(fd);
	return 0;
}

static int test_ioctl_get_stats_after_write(void)
{
	int fd = open_dev();
	ASSERT_GE_ZERO(fd);
	ASSERT_EQ(ioctl(fd, CHARDEV_IOC_RESET), 0);

	char buf[128];
	memset(buf, 'W', sizeof(buf));
	ssize_t nw = write(fd, buf, sizeof(buf));
	ASSERT_GE_ZERO((int)nw);

	struct chardev_stats stats;
	ASSERT_EQ(ioctl(fd, CHARDEV_IOC_GET_STATS, &stats), 0);
	ASSERT_EQ(stats.bytes_written, (uint64_t)nw);
	ASSERT_EQ(stats.buf_used, (uint32_t)nw);

	close(fd);
	return 0;
}

static int test_ioctl_get_stats_after_read(void)
{
	int fd = open_dev();
	ASSERT_GE_ZERO(fd);
	ASSERT_EQ(ioctl(fd, CHARDEV_IOC_RESET), 0);

	char wbuf[64];
	memset(wbuf, 'R', sizeof(wbuf));
	ASSERT_EQ(write(fd, wbuf, sizeof(wbuf)), (ssize_t)sizeof(wbuf));

	char rbuf[64];
	ssize_t nr = read(fd, rbuf, sizeof(rbuf));
	ASSERT_GE_ZERO((int)nr);

	struct chardev_stats stats;
	ASSERT_EQ(ioctl(fd, CHARDEV_IOC_GET_STATS, &stats), 0);
	ASSERT_EQ(stats.bytes_read, (uint64_t)nr);

	close(fd);
	return 0;
}

static int test_ioctl_set_bufsize_valid(void)
{
	int fd = open_dev();
	ASSERT_GE_ZERO(fd);
	ASSERT_EQ(ioctl(fd, CHARDEV_IOC_FLUSH), 0);

	uint32_t new_size = 1024;
	ASSERT_EQ(ioctl(fd, CHARDEV_IOC_SET_BUFSIZE, &new_size), 0);

	struct chardev_stats stats;
	ASSERT_EQ(ioctl(fd, CHARDEV_IOC_GET_STATS, &stats), 0);
	ASSERT_EQ(stats.buf_size, new_size);

	/* Restore default */
	uint32_t restore = 4096;
	ioctl(fd, CHARDEV_IOC_SET_BUFSIZE, &restore);

	close(fd);
	return 0;
}

static int test_ioctl_set_bufsize_too_small(void)
{
	int fd = open_dev();
	ASSERT_GE_ZERO(fd);
	ASSERT_EQ(ioctl(fd, CHARDEV_IOC_FLUSH), 0);

	uint32_t bad_size = CHARDEV_BUF_MIN - 1;
	int ret = ioctl(fd, CHARDEV_IOC_SET_BUFSIZE, &bad_size);
	ASSERT_EQ(ret, -1);
	ASSERT_ERRNO(EINVAL);

	close(fd);
	return 0;
}

static int test_ioctl_set_bufsize_too_large(void)
{
	int fd = open_dev();
	ASSERT_GE_ZERO(fd);
	ASSERT_EQ(ioctl(fd, CHARDEV_IOC_FLUSH), 0);

	uint32_t bad_size = CHARDEV_BUF_MAX + 1;
	int ret = ioctl(fd, CHARDEV_IOC_SET_BUFSIZE, &bad_size);
	ASSERT_EQ(ret, -1);
	ASSERT_ERRNO(EINVAL);

	close(fd);
	return 0;
}

static int test_ioctl_set_bufsize_busy(void)
{
	int fd = open_dev();
	ASSERT_GE_ZERO(fd);
	ASSERT_EQ(ioctl(fd, CHARDEV_IOC_FLUSH), 0);

	/* Write some data to make the buffer non-empty */
	const char msg[] = "pending data";
	ASSERT_GE_ZERO((int)write(fd, msg, sizeof(msg)));

	uint32_t new_size = 2048;
	int ret = ioctl(fd, CHARDEV_IOC_SET_BUFSIZE, &new_size);
	ASSERT_EQ(ret, -1);
	ASSERT_ERRNO(EBUSY);

	close(fd);
	return 0;
}

static int test_ioctl_reset(void)
{
	int fd = open_dev();
	ASSERT_GE_ZERO(fd);

	/* Write something so stats are non-zero */
	char buf[32];
	memset(buf, 0, sizeof(buf));
	ASSERT_GE_ZERO((int)write(fd, buf, sizeof(buf)));

	ASSERT_EQ(ioctl(fd, CHARDEV_IOC_RESET), 0);

	struct chardev_stats stats;
	ASSERT_EQ(ioctl(fd, CHARDEV_IOC_GET_STATS, &stats), 0);
	ASSERT_EQ(stats.bytes_written, 0ULL);
	ASSERT_EQ(stats.bytes_read,    0ULL);
	ASSERT_EQ(stats.buf_used,      0U);

	close(fd);
	return 0;
}

static int test_ioctl_invalid_cmd(void)
{
	int fd = open_dev();
	ASSERT_GE_ZERO(fd);

	/* Construct an ioctl number with the right magic but an invalid nr */
	unsigned int bad_cmd = _IO(CHARDEV_IOC_MAGIC, 99);
	int ret = ioctl(fd, bad_cmd);
	ASSERT_EQ(ret, -1);
	ASSERT_ERRNO(ENOTTY);

	close(fd);
	return 0;
}

/* -----------------------------------------------------------------------
 * Registration
 * --------------------------------------------------------------------- */

void register_ioctl_tests(void)
{
	tf_register("ioctl_flush",                 test_ioctl_flush);
	tf_register("ioctl_get_stats_initial",     test_ioctl_get_stats_initial);
	tf_register("ioctl_get_stats_after_write", test_ioctl_get_stats_after_write);
	tf_register("ioctl_get_stats_after_read",  test_ioctl_get_stats_after_read);
	tf_register("ioctl_set_bufsize_valid",     test_ioctl_set_bufsize_valid);
	tf_register("ioctl_set_bufsize_too_small", test_ioctl_set_bufsize_too_small);
	tf_register("ioctl_set_bufsize_too_large", test_ioctl_set_bufsize_too_large);
	tf_register("ioctl_set_bufsize_busy",      test_ioctl_set_bufsize_busy);
	tf_register("ioctl_reset",                 test_ioctl_reset);
	tf_register("ioctl_invalid_cmd",           test_ioctl_invalid_cmd);
}
