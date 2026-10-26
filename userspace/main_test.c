// SPDX-License-Identifier: GPL-2.0-only
/*
 * main_test.c — Main test runner for linux-chardev-test-framework
 *
 * Registers all 45 test cases across four suites and runs them in order.
 * Exits with status 0 if all pass, or the number of failed tests otherwise.
 *
 * Usage:
 *   ./main_test [/dev/chardev0]
 *
 * Copyright (C) 2025 Example Embedded Systems
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "test_framework.h"

/* Forward declarations of per-suite registration functions */
void register_basic_rw_tests(void);
void register_ioctl_tests(void);
void register_concurrent_tests(void);
void register_i2c_eeprom_tests(void);

int main(int argc, char *argv[])
{
	/* Allow overriding the device path on the command line */
	if (argc >= 2)
		tf_device_path = argv[1];

	printf("linux-chardev-test-framework\n");
	printf("Device: %s\n", tf_device_path);
	printf("\n");

	/*
	 * Register all test suites.
	 * Order matters: basic_rw first so that a broken device is caught
	 * early before the more complex suites run.
	 */
	register_basic_rw_tests();     /* 15 tests */
	register_ioctl_tests();        /* 10 tests */
	register_concurrent_tests();   /* 10 tests */
	register_i2c_eeprom_tests();   /* 10 tests */

	printf("Total registered: %d test(s)\n", tf_test_count);

	int failures = tf_run_all();

	return (failures > 0) ? failures : 0;
}
