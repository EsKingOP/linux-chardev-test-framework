// SPDX-License-Identifier: GPL-2.0-only
/*
 * test_basic_rw.c — Basic read/write tests for the chardev character device
 *
 * Suite: test_basic_rw (15 test cases)
 *
 * Tests:
 *  01. open_device_success        — open /dev/chardev0, expect fd >= 0
 *  02. close_device_success       — close a valid fd, expect 0
 *  03. write_small_payload        — write 16 bytes, expect 16 returned
 *  04. read_back_small_payload    — read back what was written, byte-equal
 *  05. write_then_read_exact      — write N bytes, read N bytes exactly
 *  06. read_empty_buffer          — read from empty device returns 0
 *  07. write_zero_bytes           — write(fd, buf, 0) returns 0
 *  08. read_zero_bytes            — read(fd, buf, 0) returns 0
 *  09. write_max_payload          — write CHARDEV_BUF_DEFAULT-1 bytes
 *  10. read_partial               — read fewer bytes than available
 *  11. write_invalid_fd           — write to fd=-1 returns -1 / EBADF
 *  12. read_invalid_fd            — read from fd=-1 returns -1 / EBADF
 *  13. write_null_buf             — write with NULL buffer returns EFAULT
 *  14. read_null_buf              — read with NULL buffer returns EFAULT
 *  15. open_nonexistent_device    — open /dev/chardev_NOEXIST returns ENOENT
 *
 * Copyright (C) 2025 Example Embedded Systems
 */

#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <stdio.h>

#include "../test_framework.h"

/* -----------------------------------------------------------------------
 * Helpers
 * --------------------------------------------------------------------- */

static int open_dev(void)
{
	return open(tf_device_path, O_RDWR);
}

/* Flush the device buffer by reading until empty */
static void drain_device(int fd)
{
	char tmp[256];
	while (read(fd, tmp, sizeof(tmp)) > 0)
		;
}

/* -----------------------------------------------------------------------
 * Test implementations
 * --------------------------------------------------------------------- */

static int test_open_device_success(void)
{
	int fd = open_dev();
	ASSERT_GE_ZERO(fd);
	close(fd);
	return 0;
}

static int test_close_device_success(void)
{
	int fd = open_dev();
	ASSERT_GE_ZERO(fd);
	int ret = close(fd);
	ASSERT_EQ(ret, 0);
	return 0;
}

static int test_write_small_payload(void)
{
	int fd = open_dev();
	ASSERT_GE_ZERO(fd);
	drain_device(fd);

	const char msg[] = "hello chardev!";
	ssize_t n = write(fd, msg, sizeof(msg));
	ASSERT_EQ(n, (ssize_t)sizeof(msg));

	close(fd);
	return 0;
}

static int test_read_back_small_payload(void)
{
	int fd = open_dev();
	ASSERT_GE_ZERO(fd);
	drain_device(fd);

	const char msg[] = "hello chardev!";
	ssize_t nw = write(fd, msg, sizeof(msg));
	ASSERT_EQ(nw, (ssize_t)sizeof(msg));

	char rbuf[sizeof(msg)] = {0};
	ssize_t nr = read(fd, rbuf, sizeof(rbuf));
	ASSERT_EQ(nr, (ssize_t)sizeof(msg));
	ASSERT_EQ(memcmp(rbuf, msg, sizeof(msg)), 0);

	close(fd);
	return 0;
}

static int test_write_then_read_exact(void)
{
	int fd = open_dev();
	ASSERT_GE_ZERO(fd);
	drain_device(fd);

	/* 64-byte payload */
	char wbuf[64], rbuf[64];
	for (int i = 0; i < 64; i++)
		wbuf[i] = (char)(i & 0xFF);

	ASSERT_EQ(write(fd, wbuf, 64), 64);
	ASSERT_EQ(read(fd, rbuf, 64), 64);
	ASSERT_EQ(memcmp(wbuf, rbuf, 64), 0);

	close(fd);
	return 0;
}

static int test_read_empty_buffer(void)
{
	int fd = open_dev();
	ASSERT_GE_ZERO(fd);
	drain_device(fd);

	char rbuf[32];
	ssize_t nr = read(fd, rbuf, sizeof(rbuf));
	ASSERT_EQ(nr, 0);

	close(fd);
	return 0;
}

