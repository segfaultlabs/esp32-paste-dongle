# Flashing, Reset, and Layout Guide

This guide covers getting the ESP32-S3 USB build onto the board, setting the keyboard layout, and verifying that typing works.

## Build the firmware

```bash
.venv/bin/platformio run -e esp32s3-usb
```

The build produces:

- `.pio/build/esp32s3-usb/bootloader.bin`
- `.pio/build/esp32s3-usb/partitions.bin`
- `.pio/build/esp32s3-usb/firmware.bin`

## Put the board into bootloader mode

On the ESP32-S3-DevKitM-1 used for testing, the USB/RTS auto-reset line is not wired to the physical reset button, so `pio run -t upload` cannot reset the board into download mode by itself. You must enter bootloader mode manually:

1. Hold the **BOOT** button.
2. Press and release the **RESET** button.
3. Release the **BOOT** button.

The board will enumerate as a USB serial port (e.g. `/dev/cu.usbmodem1101`).

## Flash with esptool

Write the three images at their correct offsets:

```bash
/usr/bin/python3 \
  ~/.platformio/packages/tool-esptoolpy/esptool.py \
  --chip esp32s3 --port /dev/cu.usbmodem1101 --before no_reset \
  --baud 921600 write_flash -z \
  0x0   .pio/build/esp32s3-usb/bootloader.bin \
  0x8000 .pio/build/esp32s3-usb/partitions.bin \
  0x10000 .pio/build/esp32s3-usb/firmware.bin
```

**Important:** if you previously flashed only `firmware.bin` at offset `0x0`, you overwrote the bootloader. Always use the three-offset command above, or erase the whole flash and start fresh.

## Boot the application

After `esptool` reports `Hard resetting via RTS pin...`, the board may still be sitting in the ROM bootloader because the RTS reset does not reach the reset button. Press and release **RESET** (without holding BOOT) to boot the flashed application.

The board will start the `PasteDongle` Wi-Fi AP (default password `pastepaste`) at `192.168.4.1`.

## Set the keyboard layout

If the host PC is using Dvorak, set the dongle layout to match:

```bash
curl -s -X POST http://192.168.4.1/api/layout -d "layout=DVORAK"
```

Other supported layouts: `US`, `UK`, `DVORAK`.

## Verify typing

Connect to the `PasteDongle` Wi-Fi network, open a text field on the target PC, and use the web UI at `http://192.168.4.1/`, or send text via the WebSocket:

```python
import websocket
ws = websocket.create_connection("ws://192.168.4.1/ws")
text = "Dvorak layout test success"
ws.send(f"start|{len(text)}|fast")
ws.send(f"chunk|{text}")
ws.close()
```

The text should appear in the target PC's input field.

## OTA firmware updates

The `/api/update` endpoint accepts a firmware binary uploaded as a `multipart/form-data` file. The `scripts/ota_upload.py` helper does this for you:

```bash
.venv/bin/python scripts/ota_upload.py .pio/build/esp32s3-usb/firmware.bin --host 192.168.4.1
```

Requirements and behavior:

- Connect your computer to the `PasteDongle` Wi-Fi AP first.
- Empty or undersized (`< 8192` bytes) uploads are rejected with `{"ok":false}` and do **not** reboot the device.
- A successful upload returns `{"ok":true,"reboot":true}` and the ESP32 reboots into the new firmware.

Plain `curl --data-binary` uploads do **not** work because AsyncWebServer only invokes the upload handler for multipart uploads.

## Serial console

The `esp32s3-usb` build has `ARDUINO_USB_CDC_ON_BOOT=1`, so the native USB port also provides a serial console at 115200 baud. You can open it with any serial terminal, for example:

```bash
/usr/bin/python3 -m serial.tools.miniterm /dev/cu.usbmodem1051DB5E95D82 115200
```

The device also accepts simple commands over this console:

```
name <text>    Set BLE device name (requires reset)
getname        Show current BLE device name
type <text>    Type text now (if connected)
jiggle on|off  Enable/disable mouse jiggler
jiggle status  Show mouse jiggler settings
reboot         Restart the ESP32
help           Show this help
```
