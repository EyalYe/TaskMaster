/*
 * task_model.c — mutex-guarded shared model (PLAN §5.2). See task_model.h.
 */
#include "task_model.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static SemaphoreHandle_t s_lock;
static task_status_t     s_status = {
    .sync       = SYNC_IDLE,
    .wifi_rssi  = 0,
    .task_count = 0,
};

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
    *out = s_status;
    xSemaphoreGive(s_lock);
}

void task_model_set(const task_status_t *in)
{
    if (in == NULL || s_lock == NULL) {
        return;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_status = *in;
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