static int test_write_zero_bytes(void)
{
	int fd = open_dev();
	ASSERT_GE_ZERO(fd);

	char dummy = 'X';
	ASSERT_EQ(write(fd, &dummy, 0), 0);

	close(fd);
	return 0;
}

static int test_read_zero_bytes(void)
{
	int fd = open_dev();
	ASSERT_GE_ZERO(fd);

	char dummy;
	ASSERT_EQ(read(fd, &dummy, 0), 0);

	close(fd);
	return 0;
}

static int test_write_max_payload(void)
{
	/* Write CHARDEV_BUF_DEFAULT - 1 bytes (4095 bytes for a 4096-byte buf) */
	const size_t N = 4095;
	int fd = open_dev();
	ASSERT_GE_ZERO(fd);
	drain_device(fd);

	char *wbuf = malloc(N);
	ASSERT_NOT_NULL(wbuf);
	memset(wbuf, 0xAB, N);

	ssize_t nw = write(fd, wbuf, N);
	/* Either all bytes written or at most N (driver may enforce buf limit) */
	ASSERT_TRUE(nw > 0 && (size_t)nw <= N);

	free(wbuf);
	close(fd);
	return 0;
}

static int test_read_partial(void)
{
	int fd = open_dev();
	ASSERT_GE_ZERO(fd);
	drain_device(fd);

	const char msg[] = "ABCDEFGHIJKLMNOP"; /* 17 bytes including NUL */
	ASSERT_EQ(write(fd, msg, sizeof(msg)), (ssize_t)sizeof(msg));

	/* Read only the first 4 bytes */
	char rbuf[4] = {0};
	ASSERT_EQ(read(fd, rbuf, 4), 4);
	ASSERT_EQ(memcmp(rbuf, "ABCD", 4), 0);

	/* Remaining bytes still in buffer */
	char rest[64] = {0};
	ssize_t nr = read(fd, rest, sizeof(rest));
	ASSERT_TRUE(nr > 0);

	close(fd);
	return 0;
}

static int test_write_invalid_fd(void)
{
	char buf[] = "data";
	ssize_t ret = write(-1, buf, sizeof(buf));
	ASSERT_EQ(ret, -1);
	ASSERT_ERRNO(EBADF);
	return 0;
}

static int test_read_invalid_fd(void)
{
	char buf[16];
	ssize_t ret = read(-1, buf, sizeof(buf));
	ASSERT_EQ(ret, -1);
	ASSERT_ERRNO(EBADF);
	return 0;
}

static int test_write_null_buf(void)
{
	int fd = open_dev();
	ASSERT_GE_ZERO(fd);

	ssize_t ret = write(fd, NULL, 8);
	ASSERT_EQ(ret, -1);
	ASSERT_ERRNO(EFAULT);

	close(fd);
	return 0;
}

static int test_read_null_buf(void)
{
	int fd = open_dev();
	ASSERT_GE_ZERO(fd);

	ssize_t ret = read(fd, NULL, 8);
	ASSERT_EQ(ret, -1);
	ASSERT_ERRNO(EFAULT);

	close(fd);
	return 0;
}

static int test_open_nonexistent_device(void)
{
	int fd = open("/dev/chardev_NOEXIST", O_RDWR);
	ASSERT_EQ(fd, -1);
	ASSERT_ERRNO(ENOENT);
	return 0;
}

/* -----------------------------------------------------------------------
 * Registration
 * --------------------------------------------------------------------- */

void register_basic_rw_tests(void)
{
	tf_register("open_device_success",     test_open_device_success);
	tf_register("close_device_success",    test_close_device_success);
	tf_register("write_small_payload",     test_write_small_payload);
	tf_register("read_back_small_payload", test_read_back_small_payload);
	tf_register("write_then_read_exact",   test_write_then_read_exact);
	tf_register("read_empty_buffer",       test_read_empty_buffer);
	tf_register("write_zero_bytes",        test_write_zero_bytes);
	tf_register("read_zero_bytes",         test_read_zero_bytes);
	tf_register("write_max_payload",       test_write_max_payload);
	tf_register("read_partial",            test_read_partial);
	tf_register("write_invalid_fd",        test_write_invalid_fd);
	tf_register("read_invalid_fd",         test_read_invalid_fd);
	tf_register("write_null_buf",          test_write_null_buf);
	tf_register("read_null_buf",           test_read_null_buf);
	tf_register("open_nonexistent_device", test_open_nonexistent_device);
}
