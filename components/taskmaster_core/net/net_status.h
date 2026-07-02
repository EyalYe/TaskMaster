/*
 * net_status.h — connectivity status, the app-facing way (PLAN §6 / §8A / §8.3).
 *
 * Every app gets the device's Wi-Fi / network state from ONE place, read-only, with
 * no locking or polling to think about:
 *
 *     net_status_t ns;
 *     net_status_get(&ns);
 *     if (ns.online) { ... }            // or: if (net_is_online()) { ... }
 *
 * Guarantee: the UI task re-runs your app's render() whenever connectivity changes,
 * so reading net_status_get() inside render() is always current — you never have to
 * subscribe, poll, or own a Wi-Fi event handler. Apps never manage the radio
 * themselves; that's a core service (the network task, Phase 3; the Setup app, §7).
 */
#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include <stdbool.h>

typedef enum {
    NET_WIFI_OFF = 0,   /* radio disabled by the user (Settings WIFI_EN=0, §8A) */
    NET_DISCONNECTED,   /* radio on, not associated                             */
    NET_CONNECTING,     /* associating / acquiring an IP                        */
    NET_CONNECTED,      /* associated + has IP — online                         */
    NET_PORTAL,         /* SoftAP provisioning portal up (Setup app, §7)        */
} net_state_t;

typedef struct {
    net_state_t state;
    bool        online;   /* convenience: == (state == NET_CONNECTED) */
    int         rssi;     /* dBm when online, else 0                   */
} net_status_t;

/* ───────────────── App-facing API (call from any task) ───────────────── */

/* Copy the current connectivity snapshot out (under a lock). */
void net_status_get(net_status_t *out);

/* Convenience: true when the device has a usable connection. */
bool net_is_online(void);

/* Short label for a state — handy for status bars/logs:
 * "OFF" / "---" / "..." / "OK" / "SETUP". */
const char *net_state_str(net_state_t s);

/* ───────────────── Core-internal (NOT for apps) ───────────────── */

void net_status_init(void);                      /* create the lock (boot)        */
void net_status_attach_ui(QueueHandle_t ui_q);   /* where change events are posted */
void net_status_set(net_state_t state, int rssi);/* update + notify the UI on change*/
void net_status_notify(void);                    /* poke the UI to re-render (no state change) */
