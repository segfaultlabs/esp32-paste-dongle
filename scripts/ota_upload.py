#!/usr/bin/env python3
"""OTA uploader for the PasteDongle.

Uploads firmware to the /api/update endpoint. The device requires a bearer
token (printed to serial on every boot); pass it via --token or $OTA_TOKEN.

Usage:
    export DEVICE_IP=192.168.4.1
    export OTA_TOKEN=<token shown on serial>
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
    parser.add_argument(
        "--token",
        default=os.environ.get("OTA_TOKEN", ""),
        help="OTA bearer token (shown on serial at boot, or $OTA_TOKEN)",
    )
    parser.add_argument("--chunk-size", type=int, default=1024, help="bytes per chunk")
    parser.add_argument("--delay", type=float, default=0.05, help="seconds between chunks")
    args = parser.parse_args()

    url = f"http://{args.host}/api/update"

    with open(args.firmware, "rb") as f:
        data = f.read()

    total = len(data)
    print(f"Uploading {total} bytes to {url}", file=sys.stderr)

    if not args.token:
        print("WARNING: no --token provided; upload will be rejected (HTTP 401).", file=sys.stderr)
        print("  Read the token from the device serial output or run:", file=sys.stderr)
        print("  export OTA_TOKEN=<token>", file=sys.stderr)

    headers = {}
    if args.token:
        headers["Authorization"] = f"Bearer {args.token}"

    # The firmware's upload handler is only invoked by AsyncWebServer for
    # multipart/form-data file uploads, not for raw binary bodies. Send the
    # firmware as a multipart file so the handler runs and writes to flash.
    resp = requests.post(
        url,
        files={"file": ("firmware.bin", data, "application/octet-stream")},
        headers=headers,
        timeout=120,
    )
    print(f"HTTP {resp.status_code}")
    print(resp.text)
    sys.exit(0 if resp.status_code == 200 else 1)


if __name__ == "__main__":
    main()
