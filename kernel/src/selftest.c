/* SPDX-License-Identifier: Proprietary */
/* Copyright (c) Faded Dream OS Project. All rights reserved. */
/* Source is viewable but may not be copied, modified, or redistributed
   without permission. */

#include <console.h>
#include <selftest.h>

void test_begin(struct test_ctx *t, const char *suite)
{
    t->passed = 0;
    t->failed = 0;
    kprintf("selftest: %s\n", suite);
}

void test_check(struct test_ctx *t, const char *name, int cond)
{
    if (cond) {
        t->passed++;
        kprintf("  PASS %s\n", name);
    } else {
        t->failed++;
        kprintf("  FAIL %s\n", name);
    }
}

void test_summary(struct test_ctx *t)
{
    kprintf("selftest: %lu passed, %lu failed -> %s\n",
            t->passed, t->failed, t->failed ? "FAIL" : "ALL PASS");
}
