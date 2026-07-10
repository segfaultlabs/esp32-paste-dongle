#!/usr/bin/env python3
"""Upload data/index.html to the device filesystem over HTTP (no reflash needed).

Usage:
    python3 scripts/upload_ui.py
    python3 scripts/upload_ui.py --host 192.168.4.1
    python3 scripts/upload_ui.py data/index.html
"""
import argparse, os, sys, requests

def main():
    parser = argparse.ArgumentParser(description="Upload UI file to PasteDongle LittleFS")
    parser.add_argument("file", nargs="?", default="data/index.html", help="HTML file to upload")
    parser.add_argument("--host", default=os.environ.get("DEVICE_IP", "192.168.4.1"))
    args = parser.parse_args()

    with open(args.file, "rb") as f:
        data = f.read()

    url = f"http://{args.host}/api/fs/upload"
    print(f"Uploading {len(data)} bytes to {url}", file=sys.stderr)
    resp = requests.post(url, files={"file": ("index.html", data, "text/html")}, timeout=30)
    print(f"HTTP {resp.status_code}: {resp.text}")
    sys.exit(0 if resp.status_code == 200 else 1)

if __name__ == "__main__":
    main()
