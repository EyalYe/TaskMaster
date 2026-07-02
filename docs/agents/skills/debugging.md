# Skill: debugging on hardware

## Serial capture (the workhorse)

`pyserial` is in the IDF venv:
```bash
PYSER=/Users/yeminie/.espressif/tools/python/v6.0.1/venv/bin/python
```
Reset the board and watch specific tags (adjust the filter):
```python
import serial, time, glob
s = serial.Serial(sorted(glob.glob("/dev/cu.usbmodem*"))[0], 115200, timeout=0.2)
s.setDTR(False); s.setRTS(True); time.sleep(0.1); s.setRTS(False)      # reset via RTS
end = time.time() + 20
while time.time() < end:
    for ln in s.read(4096).decode("utf-8", "replace").splitlines():
        if any(k in ln for k in ("taskmaster:", "wx:", "portal:", "Guru", "Backtrace", "abort")):
            print(ln, flush=True)
```
- `idf.py monitor` also works but is interactive; the script above is better for automated checks.
- Don't hold the port open in two processes at once (flashing needs it).

## Reading a panic

- A crash prints `Guru Meditation Error ... Backtrace: 0x... 0x...`. The C3 is **RISC-V**, so decode
  the addresses with:
  ```bash
  riscv32-esp-elf-addr2line -e build/taskmaster_c3_app.elf 0x... 0x...
  ```
  (or just paste the log into `idf.py monitor`, which symbolizes it automatically).
- Rollback: a freshly-OTA'd image is pending-verify; if it crashes before `taskmaster_run()` marks it
  valid, the bootloader reverts to the previous slot on the next reboot.

## Recurring gotchas (also in build-and-flash.md)

- **No serial port** → device is light/deep-sleeping (native USB dropped). Download mode: hold **BOOT**,
  tap **RESET**, release **BOOT**. Turn `Settings → Deep sleep → Off` while iterating.
- **LVGL build error** → `rm -f managed_components/lvgl__lvgl/CMakePresets.json`.
- **`apps.yaml` edit ignored** → `idf.py reconfigure` (should be automatic via `CMAKE_CONFIGURE_DEPENDS`).
- **Provisioning:** first boot / Home-held-at-reset → SoftAP `TaskMaster-Setup` → `192.168.4.1`. After
  provisioned, edit config without re-provisioning via the **LAN config page** (Settings → Web config →
  browse the device IP). The Wi-Fi password field is blank each load (re-enter it if you change Wi-Fi).
- **Empty SSID scan in the portal** was a fixed bug (STA reconnect starved the scan); the macOS `select`
  dropdown replaced an invisible `datalist`.

## Known-good references

- Weather test: `city = Petah Tikva` geocodes (32.0871, 34.8875); a bad spelling shows
  `Weather: '<city>' not found` in Device info.
- A clean boot logs `taskmaster: Registered apps: N` then the `app[i]` list + the boot-mode branch.
