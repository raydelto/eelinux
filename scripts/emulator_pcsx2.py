#!/usr/bin/env python3
"""Prepare and build the pinned experimental PCSX2 interpreter MMU port on Linux.

Requires native host dependencies, official Qt6.10.1, and the small dependencies
recorded in emulator/pcsx2-linux-build.json. Build validates source without edits;
prepare-patches explicitly applies reviewed extra patches to a private checkout.
Neither command changes BIOS or an installed/qualified emulator runtime.
SPDX-License-Identifier: GPL-2.0-only
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import shutil
import subprocess
import sys
import tempfile

from common import ROOT, linux_only

MANIFEST = ROOT / 'emulator/pcsx2-linux-build.json'


def sha256(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def capture(command, **kwargs):
    return subprocess.check_output(list(map(str, command)), text=True, stderr=subprocess.STDOUT, **kwargs)


def native_directory(path):
    path = Path(path).resolve()
    if str(path).startswith('/mnt/') or any(char.isspace() for char in str(path)):
        raise RuntimeError('Use native Linux source/build/dependency paths without whitespace')
    return path


def source_guard(source, data, *, allow_unapplied=False):
    """Reconstruct every allowed edit from pinned git objects, without mutation."""
    source=Path(source)
    revision=capture(['git','-C',source,'rev-parse','HEAD']).strip()
    if revision!=data['source_revision']:
        raise RuntimeError('PCSX2 checkout differs from pinned v2.6.3 revision')
    mmu_files=set(data['modified_source_files'])
    patch=ROOT/data['mmu_patch']
    if sha256(patch)!=data['mmu_patch_sha256']:
        raise RuntimeError('MMU patch differs from build manifest')
    additions=[];all_files=set(mmu_files)
    for record in data.get('additional_patches',[]):
        path=ROOT/record['path'];names=record['source_files']
        if (not path.resolve().is_relative_to(ROOT.resolve())
                or sha256(path)!=record['sha256']):
            raise RuntimeError('Additional PCSX2 patch differs from build manifest')
        if not names or len(set(names))!=len(names) or all_files.intersection(names):
            raise RuntimeError('Additional patches must declare distinct files outside the reviewed MMU files')
        for name in names:
            relative=PurePosixPath(name)
            if not relative.parts or relative.is_absolute() or '..' in relative.parts or '\\' in name or relative.as_posix()!=name:
                raise RuntimeError('Unsafe additional PCSX2 source path')
        all_files.update(names);additions.append(record)
    modified=set()
    for line in capture(['git','-C',source,'status','--porcelain','--untracked-files=all']).splitlines():
        if len(line)<4 or ' -> ' in line:
            raise RuntimeError('Unexpected renamed or malformed source status')
        modified.add(line[3:])
    if not mmu_files.issubset(modified) or not modified.issubset(all_files):
        raise RuntimeError('Source has changes beyond the exact reviewed MMU and additional patches')
    git_apply=['git','-c','core.autocrlf=false','-c','core.eol=lf','apply']
    patch_records=[]
    with tempfile.TemporaryDirectory(prefix='eelinux-pcsx2-source-') as temporary:
        pristine=Path(temporary)
        for name in all_files:
            result=subprocess.run(['git','-C',str(source),'show',f'{revision}:{name}'],
                                  stdout=subprocess.PIPE,stderr=subprocess.PIPE)
            if result.returncode:
                if name=='pcsx2/R5900MMU.h':continue
                raise RuntimeError('Missing pristine source file: '+name)
            destination=pristine/name;destination.parent.mkdir(parents=True,exist_ok=True)
            destination.write_bytes(result.stdout)
        subprocess.run([*git_apply,str(patch)],cwd=pristine,check=True)
        mmu_expected={name:sha256(pristine/name) for name in mmu_files}
        for name,checksum in mmu_expected.items():
            if (source/name).is_symlink() or sha256(source/name)!=checksum:
                raise RuntimeError('MMU source differs from byte-exact reviewed patch output: '+name)
        for record in additions:
            names=record['source_files'];original={name:sha256(pristine/name) for name in names}
            subprocess.run([*git_apply,str(ROOT/record['path'])],cwd=pristine,check=True)
            expected={name:sha256(pristine/name) for name in names}
            if any(original[name]==expected[name] for name in names):
                raise RuntimeError('Additional patch source list contains an unchanged file')
            actual={}
            for name in names:
                if (source/name).is_symlink():raise RuntimeError('Linked PCSX2 source file: '+name)
                actual[name]=sha256(source/name)
            if actual==expected:
                applied=True
            elif actual==original:
                if not allow_unapplied:
                    raise RuntimeError('Required additional patch is not applied; use prepare-patches on a private checkout: '+record['path'])
                applied=False
            else:
                raise RuntimeError('Additional source differs from byte-exact reviewed patch output: '+record['path'])
            if {name for name in names if name in modified}!=(set(names) if applied else set()):
                raise RuntimeError('Additional source git status differs from its reviewed patch state')
            patch_records.append({**record,'applied':applied,'source_sha256':actual,
                                  'pristine_source_sha256':original,'patched_source_sha256':expected})
        if {name:sha256(pristine/name) for name in mmu_files}!=mmu_expected:
            raise RuntimeError('Additional patch unexpectedly changed reviewed MMU source')
        if {path.relative_to(pristine).as_posix() for path in pristine.rglob('*') if path.is_file()}!=all_files:
            raise RuntimeError('Reviewed patches wrote undeclared source files')
    subprocess.run(['git','-C',str(source),'apply','--reverse','--check',str(patch)],check=True)
    hashes={name:sha256(source/name) for name in sorted(all_files)}
    return {'source_revision':revision,'mmu_source_sha256':{name:hashes[name] for name in sorted(mmu_files)},
            'reviewed_source_sha256':hashes,'additional_patches':patch_records}


def prepare_patches(args):
    """Explicitly add reviewed non-MMU fixes to a caller-selected private checkout."""
    linux_only()
    data=json.loads(MANIFEST.read_text());source=native_directory(args.source)
    report_path=(Path(args.report).resolve() if args.report else source.parent/(source.name+'-eelinux-source.json'))
    if report_path.is_relative_to(source):
        raise RuntimeError('Patch preparation report must remain outside the source checkout')
    before=source_guard(source,data,allow_unapplied=True)
    pending=[ROOT/item['path'] for item in before['additional_patches'] if not item['applied']]
    if pending:
        command=['git','-c','core.autocrlf=false','-c','core.eol=lf','-C',str(source),'apply']
        subprocess.run([*command,'--check',*map(str,pending)],check=True)
        if source_guard(source,data,allow_unapplied=True)!=before:
            raise RuntimeError('PCSX2 source changed while preparing additional patches')
        subprocess.run([*command,*map(str,pending)],check=True)
    after=source_guard(source,data)
    report={'result':'reviewed patches prepared','source':str(source),'manifest_sha256':sha256(MANIFEST),
            'builder_sha256':sha256(Path(__file__)),'patches_applied':[str(path.relative_to(ROOT)) for path in pending],
            **after,'compiled':False,'qualified_runtime_modified':False}
    report_path.parent.mkdir(parents=True,exist_ok=True)
    report_path.write_text(json.dumps(report,indent=2)+'\n')
    print('Prepared reviewed additional patches in caller-selected checkout: '+str(source))
    return report


def build(args):
    linux_only()
    data = json.loads(MANIFEST.read_text())
    source, output, qt, dependencies = map(native_directory,
                                          (args.source, args.output, args.qt, args.dependencies))
    if source == output or output.is_relative_to(source):
        raise RuntimeError('PCSX2 build output must be outside the source checkout')
    if args.jobs not in (1, 2):
        raise RuntimeError('Use --jobs 1 or 2 for the supported bounded-memory build')
    for tool in ('git', 'cmake', 'ninja', 'clang-19', 'clang++-19', 'lld-19', 'nasm', 'pkg-config'):
        if not shutil.which(tool):
            raise RuntimeError(f'Missing host dependency: {tool}')
    source_before=source_guard(source,data)
    revision=source_before['source_revision'];patch=ROOT/data['mmu_patch']
    qt_version = capture([qt / 'bin/qmake', '-query', 'QT_VERSION']).strip()
    if qt_version != data['qt']['version']:
        raise RuntimeError('Use the recorded official Qt6.10.1 binary package')
    for component in ('Qt6LinguistTools', 'Qt6WidgetsPrivate'):
        if not (qt / 'lib/cmake' / component / (component + 'Config.cmake')).is_file():
            raise RuntimeError(f'Qt component missing: {component}')
    output.mkdir(parents=True, exist_ok=True)
    command = ['cmake', '-S', str(source), '-B', str(output), '-G', 'Ninja',
               '-DCMAKE_BUILD_TYPE=Release', '-DCMAKE_C_COMPILER=clang-19',
               '-DCMAKE_CXX_COMPILER=clang++-19', f'-DCMAKE_PREFIX_PATH={dependencies};{qt}',
               *data['cmake_options']]
    log_path = output / 'eelinux-build.log'
    with log_path.open('a', buffering=1) as log:
        for invocation in (command, ['cmake', '--build', str(output), '--parallel', str(args.jobs)]):
            print('+ ' + ' '.join(invocation), flush=True)
            log.write('Running: ' + repr(invocation) + '\n')
            subprocess.run(invocation, stdout=log, stderr=subprocess.STDOUT, check=True)
    source_after=source_guard(source,data)
    if source_before!=source_after:
        raise RuntimeError('Reviewed source changed during compilation; rebuild from reviewed inputs')
    executable = output / 'bin/pcsx2-qt'
    environment = dict(os.environ, QT_QPA_PLATFORM='offscreen')
    environment['LD_LIBRARY_PATH'] = f'{dependencies}/lib:{qt}/lib'
    help_result = subprocess.run([str(executable), '-help'], env=environment, capture_output=True,
                                 text=True, timeout=15)
    help_text = help_result.stdout + help_result.stderr
    if help_result.returncode not in (0, 1) or '-elf' not in help_text or '-testconfig' not in help_text:
        raise RuntimeError('Built PCSX2 help/capability check failed')
    (output / 'eelinux-help.txt').write_text(help_text)
    report = {'source_revision': revision, 'manifest_sha256': sha256(MANIFEST),
              'mmu_patch_sha256': sha256(patch), 'mmu_source_sha256': source_after['mmu_source_sha256'],
              'additional_patches':source_after['additional_patches'],
              'reviewed_source_sha256':source_after['reviewed_source_sha256'],
              'builder_sha256':sha256(Path(__file__)),
              'executable': str(executable), 'bytes': executable.stat().st_size,
              'sha256': sha256(executable), 'qt_version': qt_version,
              'compiler': capture(['clang++-19', '--version']).splitlines()[0],
              'cmake_arguments': command, 'build_log_sha256': sha256(log_path),
              'help_exit_code': help_result.returncode,
              'help_sha256': hashlib.sha256(help_text.encode()).hexdigest(),
              'boot_tested': False, 'linux_boot_pass': None,
              'full_linux_mmu_qualified': False}
    report_path = Path(args.report) if args.report else output / 'eelinux-build.json'
    report_path.parent.mkdir(parents=True, exist_ok=True)
    report_path.write_text(json.dumps(report, indent=2) + '\n')
    print(f'Built experimental PCSX2: {executable}\nReport: {report_path}')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands=parser.add_subparsers(dest='command',required=True)
    prepare=commands.add_parser('prepare-patches',help='Explicitly patch a caller-selected private MMU checkout; no compilation')
    prepare.add_argument('--source',required=True,type=Path)
    prepare.add_argument('--report')
    compile_parser=commands.add_parser('build',help='Build only already-prepared byte-exact reviewed source')
    compile_parser.add_argument('--source', required=True, type=Path)
    compile_parser.add_argument('--output', required=True, type=Path)
    compile_parser.add_argument('--qt', required=True, type=Path)
    compile_parser.add_argument('--dependencies', required=True, type=Path)
    compile_parser.add_argument('--jobs', type=int, default=2)
    compile_parser.add_argument('--report')
    try:
        args=parser.parse_args()
        prepare_patches(args) if args.command=='prepare-patches' else build(args)
    except (RuntimeError, OSError, subprocess.SubprocessError, ValueError) as error:
        print(f'emulator-pcsx2: {error}', file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
