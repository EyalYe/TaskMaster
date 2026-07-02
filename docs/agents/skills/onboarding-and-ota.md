# Skill: onboarding, CI & OTA (zero self-hosting)

The whole point: a developer gets a working device with their apps, and the maintainer **hosts nothing**
but a GitHub repo. See PLAN.md §6D/§6E.

## The pieces

| Piece | Where | What it does |
|---|---|---|
| **Template repo** | `TM-Template` (fork target) | `apps.yaml` + compose hook + `app_skeleton` + tools + CI. Pulls `taskmaster_core` as a git dep. Devs fork this, never core. |
| **CI** | `.github/workflows/build.yml` | On push, `esp-idf-ci-action` builds (esp32c3) and uploads the app `.bin` + merged image as artifacts (Release on a tag). Public repos → no secrets. |
| **`tools/flash.py`** | template | First USB flash via esptool (no ESP-IDF). Auto-detects port. |
| **`tools/ota_serve.py`** | template | Serves the app image over HTTP on the LAN for OTA. |

## The developer flow

```
fork TM-Template → edit apps.yaml / add an app folder → push
  → CI builds the .bin (or build locally)
  → python tools/flash.py --bin <downloaded.bin>     # first flash over USB
  → python tools/ota_serve.py                          # host updates on your LAN
  → point the device fw_url at it → Settings → Check update   # OTA forever
```

## Local LAN OTA (how it actually works)

1. `python tools/ota_serve.py` prints a URL like `http://<your-machine>:8000/<app>.bin`.
2. On the device: **Settings → Web config → On**, browse to the device IP (Settings → Device info), set
   **`fw_url`** to that URL, Save (a live edit — no reboot).
3. **Settings → Check update** — the device downloads the image to the spare OTA slot and reboots into
   it. Rollback guards a bad image (marked valid once it boots healthy).

**Core requirement (already done):** OTA over plain `http://` needs `CONFIG_ESP_HTTPS_OTA_ALLOW_HTTP=y`
(in `sdkconfig.defaults`) and `ota_work()` attaching the cert bundle **only for `https://`**. HTTPS OTA
(e.g. a GitHub Release URL) still verifies via the cert bundle.

## CI notes

- Action versions must target **Node 24** (`actions/checkout@v5`, `actions/upload-artifact@v5`) — v4
  warns (Node 20 deprecation).
- `esp_idf_version: v6.0.1`, `target: esp32c3`. The `command` runs `idf.py reconfigure` (fetch), removes
  the lvgl `CMakePresets.json`, `idf.py build`, then `idf.py merge-bin`.
- **Private app repos** need a `GH_PAT` secret + the (commented) git-auth step; public repos need
  nothing. Core, TM-YappCloud, TM-Template are all public.
