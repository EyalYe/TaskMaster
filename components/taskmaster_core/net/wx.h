/*
 * wx.h — core weather + time service (PLAN §6C.1 steps 2–4).
 *
 * NTP (UTC) via esp_sntp, plus a keyless Open-Meteo lookup keyed by the `city`
 * config field: geocode the city → lat/lon, then a forecast call → current weather
 * + `utc_offset_seconds`. Local time = NTP UTC + offset. A background task does the
 * (HTTPS) fetches off the UI task and refreshes on a cadence; the Launcher status
 * bar (step 5) reads these snapshots. All getters are read-only + lock-free (the
 * fetched values are word-sized).
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <time.h>

/* Start NTP + the fetch task. Call once at boot (after config + Wi-Fi init). */
void wx_init(void);

/* Wake the fetch task to re-check the city + refresh now (e.g. after a config edit). */
void wx_refresh(void);

/* "HH:MM" local time into `out`. False until NTP is synced AND an offset is known. */
bool wx_time_str(char *out, size_t out_len);

/* Current LOCAL time as broken-down fields (NTP UTC + the city's UTC offset), via
 * gmtime_r on the offset-adjusted epoch — so tm_hour/tm_min/tm_sec/tm_yday are local.
 * Returns false until NTP is synced AND an offset is known (same gate as
 * wx_time_str). App-facing time source for schedulers (app-API 1.3). */
bool wx_local_time(struct tm *out);

/* Current weather: temperature (°C, rounded) + WMO weather code. False until fetched. */
bool wx_weather(int *temp_c, int *weather_code);

/* A short word for a WMO weather code ("Clear" / "Cloudy" / "Rain" / …). */
const char *wx_weather_desc(int weather_code);

/* One-line human status for Device info: no city / '<city>' not found / <city> NNC /
 * fetching. Lets a mistyped city surface instead of silently showing no weather. */
void wx_status_str(char *out, size_t out_len);
