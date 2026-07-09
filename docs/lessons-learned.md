# Lessons Learned the Hard Way

This file records the real-world problems we hit while bringing the ESP32 Paste Dongle up on actual hardware, and what the actual fixes were. It exists so future-us (and anyone else) does not repeat the same debugging loops.

## 1. OTA firmware updates were not actually writing anything

### Symptom
- `curl --data-binary @firmware.bin http://192.168.4.1/api/update` completed from the client side but the device returned `{"ok":false,"error":"invalid update","detail":"no detail"}`.
- The chunked Python uploader also completed the full ~874 KB but returned the same error.
- The device stayed online; it did not reboot.

### Root cause
`ESPAsyncWebServer` only invokes the **upload handler** (the second lambda passed to `server.on`) for `multipart/form-data` requests. The client was sending raw binary with `Content-Type: application/octet-stream`, so the upload handler never ran. The request handler (first lambda) simply saw an empty success flag and returned the generic rejection.

### Fix
- Change the client to send the firmware as a multipart file. `scripts/ota_upload.py` now uses:
  ```python
  files={"file": ("firmware.bin", data, "application/octet-stream")}
  ```
- The firmware endpoint comment was updated to make this explicit.
- Plain `curl --data-binary` uploads do **not** work and are no longer documented as the primary path.

### Verification
- `.venv/bin/python scripts/ota_upload.py .pio/build/esp32s3-usb/firmware.bin --host 192.168.4.1` returns `{"ok":true,"reboot":true}` and the device reboots into the new firmware.
- `bash test/ota_update_test.sh` passes: empty body and undersized multipart file are rejected with HTTP 500 and no reboot.

## 2. The BOOT button was not broken

### Symptom
After flashing with `esptool.py --before no_reset`, the board kept appearing as `/dev/cu.usbmodem1101` (the ROM bootloader port) instead of `/dev/cu.usbmodem1051DB5E95D82` (the application CDC port). Every reset or hard-reset command put it back into bootloader mode.

### What looked like the problem
It appeared the BOOT button was stuck low, because the strapping pin that selects download mode was being sampled as low on every reset.

### Actual explanation
The user **was following the instructions correctly**: hold BOOT, press RESET, release BOOT to enter bootloader mode. The issue was the **reverse transition**: once in bootloader mode, pressing RESET again while BOOT was still being held (or while the finger had not fully come off the button) re-sampled BOOT as low and re-entered bootloader. The button itself was fine.

### Reliable procedure for this board (ESP32-S3-DevKitM-1)

**Into bootloader mode (for serial flashing):**
1. Hold **BOOT**.
2. Press and release **RESET**.
3. Release **BOOT**.
4. Port becomes `/dev/cu.usbmodem1101` (short number).

**Out of bootloader mode and into the flashed application:**
1. **Unplug USB**.
2. **Do not touch any buttons**.
3. **Plug USB back in**.
4. Port becomes `/dev/cu.usbmodem1051DB5E95D82` (contains chip serial number) and the `PasteDongle` AP appears.

Pressing RESET while the board is in bootloader mode is risky because it re-samples the BOOT pin; a power-cycle is the cleanest way to boot the application.

## 3. Serial console needs DTR asserted on macOS

### Symptom
Python/pyserial and `cat` read zero bytes from the application CDC port even though the firmware was printing boot messages and CLI responses.

### Fix
Open the serial port with **DTR=True** (RTS can stay False). With DTR low, the CDC endpoint does not deliver data on this board/macOS combination.

```python
s = serial.Serial('/dev/cu.usbmodem1051DB5E95D82', 115200, timeout=1)
s.setDTR(True)
s.setRTS(False)
```

## 4. Two different USB ports for the same board

On the ESP32-S3-DevKitM-1:
- **Bootloader / ROM USB Serial-JTAG:** `/dev/cu.usbmodem1101` — used by `esptool.py`.
- **Application CDC (Arduino USB stack):** `/dev/cu.usbmodem1051DB5E95D82` — used for the serial CLI and for confirming the app is running.

The port name changes after a power-cycle out of bootloader mode. Always check `ls /dev/cu.* | grep usb` after each reset.

## 5. Python environment quirks

The pre-existing `.venv` in this project was incomplete (no `pip`, broken `ensurepip`). We recreated it with `virtualenv` and Python 3.9:

```bash
rm -rf .venv
virtualenv .venv -p python3.9
.venv/bin/pip install platformio
```

Also, the Python in this environment lacks `sys.environ` (likely stripped by the runtime/sandbox), so `scripts/ota_upload.py` now uses `os.environ`.

## 6. Current working end-to-end commands

Build:
```bash
.venv/bin/platformio run -e esp32s3-usb
```

Flash over USB (bootloader mode required):
```bash
/usr/bin/python3 ~/.platformio/packages/tool-esptoolpy/esptool.py \
  --chip esp32s3 --port /dev/cu.usbmodem1101 --before no_reset \
  --baud 921600 write_flash -z \
  0x0     .pio/build/esp32s3-usb/bootloader.bin \
  0x8000  .pio/build/esp32s3-usb/partitions.bin \
  0x10000 .pio/build/esp32s3-usb/firmware.bin
```

OTA over Wi-Fi:
```bash
.venv/bin/python scripts/ota_upload.py .pio/build/esp32s3-usb/firmware.bin --host 192.168.4.1
```

Run host tests:
```bash
cd test && make run
```
