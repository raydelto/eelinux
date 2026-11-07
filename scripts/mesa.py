"""Fetch checked Mesa sources for PS2 driver development. No software backend.

The PS2 Gallium/GLX driver is not implemented yet. Source acquisition is not a
driver build, and no accelerated libGL is installed by this command.
"""
from pathlib import Path
import argparse, hashlib, json
from common import download, extract
ROOT = Path(__file__).resolve().parents[1]


def fetch(work):
    data = json.loads((ROOT/'userland/mesa-sources.json').read_text())
    work = work.resolve()
    work.mkdir(parents=True, exist_ok=True)
    archive = work/('mesa-'+data['version']+'.tar.xz')
    download(data['url'], archive, data['sha256'])
    container = work/('mesa-'+data['version'])
    if not container.exists():
        extract(archive, container)
    source = container/('mesa-'+data['version'])
    if hashlib.sha256((source/data['license_file']).read_bytes()).hexdigest() != data['license_sha256']:
        raise ValueError('Mesa license/source verification failed')
    print(f'Verified Mesa sources: {source}')
    print('PS2 Gallium/GLX driver is unfinished; no libGL or software fallback installed.')


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('action', choices=['fetch'])
    p.add_argument('--work', type=Path, required=True)
    args = p.parse_args()
    fetch(args.work)


if __name__ == '__main__':
    main()
