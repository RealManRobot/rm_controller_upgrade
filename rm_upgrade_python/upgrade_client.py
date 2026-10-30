"""Python bindings for the upgrade_client library.

Loads the C ABI shim (``libupgrade_capi.so`` / ``upgrade_capi.dll``) built from
``capi/`` and exposes a small ``UpgradeClient`` class over it via ctypes.
"""

import ctypes
import json
import os
import platform
import sys


def _fsencode(path):
    """Encode a path the way the C runtime's narrow file API expects.

    On Windows, MSVC opens ``char*`` paths using the active ANSI code page, so
    UTF-8 would break non-ASCII (e.g. Chinese) names — use ``mbcs`` to match it.
    On other systems use the filesystem encoding (UTF-8 on Linux).
    """
    if isinstance(path, (bytes, bytearray)):
        return bytes(path)
    if os.name == "nt":
        return path.encode("mbcs")
    return os.fsencode(path)


def _lib_filename():
    if sys.platform.startswith("win"):
        return "upgrade_capi.dll"
    if sys.platform == "darwin":
        return "libupgrade_capi.dylib"
    return "libupgrade_capi.so"


def _platform_dir():
    """Per-platform lib/ subfolder for the current interpreter."""
    machine = platform.machine().lower()
    if sys.platform.startswith("win"):
        return "windows-x64"
    if machine in ("x86_64", "amd64"):
        return "linux-x86_64"
    if machine in ("aarch64", "arm64"):
        return "linux-arm64"
    return "linux-" + machine


def _load_lib(path=None):
    here = os.path.dirname(os.path.abspath(__file__))
    name = _lib_filename()
    pdir = _platform_dir()
    candidates = [path] if path else []
    candidates += [
        os.path.join(here, "lib", pdir, name),          # prebuilt, per-platform
        os.path.join(here, "build", name),              # local dev build
        os.path.join(here, "build", "Release", name),   # multi-config (Windows)
        os.path.join(here, name),
    ]
    for p in candidates:
        if p and os.path.exists(p):
            return ctypes.CDLL(p)
    raise FileNotFoundError(
        "upgrade_capi shared library not found for this platform (%s). Build it:\n"
        "  cmake -S . -B build && cmake --build build\n"
        "Tried: %s" % (pdir, ", ".join(c for c in candidates if c))
    )


def _bind(lib):
    lib.uc_version.restype = ctypes.c_char_p

    lib.uc_create.restype = ctypes.c_void_p
    lib.uc_create.argtypes = [ctypes.c_char_p]
    lib.uc_destroy.argtypes = [ctypes.c_void_p]

    lib.uc_set_timeouts.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_int]

    lib.uc_upload.restype = ctypes.c_int
    lib.uc_upload.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_char_p]

    # JSON getters return a malloc'd char* -> keep it as a raw pointer
    # (c_void_p) so we can free it after reading.
    lib.uc_wait_extract_json.restype = ctypes.c_void_p
    lib.uc_wait_extract_json.argtypes = [ctypes.c_void_p, ctypes.c_int]
    lib.uc_get_rate_json.restype = ctypes.c_void_p
    lib.uc_get_rate_json.argtypes = [ctypes.c_void_p]

    lib.uc_start_all.restype = ctypes.c_int
    lib.uc_start_all.argtypes = [ctypes.c_void_p]
    lib.uc_clear.restype = ctypes.c_int
    lib.uc_clear.argtypes = [ctypes.c_void_p]
    lib.uc_reboot.restype = ctypes.c_int
    lib.uc_reboot.argtypes = [ctypes.c_void_p]

    lib.uc_last_error.restype = ctypes.c_char_p
    lib.uc_last_error.argtypes = [ctypes.c_void_p]
    lib.uc_free_string.argtypes = [ctypes.c_void_p]
    return lib


def library_version(lib_path=None):
    """Return the upgrade_client version without creating a client."""
    lib = _bind(_load_lib(lib_path))
    return lib.uc_version().decode()


class UpgradeClient:
    """Drives the controller upgrade flow via the C++ upgrade_client library."""

    def __init__(self, host, lib_path=None):
        self._lib = _bind(_load_lib(lib_path))
        self._c = self._lib.uc_create(host.encode())
        if not self._c:
            raise RuntimeError("uc_create failed")

    # --- lifetime ---------------------------------------------------------
    def close(self):
        c = getattr(self, "_c", None)
        if c:
            self._lib.uc_destroy(c)
            self._c = None

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self.close()

    def __del__(self):
        try:
            self.close()
        except Exception:
            pass

    # --- helpers ----------------------------------------------------------
    def _take_json(self, ptr):
        if not ptr:
            return None
        try:
            text = ctypes.cast(ptr, ctypes.c_char_p).value.decode()
        finally:
            self._lib.uc_free_string(ptr)
        return json.loads(text)

    # --- API --------------------------------------------------------------
    def version(self):
        return self._lib.uc_version().decode()

    def set_timeouts(self, connect_s, transfer_s):
        self._lib.uc_set_timeouts(self._c, connect_s, transfer_s)

    def upload(self, file_path, stored_name=""):
        return bool(self._lib.uc_upload(
            self._c, _fsencode(file_path), _fsencode(stored_name)))

    def wait_extract(self, timeout_s=120):
        """List of extracted items (dicts), [] if none, or None on failure."""
        return self._take_json(self._lib.uc_wait_extract_json(self._c, timeout_s))

    def start_all(self):
        return bool(self._lib.uc_start_all(self._c))

    def get_rate(self):
        """List of per-item progress dicts, or None on failure."""
        return self._take_json(self._lib.uc_get_rate_json(self._c))

    def clear(self):
        return bool(self._lib.uc_clear(self._c))

    def reboot(self):
        """Reboot the controller (usually after the upgrade completes)."""
        return bool(self._lib.uc_reboot(self._c))

    def last_error(self):
        err = self._lib.uc_last_error(self._c)
        return err.decode() if err else ""
