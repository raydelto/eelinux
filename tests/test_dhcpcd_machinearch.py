"""Compile dhcpcd's patched CPU-info function against real multiline inputs."""
from pathlib import Path
import hashlib
import json
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
REFERENCE = ROOT / 'userland/vendor/dhcpcd-10.5.2'


class MachineArch(unittest.TestCase):
    def test_reference_and_patch_match_locked_release(self):
        lock = json.loads((ROOT / 'userland/dhcpcd-sources.json').read_text())
        reference = json.loads((REFERENCE / 'manifest.json').read_text())
        self.assertEqual(lock['archive'], reference['archive'])
        for name, item in reference['files'].items():
            blob = (REFERENCE / name).read_bytes()
            self.assertEqual(len(blob), item['bytes'])
            self.assertEqual(hashlib.sha256(blob).hexdigest(), item['sha256'])
        for item in lock['patches']:
            self.assertEqual(hashlib.sha256((ROOT / item['path']).read_bytes()).hexdigest(), item['sha256'])

    @unittest.skipUnless(shutil.which('cc') and shutil.which('git'), 'requires native C compiler and git')
    def test_actual_function_parses_mips_and_retains_errors_and_bounds(self):
        lock = json.loads((ROOT / 'userland/dhcpcd-sources.json').read_text())
        with tempfile.TemporaryDirectory() as tmp:
            folder = Path(tmp)
            source = folder / 'src/if-linux.c'
            source.parent.mkdir(); shutil.copyfile(REFERENCE / 'src/if-linux.c', source)
            for item in lock['patches']:
                subprocess.run(['git', 'apply', str(ROOT / item['path'])], cwd=folder, check=True)
            text = source.read_text()
            start = text.rindex('int\nif_machinearch(')
            function = text[start:text.index('\n#endif', start)]
            harness = r'''
#define _GNU_SOURCE
#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
static const char *mproc, *input;
static FILE *cpuinfo(const char *name, const char *mode) {
    assert(!strcmp(name,"/proc/cpuinfo") && !strcmp(mode,"r"));
    if (!input) { errno=ENOENT; return NULL; }
    return fmemopen((void *)input, strlen(input), "r");
}
#define fopen cpuinfo
''' + function + r'''
#undef fopen
int main(void) {
    char out[32];
    mproc="system type";
    input="processor : 0\nsystem type\t: PlayStation 2  \nCPU model : R5900\n";
    assert(if_machinearch(out,sizeof(out))==13 && !strcmp(out,"PlayStation 2"));
    assert(if_machinearch(out,5)==13 && !strcmp(out,"Play"));
    input="system typeX : wrong\nmalformed\nsystem type: \t\nsystem type :  PS2\t\n";
    assert(if_machinearch(out,sizeof(out))==3 && !strcmp(out,"PS2"));
    mproc="model name"; input="model name : Example CPU with spaces\n";
    assert(if_machinearch(out,sizeof(out))==23 && !strcmp(out,"Example CPU with spaces"));
    input="model nameX: wrong\nmodel name : \n";
    assert(if_machinearch(out,sizeof(out))==-1 && errno==ESRCH);
    input=NULL; assert(if_machinearch(out,sizeof(out))==-1 && errno==ENOENT);
    mproc=NULL; assert(if_machinearch(out,sizeof(out))==-1 && errno==EINVAL);
    return 0;
}
'''
            c = folder / 'test.c'; executable = folder / 'test'
            c.write_text(harness)
            subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror',
                '-fsanitize=address,undefined', str(c), '-o', str(executable)], check=True)
            subprocess.run([str(executable)], check=True)

    def test_manual_client_uses_existing_gnu_hook(self):
        config = (ROOT / 'rootfs/etc/dhcpcd.conf').read_text()
        directive = [line.split(None, 1)[1] for line in config.splitlines() if line.startswith('script ')]
        self.assertEqual(directive, ['/usr/lib/eelinux/dhcpcd-hook'])
        hook = ROOT / 'rootfs' / directive[0].lstrip('/')
        self.assertTrue(hook.is_file())
        self.assertEqual(hook.read_text().splitlines()[0], '#!/bin/bash')


if __name__ == '__main__':
    unittest.main()
