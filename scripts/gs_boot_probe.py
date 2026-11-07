#!/usr/bin/env python3
"""Build a stopping GS red-screen diagnostic, separate from the Linux kernel."""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile

from emulator_disk import regular_path
from emulator import validate_kernel

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cross-prefix', required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    output = regular_path(args.output, must_exist=False)
    report = regular_path(Path(str(output) + '.json'), must_exist=False)
    if output.exists() or report.exists():
        parser.error('choose a new output; existing artifacts are preserved')
    source = ROOT / 'boot/gs-probe.S'
    digest = hashlib.sha256(source.read_bytes()).hexdigest()
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='eelinux-gs-probe-') as temporary:
        folder = Path(temporary)
        linker = folder / 'probe.ld'
        linker.write_text('''OUTPUT_ARCH(mips)
ENTRY(_start)
PHDRS { text PT_LOAD FLAGS(5); }
SECTIONS {
  . = 0x00800000;
  .text : { *(.text*) } :text
  .MIPS.abiflags : { *(.MIPS.abiflags) } :text
  .reginfo : { *(.reginfo) } :text
  /DISCARD/ : { *(.comment) *(.note*) *(.pdr) *(.gnu.attributes) }
}
''')
        obj, elf = folder / 'probe.o', folder / 'probe.elf'
        subprocess.run([args.cross_prefix + 'gcc', '-march=r5900', '-mabi=32',
                        '-msoft-float', '-mno-abicalls', '-fno-pic', '-c', str(source),
                        '-o', str(obj)], check=True)
        subprocess.run([args.cross_prefix + 'ld', '-m', 'elf32ltsmip', '-T', str(linker),
                        str(obj), '-o', str(elf)], check=True)
        evidence = validate_kernel(elf)
        disassembly = subprocess.check_output([args.cross_prefix + 'objdump', '-d', str(elf)], text=True)
        if disassembly.count('\tsd\t') != 2 or '\tsw\t' in disassembly:
            raise RuntimeError('diagnostic must contain exactly two 64-bit GS stores')
        data = elf.read_bytes()
        metadata = {'purpose': 'stopping red GS background diagnostic; not a Linux boot image',
                    'source_sha256': digest, 'image_sha256': hashlib.sha256(data).hexdigest(),
                    'image_bytes': len(data), 'elf': evidence,
                    'compiler': subprocess.check_output([args.cross_prefix + 'gcc', '--version'], text=True).splitlines()[0],
                    'inherited_video_timing': True, 'gs_store_width_bits': 64,
                    'expected_display': 'solid red until power cycle', 'real_hardware_tested': False}
        with output.open('xb') as stream:
            stream.write(data)
        with report.open('x') as stream:
            json.dump(metadata, stream, indent=2)
            stream.write('\n')
        Path(str(output) + '.disasm').write_text(disassembly)
    print(f'Built {output}: stopping red-screen diagnostic')


if __name__ == '__main__':
    main()
