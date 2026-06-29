/*
 * task_model.h — the single shared model (PLAN §5.2).
 *
 * Phase 1 stub: the *ownership boundary* is real (one mutex; the network task is
 * the sole writer in Phase 3, the UI task the sole reader) even though there's no
 * network yet. Readers copy the status out under the lock and render outside it —
 * the pattern is built right from the start rather than retrofitted (§5.2).
 */
#pragma once

typedef enum {
    SYNC_IDLE = 0,
    SYNC_CONNECTING,
    SYNC_SYNCING,
    SYNC_OK,
    SYNC_ERROR,
} sync_state_t;

typedef struct {
    sync_state_t sync;       /* current task-sync phase (link state lives in net_status.h) */
    unsigned     task_count; /* tasks held (stub: 0 until Phase 3)                         */
} task_status_t;

/* Create the mutex. Call once at boot before any task touches the model. */
void task_model_init(void);

/* Reader (UI task): copy the status out under the lock. */
void task_model_get(task_status_t *out);

/* Writer (network task, Phase 3): replace the status under the lock. */
void task_model_set(const task_status_t *in);

/* Short label for the sync state — for the status bar. */
const char *sync_state_label(sync_state_t s);
