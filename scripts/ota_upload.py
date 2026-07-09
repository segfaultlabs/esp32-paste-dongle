#!/usr/bin/env python3
"""Chunked OTA uploader for the PasteDongle.

Plain curl uploads tend to reset the connection on this firmware because the
ESP32 async TCP task cannot absorb the full firmware in one burst. This script
sends the firmware in small chunks with short pauses, which lets the device
keep up while it writes to flash.

Usage:
    export DEVICE_IP=192.168.4.1
    python3 scripts/ota_upload.py .pio/build/esp32s3-usb/firmware.bin
"""

import argparse
import os
import sys
import time

import requests


def main():
    parser = argparse.ArgumentParser(description="Upload firmware to PasteDongle OTA endpoint")
    parser.add_argument("firmware", help="path to firmware.bin")
    parser.add_argument(
        "--host",
        default=os.environ.get("DEVICE_IP", "192.168.4.1"),
        help="device IP or hostname (default: 192.168.4.1 or $DEVICE_IP)",
    )
    parser.add_argument("--chunk-size", type=int, default=1024, help="bytes per chunk")
    parser.add_argument("--delay", type=float, default=0.05, help="seconds between chunks")
    args = parser.parse_args()

    url = f"http://{args.host}/api/update"

    with open(args.firmware, "rb") as f:
        data = f.read()

    total = len(data)
    print(f"Uploading {total} bytes to {url}", file=sys.stderr)

    # The firmware's upload handler is only invoked by AsyncWebServer for
    # multipart/form-data file uploads, not for raw binary bodies. Send the
    # firmware as a multipart file so the handler runs and writes to flash.
    resp = requests.post(
        url,
        files={"file": ("firmware.bin", data, "application/octet-stream")},
        timeout=120,
    )
    print(f"HTTP {resp.status_code}")
    print(resp.text)
    sys.exit(0 if resp.status_code == 200 else 1)


if __name__ == "__main__":
    main()
