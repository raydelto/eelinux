"""Export and verify the native 2D delta against the complete generic series."""
from pathlib import Path
import argparse, difflib, hashlib, json, re, subprocess

ROOT = Path(__file__).resolve().parents[1]

def effective_patch(patch, tree):
    sections = re.split(r'(?m)(?=^--- )', patch.read_text())
    kept = []; duplicates = []
    for section in sections:
        if section.startswith('--- /dev/null\n+++ b/'):
            lines = section.splitlines(keepends=True)
            path = tree / lines[1][6:].strip()
            contents = ''.join(line[1:] for line in lines[3:] if line.startswith('+'))
            if path.exists() and path.read_text() == contents:
                duplicates.append(lines[1][6:].strip()); continue
        kept.append(section)
    effective = tree / 'effective.patch'; effective.write_text(''.join(kept))
    return effective, duplicates

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--work', type=Path, required=True)
    parser.add_argument('--build-report', type=Path, required=True)
    parser.add_argument('--toolchain-prefix', required=True)
    parser.add_argument('--extension',choices=('plane-composite','alpha','filters','formats','rgba','coverage','rgba-transfer','bgra','hardware-2d'),default='plane-composite')
    args = parser.parse_args(); args.work.mkdir(parents=True, exist_ok=False)
    report = {'result': 'passed', 'series': {}}
    flags = json.loads(args.build_report.read_text())['flags']
    for kind, target in [('mesa', 'src/gallium/drivers/ps2'), ('xorg', 'hw/xfree86/drivers/ps2')]:
        tree = args.work / kind; tree.mkdir()
        export_name = kind + '-ps2-'+args.extension+'.patch'
        pending = sorted(p for p in (ROOT / 'userland/patches').glob(kind + '-ps2-*.patch')
                         if p.name != export_name)
        applied = []; duplicates = {}
        while pending:
            for patch in pending:
                effective, already_present = effective_patch(patch, tree)
                check = subprocess.run(['git', 'apply', '--check', str(effective)], cwd=tree, capture_output=True)
                if not check.returncode:
                    subprocess.run(['git', 'apply', str(effective)], cwd=tree, check=True, capture_output=True)
                    if already_present: duplicates[patch.name] = already_present
                    pending.remove(patch); applied.append(patch.name); break
            else:
                raise RuntimeError('Unresolved prerequisite patches: ' + ', '.join(p.name for p in pending))
        files = {}
        for folder in ('graphics', 'mesa/ps2', 'desktop/xorg') if kind == 'xorg' else ('graphics', 'mesa/ps2'):
            for local in (ROOT / folder).iterdir():
                if local.suffix in ('.c', '.h'): files[local.name] = local
        for local in (ROOT / 'kernel/new-drivers').glob('ps2-gs*.h'): files[local.name] = local
        changes = []; contents = {}
        for name, local in sorted(files.items()):
            path = tree / target / name
            old = path.read_text() if path.exists() else ''
            new = local.read_text(); contents[name] = new
            if old != new:
                changes.append(''.join(difflib.unified_diff(old.splitlines(keepends=True), new.splitlines(keepends=True),
                    fromfile='a/' + target + '/' + name if old else '/dev/null', tofile='b/' + target + '/' + name)))
        patch = ROOT / 'userland/patches' / export_name
        patch.write_text(''.join(changes))
        subprocess.run(['git', 'apply', '--check', str(patch)], cwd=tree, check=True)
        subprocess.run(['git', 'apply', str(patch)], cwd=tree, check=True)
        for name, expected in contents.items():
            if (tree / target / name).read_text() != expected: raise RuntimeError('Export mismatch: ' + name)
        changed_sources = [name for name in contents if name in
            ('ps2_plane.c', 'ps2_composite.c', 'ps2_filter.c', 'ps2_rop.c', 'ps2_gs_lut.c', 'ps2_gs_bitmap.c',
             'ps2_gs_transfer.c', 'ps2_glyph.c', 'ps2_driver.c', 'ps2_image_gc.c', 'ps2_gc.c',
             'ps2_blit_exa.c', 'ps2_surface_exa.c')]
        if args.extension in ('rgba','coverage','rgba-transfer','bgra','hardware-2d'):
            changed_sources=[name for name in contents if name.endswith('.c') and name in
                {line.split('+++ b/'+target+'/')[1].split('\n')[0] for line in changes}]
        folder = tree / target
        with (tree / 'compile.log').open('w') as log:
            for composite in (True, False):
                include = tree / ('composite.h' if composite else 'no-composite.h')
                include.write_text('#include <xorg-server.h>\n' + ('#define COMPOSITE\n' if composite else '#undef COMPOSITE\n'))
                options = [f for f in flags if f != '-DCOMPOSITE']
                for name in changed_sources:
                    subprocess.run([args.toolchain_prefix + 'gcc', '-I' + str(folder), *options,
                        '-include', str(include), '-c', str(folder / name), '-o',
                        str(tree / (name + ('.composite.o' if composite else '.no-composite.o')))],
                        check=True, stdout=log, stderr=log)
        report['series'][kind] = {'prerequisites': applied, 'patch': patch.relative_to(ROOT).as_posix(),
            'already_identical_additions_in_prior_series': duplicates,
            'patch_sha256': hashlib.sha256(patch.read_bytes()).hexdigest(),
            'source_equal_files': len(contents), 'compiled_sources': changed_sources,
            'COMPOSITE_enabled_and_disabled': True}
    (args.work / 'patches.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))

if __name__ == '__main__': main()
