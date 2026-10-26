// SPDX-License-Identifier: GPL-2.0-only
/*
 * test_framework.c — Test runner implementation
 *
 * Copyright (C) 2025 Example Embedded Systems
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "test_framework.h"

/* -----------------------------------------------------------------------
 * Global state
 * --------------------------------------------------------------------- */

struct tf_test tf_registry[TF_MAX_TESTS];
int            tf_test_count = 0;
const char    *tf_device_path = "/dev/chardev0";

/* -----------------------------------------------------------------------
 * tf_register
 * --------------------------------------------------------------------- */

int tf_register(const char *name, tf_test_fn fn)
{
	if (tf_test_count >= TF_MAX_TESTS) {
		fprintf(stderr, "test_framework: registry full (max %d)\n",
			TF_MAX_TESTS);
		return -1;
	}
	strncpy(tf_registry[tf_test_count].name, name, TF_NAME_LEN - 1);
	tf_registry[tf_test_count].name[TF_NAME_LEN - 1] = '\0';
	tf_registry[tf_test_count].fn = fn;
	tf_test_count++;
	return 0;
}

/* -----------------------------------------------------------------------
 * tf_run_all
 * --------------------------------------------------------------------- */

int tf_run_all(void)
{
	int passed = 0, failed = 0;
	int i;

	printf("\n");
	printf("=============================================================\n");
	printf("  linux-chardev-test-framework  |  device: %s\n", tf_device_path);
	printf("  Running %d test(s)\n", tf_test_count);
	printf("=============================================================\n");

	for (i = 0; i < tf_test_count; i++) {
		struct tf_test *t = &tf_registry[i];
		int result;

		printf("  [ RUN  ] %-52s", t->name);
		fflush(stdout);

		result = t->fn();

		if (result == 0) {
			printf("[ PASS ]\n");
			passed++;
		} else {
			printf("[ FAIL ]\n");
			failed++;
		}
	}

	printf("-------------------------------------------------------------\n");
	printf("  Results: %d passed, %d failed, %d total\n",
	       passed, failed, tf_test_count);
	printf("=============================================================\n\n");

	return failed;
}
