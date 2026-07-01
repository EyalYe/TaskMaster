/*
 * async_job.c — single-worker background job service. See async_job.h.
 *
 * Lifecycle (one job at a time), with the UI task as the sole owner of the job's
 * lifetime so there's no free-race:
 *   submit (UI)  : copy ctx → core buffer; state=RUNNING; signal the worker.
 *   worker       : run work(); post EV_SYS_JOB_DONE; loop (never frees).
 *   deliver (UI) : if !cancelled call done(); free ctx; state=IDLE.
 *   cancel (UI)  : set flag + fire abort hook (unblocks work); done is skipped.
 */
#include "async_job.h"
#include "input.h"          /* EV_SYS_JOB_DONE */

#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "async";

typedef enum { JOB_IDLE, JOB_RUNNING, JOB_DONE_PENDING } job_state_t;

struct async_job {
    async_work_fn   work;
    async_done_fn   done;
    void           *ctx;            /* core-owned copy */
    size_t          ctx_size;
    volatile bool   cancelled;
    bool            ok;
    void          (*abort_fn)(void *);
    void           *abort_ctx;
};

static struct async_job s_job;       /* single job slot */
static volatile job_state_t s_state = JOB_IDLE;
static SemaphoreHandle_t s_go;       /* signals the worker a job is ready */
static QueueHandle_t s_ui_queue;

static void worker_task(void *arg)
{
    (void)arg;
    for (;;) {
        xSemaphoreTake(s_go, portMAX_DELAY);
        s_job.ok = s_job.work ? s_job.work(&s_job, s_job.ctx) : false;
        s_state = JOB_DONE_PENDING;
        input_event_t ev = EV_SYS_JOB_DONE;
        if (s_ui_queue) {
            xQueueSend(s_ui_queue, &ev, portMAX_DELAY);
        }
    }
}

void async_job_init(QueueHandle_t ui_queue)
{
    s_ui_queue = ui_queue;
    s_go = xSemaphoreCreateBinary();
    xTaskCreate(worker_task, "async", ASYNC_WORKER_STACK, NULL, ASYNC_WORKER_PRIO, NULL);
}

async_job_t *async_job_submit(async_work_fn work, async_done_fn done,
                              const void *ctx, size_t ctx_size)
{
    if (s_state != JOB_IDLE || ctx_size > ASYNC_CTX_MAX) {
        return NULL;
    }
    void *copy = NULL;
    if (ctx && ctx_size) {
        copy = malloc(ctx_size);
        if (!copy) {
            return NULL;
        }
        memcpy(copy, ctx, ctx_size);
    }
    s_job.work      = work;
    s_job.done      = done;
    s_job.ctx       = copy;
    s_job.ctx_size  = ctx_size;
    s_job.cancelled = false;
    s_job.ok        = false;
    s_job.abort_fn  = NULL;
    s_job.abort_ctx = NULL;
    s_state = JOB_RUNNING;
    xSemaphoreGive(s_go);              /* wake the worker */
    return &s_job;
}

void async_job_deliver(void)
{
    if (s_state != JOB_DONE_PENDING) {
        return;
    }
    if (!s_job.cancelled && s_job.done) {
        s_job.done(s_job.ctx, s_job.ok);
    }
    free(s_job.ctx);
    s_job.ctx = NULL;
    s_state = JOB_IDLE;
}

void async_job_cancel(async_job_t *job)
{
    if (job != &s_job) {
        return;
    }
    job->cancelled = true;
    if (job->abort_fn) {
        job->abort_fn(job->abort_ctx);   /* unblock a blocked work() (e.g. close socket) */
    }
    ESP_LOGD(TAG, "job cancelled");
}

bool async_job_cancelled(const async_job_t *job)
{
    return job && job->cancelled;
}

void async_job_on_cancel(async_job_t *job, void (*abort_fn)(void *), void *abort_ctx)
{
    if (job != &s_job) {
        return;
    }
    job->abort_fn  = abort_fn;
    job->abort_ctx = abort_ctx;
}
