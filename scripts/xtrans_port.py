"""Bound Xtrans' uname copy by the actual nodename object size."""
from pathlib import Path
import argparse
import difflib
import hashlib

ROOT = Path(__file__).resolve().parents[1]

OLD = '''    uname (&name);
    len = strlen (name.nodename);
    if (len >= maxlen) len = maxlen - 1;
    strncpy (buf, name.nodename, len);
    buf[len] = '\\0';'''
NEW = '''    if (maxlen <= 0 || uname (&name) < 0)
        return -1;
    len = strnlen (name.nodename, sizeof (name.nodename));
    if (len >= maxlen) len = maxlen - 1;
    memcpy (buf, name.nodename, len);
    buf[len] = '\\0';'''


def adapt(text):
    if text.count(OLD) != 1:
        raise ValueError('Xtrans 1.5.0 hostname source changed')
    return text.replace(OLD, NEW)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('source', type=Path)
    args = p.parse_args()
    file = args.source/'Xtrans.c'
    old = file.read_text()
    new = adapt(old)
    patch = ''.join(difflib.unified_diff(old.splitlines(keepends=True),
        new.splitlines(keepends=True), fromfile='a/Xtrans.c', tofile='b/Xtrans.c'))
    dest = ROOT/'userland/patches/xtrans-bounded-hostname.patch'
    if dest.exists() and dest.read_text() != patch:
        raise ValueError('Existing Xtrans patch differs')
    dest.write_text(patch)
    print('source', hashlib.sha256(old.encode()).hexdigest())
    print('patch', hashlib.sha256(patch.encode()).hexdigest())
    print('result', hashlib.sha256(new.encode()).hexdigest())
    file.write_text(new)


if __name__ == '__main__':
    main()
