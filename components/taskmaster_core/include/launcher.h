/*
 * launcher.h — the home screen: a raw-rendered (sh1106) scrollable list of the
 * registered apps (PLAN §6.1; LVGL deferred to Phase 3, §4.4). Owned and driven
 * by the UI task — never call these from another task.
 */
#pragma once

#include "input.h"

/* Reset selection to the top and draw the list. Call when returning home. */
void launcher_open(void);

/* Handle one input event while in the Launcher. Returns the app index to enter
 * (>= 0) when the user selects one, or -1 to stay in the Launcher.
 * (EV_HOME never reaches here — it's intercepted by the UI task, §5.2.) */
int launcher_input(input_event_t ev);

/* Redraw the list (e.g. after a status change). */
void launcher_render(void);
