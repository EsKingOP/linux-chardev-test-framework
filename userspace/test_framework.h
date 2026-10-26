/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * test_framework.h — Minimal C unit-test framework
 *
 * Provides test registration, assertion macros, and a result summary.
 * Designed to be embedded directly into a single binary with no runtime
 * dependencies beyond libc.
 *
 * Copyright (C) 2025 Example Embedded Systems
 */

#ifndef _TEST_FRAMEWORK_H
#define _TEST_FRAMEWORK_H

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdint.h>

/* -----------------------------------------------------------------------
 * Limits
 * --------------------------------------------------------------------- */

#define TF_MAX_TESTS   64
#define TF_NAME_LEN    64

/* -----------------------------------------------------------------------
 * Test function signature
 * --------------------------------------------------------------------- */

/** A test function takes no arguments and returns 0 on pass, -1 on fail. */
typedef int (*tf_test_fn)(void);

/* -----------------------------------------------------------------------
 * Test descriptor
 * --------------------------------------------------------------------- */

struct tf_test {
	char        name[TF_NAME_LEN];
	tf_test_fn  fn;
};

/* -----------------------------------------------------------------------
 * Global test registry (defined in test_framework.c)
 * --------------------------------------------------------------------- */

extern struct tf_test tf_registry[TF_MAX_TESTS];
extern int            tf_test_count;

/* Path to the device node — set before calling tf_run_all() */
extern const char    *tf_device_path;

/* -----------------------------------------------------------------------
 * Registration
 * --------------------------------------------------------------------- */

/**
 * tf_register() - Register a test case.
 * Returns 0 on success, -1 if the registry is full.
 */
int tf_register(const char *name, tf_test_fn fn);

/**
 * tf_run_all() - Run every registered test and print a summary.
 * Returns the number of failed tests (0 = all passed).
 */
int tf_run_all(void);

/* -----------------------------------------------------------------------
 * Assertion macros
 *
 * Each macro prints a failure message with file, line, and expected vs.
 * actual values, then returns -1 from the enclosing test function.
 * --------------------------------------------------------------------- */

#define _TF_FAIL(fmt, ...) \
	do { \
		fprintf(stderr, "  FAIL %s:%d: " fmt "\n", \
			__FILE__, __LINE__, ##__VA_ARGS__); \
		return -1; \
	} while (0)

/** Assert two integer/pointer values are equal. */
#define ASSERT_EQ(actual, expected) \
	do { \
		long long _a = (long long)(actual); \
		long long _e = (long long)(expected); \
		if (_a != _e) \
			_TF_FAIL("ASSERT_EQ(%s == %s): got %lld, want %lld", \
				 #actual, #expected, _a, _e); \
	} while (0)

/** Assert two integer/pointer values are NOT equal. */
#define ASSERT_NE(actual, expected) \
	do { \
		long long _a = (long long)(actual); \
		long long _e = (long long)(expected); \
		if (_a == _e) \
			_TF_FAIL("ASSERT_NE(%s != %s): both are %lld", \
				 #actual, #expected, _a); \
	} while (0)

/** Assert an integer/pointer value is >= 0. */
#define ASSERT_GE_ZERO(val) \
	do { \
		long long _v = (long long)(val); \
		if (_v < 0) \
			_TF_FAIL("ASSERT_GE_ZERO(%s): got %lld", #val, _v); \
	} while (0)

/** Assert two NUL-terminated strings are equal. */
#define ASSERT_STR_EQ(actual, expected) \
	do { \
		if (strcmp((actual), (expected)) != 0) \
			_TF_FAIL("ASSERT_STR_EQ: \"%s\" != \"%s\"", \
				 (actual), (expected)); \
	} while (0)

/** Assert a pointer is NULL. */
#define ASSERT_NULL(ptr) \
	do { \
		if ((ptr) != NULL) \
			_TF_FAIL("ASSERT_NULL(%s): pointer is %p", \
				 #ptr, (void *)(ptr)); \
	} while (0)

/** Assert a pointer is NOT NULL. */
#define ASSERT_NOT_NULL(ptr) \
	do { \
		if ((ptr) == NULL) \
			_TF_FAIL("ASSERT_NOT_NULL(%s): pointer is NULL", #ptr); \
	} while (0)

/**
 * ASSERT_ERRNO - Assert errno equals the expected value.
 * Typically used immediately after a syscall that is expected to fail.
 */
#define ASSERT_ERRNO(expected_errno) \
	do { \
		if (errno != (expected_errno)) \
			_TF_FAIL("ASSERT_ERRNO: errno=%d (%s), want %d (%s)", \
				 errno, strerror(errno), \
				 (expected_errno), strerror(expected_errno)); \
	} while (0)

/**
 * ASSERT_TRUE — Assert a boolean condition is true.
 */
#define ASSERT_TRUE(cond) \
	do { \
		if (!(cond)) \
			_TF_FAIL("ASSERT_TRUE(%s) is false", #cond); \
	} while (0)

#endif /* _TEST_FRAMEWORK_H */
