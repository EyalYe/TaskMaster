/*
 * task_model.c — mutex-guarded shared model (PLAN §5.2). See task_model.h.
 */
#include "task_model.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include <string.h>

static SemaphoreHandle_t s_lock;

static sync_state_t s_sync = SYNC_IDLE;
static task_t       s_tasks[TASKMASTER_MAX_TASKS];   /* fixed array, no per-task malloc (§6A.1) */
static unsigned     s_count;

void task_model_init(void)
{
    s_lock = xSemaphoreCreateMutex();
}

void task_model_get(task_status_t *out)
{
    if (out == NULL || s_lock == NULL) {
        return;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    out->sync       = s_sync;
    out->task_count = s_count;
    xSemaphoreGive(s_lock);
}

unsigned task_model_copy(task_t *out, unsigned max)
{
    if (out == NULL || s_lock == NULL) {
        return 0;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    unsigned n = s_count < max ? s_count : max;
    memcpy(out, s_tasks, n * sizeof(task_t));
    xSemaphoreGive(s_lock);
    return n;
}

void task_model_set_sync(sync_state_t s)
{
    if (s_lock == NULL) {
        return;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_sync = s;
    xSemaphoreGive(s_lock);
}

void task_model_set_tasks(const task_t *in, unsigned count)
{
    if (s_lock == NULL) {
        return;
    }
    if (count > TASKMASTER_MAX_TASKS) {
        count = TASKMASTER_MAX_TASKS;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (in != NULL && count > 0) {
        memcpy(s_tasks, in, count * sizeof(task_t));
        s_count = count;
    } else {
        s_count = 0;
    }
    xSemaphoreGive(s_lock);
}

const char *sync_state_label(sync_state_t s)
{
    switch (s) {
    case SYNC_IDLE:       return "IDLE";
    case SYNC_CONNECTING: return "CONN";
    case SYNC_SYNCING:    return "SYNC";
    case SYNC_OK:         return "OK";
    case SYNC_ERROR:      return "ERR";
    default:              return "?";
    }
}
