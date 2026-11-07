"""Track and stop only a process launched for this emulator session."""
from __future__ import annotations

import ctypes
import json
import os
from pathlib import Path
import signal
import subprocess

from emulator_disk import regular_path


def _windows_handle(pid, rights):
    from ctypes import wintypes
    api = ctypes.WinDLL("kernel32", use_last_error=True)
    api.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
    api.OpenProcess.restype = wintypes.HANDLE
    api.CloseHandle.argtypes = [wintypes.HANDLE]
    handle = api.OpenProcess(rights, False, pid)
    return api, handle


def _windows_identity(api, handle):
    from ctypes import wintypes
    api.QueryFullProcessImageNameW.argtypes = [wintypes.HANDLE, wintypes.DWORD,
                                             wintypes.LPWSTR, ctypes.POINTER(wintypes.DWORD)]
    api.GetProcessTimes.argtypes = [wintypes.HANDLE, *([ctypes.POINTER(wintypes.FILETIME)] * 4)]
    name = ctypes.create_unicode_buffer(32768)
    size = wintypes.DWORD(len(name))
    times = [wintypes.FILETIME() for _ in range(4)]
    if not api.QueryFullProcessImageNameW(handle, 0, name, ctypes.byref(size)) or not api.GetProcessTimes(handle, *(ctypes.byref(t) for t in times)):
        raise OSError("cannot verify emulator process identity")
    creation = (times[0].dwHighDateTime << 32) | times[0].dwLowDateTime
    return {"program": os.path.normcase(str(Path(name.value).resolve())), "creation": creation}


def process_identity(pid: int):
    if pid <= 0:
        raise ValueError("invalid emulator PID")
    if os.name == "nt":
        api, handle = _windows_handle(pid, 0x1000)
        if not handle:
            return None
        try:
            return _windows_identity(api, handle)
        finally:
            api.CloseHandle(handle)
    try:
        process = Path("/proc") / str(pid)
        # Process names may contain spaces or closing parentheses.
        fields = (process / "stat").read_text().rsplit(")", 1)[1].split()
        return {"program": str((process / "exe").resolve(strict=True)), "creation": int(fields[19])}
    except FileNotFoundError:
        return None


def run_process(command, *, cwd, env, timeout):
    record = regular_path(Path(cwd) / "running-process.json", must_exist=False)
    child = subprocess.Popen(command, cwd=cwd, env=env)
    metadata = {"pid": child.pid, "identity": None, "active": True}
    try:
        metadata["identity"] = process_identity(child.pid)
        record.write_text(json.dumps(metadata, indent=2) + "\n")
        code = child.wait(timeout=timeout)
        return subprocess.CompletedProcess(command, code)
    except BaseException:
        if child.poll() is None:
            child.kill()
        child.wait()
        raise
    finally:
        metadata["active"] = False
        record.write_text(json.dumps(metadata, indent=2) + "\n")


def stop_process(directory: Path) -> bool:
    record = regular_path(directory / "running-process.json", must_exist=True)
    metadata = json.loads(record.read_text())
    if not metadata.get("active"):
        return False
    pid, expected = metadata.get("pid"), metadata.get("identity")
    if not isinstance(pid, int) or isinstance(pid, bool) or pid <= 0 or not isinstance(expected, dict):
        raise ValueError("invalid running emulator record")
    if os.name == "nt":
        from ctypes import wintypes
        api, handle = _windows_handle(pid, 0x1001)  # query + terminate the same object
        if not handle:
            return False
        try:
            if _windows_identity(api, handle) != expected:
                raise ValueError("emulator PID was reused or its identity changed; refusing to stop it")
            api.TerminateProcess.argtypes = [wintypes.HANDLE, wintypes.UINT]
            if not api.TerminateProcess(handle, 1):
                raise OSError("could not stop the verified emulator process")
        finally:
            api.CloseHandle(handle)
    else:
        descriptor = os.pidfd_open(pid) if hasattr(os, "pidfd_open") else None
        try:
            if process_identity(pid) != expected:
                raise ValueError("emulator PID was reused or its identity changed; refusing to stop it")
            if descriptor is not None and hasattr(signal, "pidfd_send_signal"):
                signal.pidfd_send_signal(descriptor, signal.SIGTERM)
            else:
                os.kill(pid, signal.SIGTERM)
        finally:
            if descriptor is not None:
                os.close(descriptor)
    return True
