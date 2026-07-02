/*
 * net_status.c — connectivity snapshot + change notification. See net_status.h.
 *
 * One mutex-guarded snapshot (the §5.2 copy-out pattern). On a *change*, posts a
 * synthetic EV_SYS_NET_CHANGED to the UI event queue so the UI task re-renders the
 * current view — that's what makes "read it in render()" always-fresh for apps.
 * The system event is handled by the UI and never delivered to an app's on_event.
 */
#include "net_status.h"
#include "input.h"   /* EV_SYS_NET_CHANGED */

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static SemaphoreHandle_t s_lock;
static QueueHandle_t     s_ui_q;     /* optional observer (the UI event queue) */
static net_status_t      s_status = { .state = NET_WIFI_OFF, .online = false, .rssi = 0 };

void net_status_init(void)
{
    s_lock = xSemaphoreCreateMutex();
}

void net_status_attach_ui(QueueHandle_t ui_q)
{
    s_ui_q = ui_q;
}

void net_status_get(net_status_t *out)
{
    if (out == NULL || s_lock == NULL) {
        return;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    *out = s_status;
    xSemaphoreGive(s_lock);
}

bool net_is_online(void)
{
    net_status_t s;
    net_status_get(&s);
    return s.online;
}

void net_status_set(net_state_t state, int rssi)
{
    if (s_lock == NULL) {
        return;
    }
    bool changed;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    changed = (s_status.state != state) || (s_status.rssi != rssi);
    s_status.state  = state;
    s_status.online = (state == NET_CONNECTED);
    s_status.rssi   = (state == NET_CONNECTED) ? rssi : 0;
    xSemaphoreGive(s_lock);

    if (changed && s_ui_q != NULL) {
        input_event_t ev = EV_SYS_NET_CHANGED;
        xQueueSend(s_ui_q, &ev, 0);   /* best-effort; UI coalesces by re-rendering */
    }
}

void net_status_notify(void)
{
    if (s_ui_q != NULL) {
        input_event_t ev = EV_SYS_NET_CHANGED;
        xQueueSend(s_ui_q, &ev, 0);   /* poke the UI to re-render (e.g. wx updated) */
    }
}

const char *net_state_str(net_state_t s)
{
    switch (s) {
    case NET_WIFI_OFF:     return "OFF";
    case NET_DISCONNECTED: return "---";
    case NET_CONNECTING:   return "...";
    case NET_CONNECTED:    return "OK";
    case NET_PORTAL:       return "SETUP";
    default:               return "?";
    }
}
