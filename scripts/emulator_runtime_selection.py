"""Select the hash-checked project Windows MMU runtime for Linux sessions."""
from __future__ import annotations
import hashlib
import json
from pathlib import Path

from emulator_disk import regular_path
from emulator_pcsx2_windows import inventory

ROOT = Path(__file__).resolve().parents[1]


def digest(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def select_runtime(requested: Path, host: str, *, stock=False, root=ROOT):
    """Keep installed PCSX2 intact; validate the complete local runtime package."""
    if host != 'windows' or stock or requested.name.lower() != 'pcsx2-qt.exe':
        return requested, None
    folder = root / 'build/emulator/pcsx2-patched-windows/runtime'
    evidence = folder / 'eelinux-runtime.json'
    if not evidence.exists():
        raise ValueError(
            'project Windows MMU runtime is missing; build it with '
            'scripts/emulator_pcsx2_windows.py before launching Linux '
            '(--stock-pcsx2 explicitly selects the unqualified installed runtime)')
    regular_path(evidence, must_exist=True)
    record = json.loads(evidence.read_text())
    lock = root / 'emulator/pcsx2-windows-build.json'
    specification = json.loads(lock.read_text())
    program = regular_path(folder / 'pcsx2-qt.exe', must_exist=True)
    if (record.get('source_revision') != specification['source_revision'] or
            record.get('manifest_sha256') != digest(lock) or
            record.get('builder_sha256') != digest(root / 'scripts/emulator_pcsx2_windows.py') or
            record.get('mmu_patch_sha256') != specification['mmu_patch_sha256'] or
            record.get('mmu_patch_sha256') != digest(root / 'emulator/patches/pcsx2-2.6.3-mmu.patch') or
            record.get('windows_portability_patch_sha256') != digest(root / specification['windows_portability_patch']) or
            record.get('sha256') != digest(program) or record.get('bytes') != program.stat().st_size or
            record.get('runtime_files_sha256') != inventory(folder)):
        raise ValueError('project Windows runtime changed; rebuild it before launching Linux')
    return program, {'requested_program': str(requested), 'selected_program': str(program),
                     'executable_sha256': record['sha256'],
                     'runtime_manifest_sha256': digest(evidence),
                     'mmu_patch_sha256': record['mmu_patch_sha256']}
