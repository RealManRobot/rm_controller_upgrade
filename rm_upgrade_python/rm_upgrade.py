#!/usr/bin/env python3
"""Python demo that drives the controller upgrade flow through the
upgrade_client C++ library (via the ctypes bindings in upgrade_client.py).

Flow: upload -> wait for extraction -> start all -> poll progress.
"""

import argparse
import os
import sys
import time

from upgrade_client import UpgradeClient


def _enable_utf8_output():
    """Print Unicode (e.g. Chinese) text correctly.

    On Windows the console and Python's output streams default to the legacy
    ANSI code page, which garbles UTF-8 text (item names, file paths). Switch
    the console to UTF-8 and the streams to UTF-8. No-op / harmless elsewhere.
    """
    if os.name == "nt":
        try:
            import ctypes
            ctypes.windll.kernel32.SetConsoleOutputCP(65001)  # UTF-8
            ctypes.windll.kernel32.SetConsoleCP(65001)
        except Exception:
            pass
    for stream in (sys.stdout, sys.stderr):
        try:
            stream.reconfigure(encoding="utf-8")   # Python 3.7+
        except Exception:
            pass


def main():
    _enable_utf8_output()
    ap = argparse.ArgumentParser(
        description="Drive the upgrade_client library from Python.")
    ap.add_argument("host", help="controller host/IP (connects to <host>:8090)")
    ap.add_argument("package", help="path to the upgrade package")
    ap.add_argument("--stored-name", default="",
                    help="name stored on the controller (default: basename)")
    ap.add_argument("--extract-timeout", type=int, default=120)
    ap.add_argument("--upgrade-timeout", type=int, default=600)
    ap.add_argument("--poll", type=int, default=2, help="poll interval (s)")
    ap.add_argument("--no-reboot", dest="reboot", action="store_false",
                    help="do not reboot the controller after upgrading")
    ap.add_argument("--lib", help="path to the upgrade_capi shared library")
    args = ap.parse_args()

    with UpgradeClient(args.host, lib_path=args.lib) as client:
        print("rm_upgrade_python (upgrade_client %s)" % client.version())
        print("Target: %s:8090" % args.host)

        # 1) Upload
        print("[1/4] upload %s ..." % args.package)
        if not client.upload(args.package, args.stored_name):
            print("  upload failed:", client.last_error())
            return 2

        # 2) Wait for extraction
        print("[2/4] waiting for extraction (up to %ds) ..." % args.extract_timeout)
        items = client.wait_extract(args.extract_timeout)
        if items is None:
            print("  extract failed:", client.last_error())
            return 3
        if not items:
            print("  extraction reported no items; nothing to upgrade.")
            return 3
        for it in items:
            print("   - %s (type %s)" % (it["name"], it["type"]))

        # 3) Start (all items)
        print("[3/4] startUpgrade for %d item(s) ..." % len(items))
        if not client.start_all():
            print("  startUpgrade failed:", client.last_error())
            return 4

        # 4) Poll progress
        print("[4/4] polling progress (every %ds) ..." % args.poll)
        deadline = time.time() + args.upgrade_timeout
        while True:
            rate = client.get_rate()
            if rate is not None:
                print("  ", " ".join(
                    "%s:%d%%" % (it["name"], it["rate"]) for it in rate))
                if rate and all(it["rate"] >= 100 for it in rate):
                    print("Upgrade completed successfully.")
                    # Reboot the controller so the new program takes effect.
                    if args.reboot:
                        print("[5/5] sysReboot ...")
                        if not client.reboot():
                            print("  sysReboot failed:", client.last_error())
                        else:
                            print("  reboot requested")
                    return 0
            else:
                print("  poll error:", client.last_error())
            if time.time() >= deadline:
                print("Timed out waiting for upgrade to finish.")
                return 6
            time.sleep(args.poll)


if __name__ == "__main__":
    sys.exit(main())
