/*
 * async_job.h — generic background work, off the UI task (PLAN §8.5 step 8, §6A.2).
 *
 * TASK-AGNOSTIC: lets an app do blocking I/O (a network fetch) without spawning
 * its own task. `work` runs on a core-owned worker; `done` runs back on the UI
 * task. Apps stay single-threaded (§6A.2), so their models need no mutex.
 *
 * Safety (the §6A teardown guarantee):
 *  - `work` must NOT touch app memory. At submit, core **copies** your context
 *    (`ctx`, `ctx_size`) into a core-owned buffer; both `work` and `done` get a
 *    pointer to that copy. So even if the app is torn down mid-fetch, the worker
 *    only ever touches the core copy — no use-after-free.
 *  - Cancellation is **cooperative**: `async_job_cancel()` (call it from the app's
 *    exit()) sets a flag; `work` must poll `async_job_cancelled()` in its loops and
 *    bail. It never vTaskDeletes a worker mid-perform() (which would orphan the
 *    socket + TLS session). A cancelled job's `done` is **not** called, and because
 *    `work` only touches the core-owned ctx copy, a cancelled worker that keeps
 *    running until its I/O times out never writes into freed app state.
 *  - **Do NOT tear down a non-thread-safe handle from the abort hook.** The abort
 *    hook (async_job_on_cancel) fires on the UI task; calling e.g.
 *    esp_http_client_close() on a handle the worker is simultaneously inside
 *    (read/perform) is a data race that CRASHES (esp_http_client isn't thread-safe,
 *    step 13). Keep the client handle worker-local and cancel via the flag; reserve
 *    the abort hook for genuinely thread-safe signals only.
 *
 * Single worker: one job at a time; submit returns NULL if busy.
 */
#pragma once

#include <stddef.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#define ASYNC_CTX_MAX     8192   /* max bytes copied per job */
#define ASYNC_WORKER_STACK 6144
#define ASYNC_WORKER_PRIO  4

typedef struct async_job async_job_t;

/* Runs on the WORKER. Read inputs from `ctx`, write results into `ctx`. Keep any
 * client handle worker-local and poll async_job_cancelled() in long loops (do not
 * tear a non-thread-safe handle down from the UI thread). Return true on success. */
typedef bool (*async_work_fn)(async_job_t *job, void *ctx);

/* Runs on the UI task after `work` finishes — skipped if the job was cancelled.
 * Read results from `ctx`; `ok` is `work`'s return. */
typedef void (*async_done_fn)(void *ctx, bool ok);

/* Boot: create the worker; `ui_queue` is the UI task's input queue (results are
 * delivered as EV_SYS_JOB_DONE). Call once. */
void async_job_init(QueueHandle_t ui_queue);

/* Called by the UI task when it dequeues EV_SYS_JOB_DONE (runs done + frees). */
void async_job_deliver(void);

/* Submit. Copies `ctx_size` (≤ ASYNC_CTX_MAX) bytes of `ctx`. Returns a handle,
 * or NULL if the worker is busy / ctx too big / OOM. */
async_job_t *async_job_submit(async_work_fn work, async_done_fn done,
                              const void *ctx, size_t ctx_size);

/* Request cancel (UI task, e.g. from exit()): flag + abort hook; non-blocking;
 * `done` won't run. Safe even if the job already finished. */
void async_job_cancel(async_job_t *job);

/* ── Worker-side (call from inside `work`) ── */
bool async_job_cancelled(const async_job_t *job);
void async_job_on_cancel(async_job_t *job, void (*abort_fn)(void *), void *abort_ctx);
