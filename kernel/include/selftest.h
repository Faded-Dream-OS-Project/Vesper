/* SPDX-License-Identifier: Proprietary */
/* Copyright (c) Faded Dream OS Project. All rights reserved. */
/* Source is viewable but may not be copied, modified, or redistributed
   without permission. */

#ifndef VESPER_SELFTEST_H
#define VESPER_SELFTEST_H

#include <common.h>

/* Minimal boot-time self-test framework: PASS/FAIL per test case,
   printed over serial (and framebuffer). */

struct test_ctx {
    u64 passed;
    u64 failed;
};

void test_begin(struct test_ctx *t, const char *suite);
void test_check(struct test_ctx *t, const char *name, int cond);
void test_summary(struct test_ctx *t);

#endif /* VESPER_SELFTEST_H */
