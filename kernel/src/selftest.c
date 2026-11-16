/* SPDX-License-Identifier: Proprietary */
/* Copyright (c) Faded Dream OS Project. All rights reserved.
   Source is viewable but may not be copied, modified, or redistributed
   without permission. */

#include <console.h>
#include <lib.h>
#include <selftest.h>

/* Self-test output: colourised on the framebuffer ($P/$F/$T tags are
   stripped on serial), so PASS lines are green and FAIL lines are red
   on real-hardware photographs.

   Session 8: a strict registry. Every suite registers its name at
   test_begin; test_summary records PASS/FAIL. A suite whose harness
   never returns (kernel panic inside the suite) stays OPEN — the
   panic path closes it as FAIL, and test_strict_summary() at the end
   of boot lists every suite as PASS / FAIL / NOT RUN so a photo (or a
   serial log) shows exactly what ran and what did not. */

enum suite_state {
    SUITE_UNUSED = 0,
    SUITE_OPEN,
    SUITE_PASS,
    SUITE_FAIL,
    SUITE_NOTRUN,
};

static char suite_names[TEST_MAX_SUITES][TEST_NAME_MAX];
static u8 suite_states[TEST_MAX_SUITES];
static int open_suite;   /* index of the currently-open suite, -1 none */
static int registered;   /* total registered suites */

void test_begin(struct test_ctx *t, const char *suite)
{
    t->passed = 0;
    t->failed = 0;
    t->suite_idx = -1;

    if (registered < TEST_MAX_SUITES) {
        strncpy(suite_names[registered], suite, TEST_NAME_MAX - 1);
        suite_names[registered][TEST_NAME_MAX - 1] = 0;
        suite_states[registered] = SUITE_OPEN;
        t->suite_idx = registered;
        open_suite = registered;
        registered++;
    }
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
    if (t->suite_idx >= 0 && t->suite_idx < TEST_MAX_SUITES)
        suite_states[t->suite_idx] =
            t->failed ? SUITE_FAIL : SUITE_PASS;
    if (open_suite == t->suite_idx)
        open_suite = -1;

    if (t->failed)
        kprintf("$Fselftest: %lu passed, %lu failed -> FAIL$T\n",
                t->passed, t->failed);
    else
        kprintf("$Pselftest: %lu passed, %lu failed -> ALL PASS$T\n",
                t->passed, t->failed);
}

/* Panic path: a kernel panic while a suite is OPEN means that suite
   killed the kernel — it is a FAIL, not "inconclusive". */
void test_panic_close_open(void)
{
    if (open_suite >= 0 && open_suite < TEST_MAX_SUITES) {
        suite_states[open_suite] = SUITE_FAIL;
        open_suite = -1;
    }
}

int open_suite_idx(void)
{
    return open_suite;
}

const char *open_suite_name(void)
{
    if (open_suite >= 0 && open_suite < TEST_MAX_SUITES)
        return suite_names[open_suite];
    return "";
}

/* Strict end-of-boot summary. Suites after the last one that ran are
   NOT RUN: a photo shows exactly how far boot got. */
void test_strict_summary(void)
{
    kprintf("$I=== boot test summary ===$T\n");
    int npass = 0, nfail = 0, nnot = 0;
    for (int i = 0; i < registered; i++) {
        const char *state = "NOT RUN";
        const char *tag = "$W";
        switch (suite_states[i]) {
        case SUITE_PASS:  state = "PASS";    tag = "$P"; npass++; break;
        case SUITE_FAIL:  state = "FAIL";    tag = "$F"; nfail++; break;
        case SUITE_OPEN:  state = "FAIL(open)"; tag = "$F"; nfail++; break;
        case SUITE_NOTRUN:
        default:          state = "NOT RUN"; tag = "$W"; nnot++; break;
        }
        /* kprintf has no %-24s: pad the name manually so the table
           aligns in the serial log and on screen. */
        kprintf("  %s", tag);
        kprintf("%s", suite_names[i]);
        int pad = TEST_NAME_MAX - 2 - (int)strlen(suite_names[i]);
        for (int j = 0; j < pad; j++)
            kputchar(' ');
        kprintf("%s$T\n", state);
    }
    kprintf("suites: %d pass, %d fail, %d not run -> %s\n",
            npass, nfail, nnot,
            (nfail == 0 && nnot == 0) ? "ALL PASS" : "INCOMPLETE");
}

