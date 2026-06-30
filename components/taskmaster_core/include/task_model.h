/*
 * task_model.h — the single shared task model (PLAN §5.2, §8.1, §8.5 step 5).
 *
 * Holds a *fixed* array of tasks (no per-task malloc, §6A.1), filled by the sync
 * task (the sole writer) from the device REST contract and read by the UI task.
 * One mutex; writers replace state under it, readers copy out under it and render
 * outside it (§5.2).
 *
 * Device contract (every source — yapp-server or a LAN box — exposes it, §8.1):
 *   GET  /tasks  → { "etag": "...",
 *                    "tasks": [ { "id", "title"(≤TASK_TITLE_MAX),
 *                                 "priority"(TASK_PRIO_MIN..TASK_PRIO_MAX),
 *                                 "due", "parent_id", "done" }, ... ] }
 *   POST /tasks/{id}/complete                       → mark complete
 *   POST /tasks/{id}/postpone  { "due": "tomorrow" }→ reschedule (501 if unsupported)
 *   GET  /health                                    → liveness
 * Rules: flat JSON, server-truncated titles, ≤TASKMASTER_MAX_TASKS tasks, priority
 * normalized so TASK_PRIO_MAX = highest (matches Todoist P1). `etag` lets the device
 * skip re-parsing/re-rendering an unchanged list.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

/* Fixed bounds (no magic numbers; §6A.1 keeps the model a fixed array). */
#define TASKMASTER_MAX_TASKS 50   /* cap on tasks held on-device */
#define TASK_ID_MAX          24   /* task / parent id string (incl. NUL) */
#define TASK_TITLE_MAX       48   /* title string (server-truncated) */
#define TASK_DUE_MAX         16   /* due date/label string */
#define TASK_ETAG_MAX        33   /* list etag string */
#define TASK_PRIO_MIN        1    /* lowest priority */
#define TASK_PRIO_MAX        4    /* highest priority (Todoist P1) */

typedef struct {
    char    id[TASK_ID_MAX];
    char    parent_id[TASK_ID_MAX];   /* empty = top-level */
    char    title[TASK_TITLE_MAX];
    char    due[TASK_DUE_MAX];         /* empty = no due date */
    uint8_t priority;                  /* TASK_PRIO_MIN..TASK_PRIO_MAX */
    bool    done;
} task_t;

typedef enum {
    SYNC_IDLE = 0,
    SYNC_CONNECTING,
    SYNC_SYNCING,
    SYNC_OK,
    SYNC_ERROR,
} sync_state_t;

typedef struct {
    sync_state_t sync;       /* current task-sync phase (link state lives in net_status.h) */
    unsigned     task_count; /* tasks currently held */
} task_status_t;

/* Create the mutex. Call once at boot before any task touches the model. */
void task_model_init(void);

/* ── Reader (UI task) ── */
void     task_model_get(task_status_t *out);                 /* sync phase + count */
unsigned task_model_copy(task_t *out, unsigned max);         /* copy ≤max tasks, returns count */

/* ── Writer (sync task, §5.2) ── */
void task_model_set_sync(sync_state_t s);                    /* update the sync phase only */
void task_model_set_tasks(const task_t *in, unsigned count); /* replace the task list (capped) */

/* Short label for the sync state — for the inline indicator. */
const char *sync_state_label(sync_state_t s);
