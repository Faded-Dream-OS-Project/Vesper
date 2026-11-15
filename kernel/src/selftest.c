/* SPDX-License-Identifier: Proprietary */
/* Copyright (c) Faded Dream OS Project. All rights reserved.
   Source is viewable but may not be copied, modified, or redistributed
   without permission. */

#include <console.h>
#include <selftest.h>

/* Self-test output: colourised on the framebuffer ($P/$F/$T tags are
   stripped on serial), so PASS lines are green and FAIL lines are red
   on real-hardware photographs. */

void test_begin(struct test_ctx *t, const char *suite)
{
    t->passed = 0;
    t->failed = 0;
    kprintf("$Iselftest:$T %s\n", suite);
}

void test_check(struct test_ctx *t, const char *name, int cond)
{
    if (cond) {
        t->passed++;
        kprintf("  $PPASS$T %s\n", name);
    } else {
        t->failed++;
        kprintf("  $FFAIL$T %s\n", name);
    }
}

void test_summary(struct test_ctx *t)
{
    if (t->failed)
        kprintf("$Fselftest: %lu passed, %lu failed -> FAIL$T\n",
                t->passed, t->failed);
    else
        kprintf("$Pselftest: %lu passed, %lu failed -> ALL PASS$T\n",
                t->passed, t->failed);
}
