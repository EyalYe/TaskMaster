# Skill: build & flash

The buildable project is the **template** (`~/TaskMaster-App-Template`) — build there, not in core (see
PREFERENCES.md). These commands assume that dir; the core repo builds the same way for a verification.

## Environment

```bash
source "$HOME/.espressif/tools/activate_idf_v6.0.1.sh"   # once per shell — ESP-IDF v6.0.1
```

## Build

```bash
cd ~/TaskMaster-App-Template
idf.py set-target esp32c3                                  # first time, or after deleting sdkconfig
rm -f managed_components/lvgl__lvgl/CMakePresets.json      # ALWAYS before build (see gotcha)
idf.py build
```

`idf.py build` runs the compose hook (regenerates `main/idf_component.yml` from `apps.yaml`), fetches
`taskmaster_core` + apps over git, and compiles. Output: `build/taskmaster_c3_app.bin` (~1.4 MB, ~30%
free in the OTA slot).

## Flash

```bash
idf.py -p /dev/cu.usbmodem101 flash          # find the port with: ls /dev/cu.usbmodem*
# or, toolchain-free (esptool only):
python tools/flash.py                         # auto-detects the port, flashes ./build
python tools/flash.py --bin firmware.bin      # a merged image (e.g. a CI Release)
```

## Gotchas (all hit in practice — don't relearn them)

- **LVGL build error** — the managed `lvgl/lvgl` component drops a stray `CMakePresets.json` that breaks
  the build. `rm -f managed_components/lvgl__lvgl/CMakePresets.json` before every `idf.py build`.
  Harmless if absent.
- **Port vanishes / flash won't connect** — the C3's **native USB drops in light/deep sleep** (only
  `/dev/cu.Bluetooth*` show). Enter **download mode**: hold **BOOT**, tap **RESET**, release **BOOT**,
  then flash. While iterating, `Settings → Deep sleep → Off` avoids it.
- **Editing `apps.yaml` didn't rebuild** — fixed via `CMAKE_CONFIGURE_DEPENDS`; if a change isn't
  picked up, `idf.py reconfigure`.
- **Wrong target after deleting `sdkconfig`** — it resets to `esp32`; re-run `idf.py set-target esp32c3`.
- **Changed core/an app repo you build from git** — force a re-fetch:
  `rm -rf managed_components/taskmaster_core dependencies.lock` then `idf.py reconfigure`.
- **A `sdkconfig.defaults` change won't apply to an existing `sdkconfig`** — delete `sdkconfig` and
  re-run `set-target` (or set it via `menuconfig`).

## Serial / verifying a boot

`pyserial` lives in the IDF venv:
```bash
PYSER=/Users/yeminie/.espressif/tools/python/v6.0.1/venv/bin/python
```
Capture a boot (reset via DTR/RTS, then watch for the app registration):
```python
import serial, time, glob
s = serial.Serial(sorted(glob.glob("/dev/cu.usbmodem*"))[0], 115200, timeout=0.2)
s.setDTR(False); s.setRTS(True); time.sleep(0.1); s.setRTS(False)   # reset pulse
end = time.time() + 15
while time.time() < end:
    for ln in s.read(4096).decode("utf-8", "replace").splitlines():
        if "taskmaster:" in ln: print(ln)
```
Look for `taskmaster: Registered apps: N` + the `app[i] = ...` list to confirm what's running.
