/*
 * taskmaster.h — the OS entry point.
 *
 * All firmware bootstrap (NVS, Wi-Fi, display, input, boot-mode branch, the UI task)
 * lives inside the sealed core. A firmware image's `main` component is a trivial stub
 * that just calls taskmaster_run() from app_main() — so the project a developer forks
 * never contains (or edits) any bootstrap logic (PLAN §6D/§6E; core is immutable).
 */
#pragma once

/* Bring up the whole device and hand off to the UI task. Never returns meaningfully
 * (the UI task owns the device from here). Call once from app_main(). */
void taskmaster_run(void);
