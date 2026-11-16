/* SPDX-License-Identifier: Proprietary */
/* Copyright (c) Faded Dream OS Project. All rights reserved.
   Source is viewable but may not be copied, modified, or redistributed
   without permission. */

#ifndef VESPER_SELFTEST_H
#define VESPER_SELFTEST_H

#include <common.h>

/* Minimal boot-time self-test framework: PASS/FAIL per test case,
   printed over serial (and framebuffer).

   Session 8: a registry of suite NAMES tracks what ran, so the final
   summary can be strict — every suite is PASS, FAIL, or NOT RUN, and
   a kernel panic inside a suite marks that suite FAIL (the harness
   never gets to print its own summary, but the panic handler closes
   any open suite before dying). */

#define TEST_MAX_SUITES 16
#define TEST_NAME_MAX   24

struct test_ctx {
    u64 passed;
    u64 failed;
    int suite_idx;   /* index into the registry, -1 = unregistered */
};

void test_begin(struct test_ctx *t, const char *suite);
void test_check(struct test_ctx *t, const char *name, int cond);
void test_summary(struct test_ctx *t);

/* Strict boot summary: every registered suite as PASS/FAIL/NOT RUN,
   followed by a one-line verdict. Panic closes any OPEN suite (marks
   it FAIL) before the panic banner prints. */
void test_strict_summary(void);
void test_panic_close_open(void);   /* called from panic() */

/* Name/index of the currently-open suite (-1 = none); panic prints
   which suite was running when the kernel died. */
int open_suite_idx(void);
const char *open_suite_name(void);

#endif /* VESPER_SELFTEST_H */
