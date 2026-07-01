/*
 * leak_test.h — the §6A.4 leak-clean teardown harness (PLAN §7A.7).
 *
 * Runs launch→Home→relaunch cycles on every registered app and checks that the
 * free heap returns to its pre-launch baseline (a per-cycle leak shows as steadily
 * falling free heap). Built only when CONFIG_TM_LEAK_TEST is set; run from the UI
 * task (app lifecycle is UI-task-only, §6A).
 */
#pragma once

void leak_test_run(void);
