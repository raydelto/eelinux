"""Behavioral networking tests and host-side root-image safety checks."""
from __future__ import annotations

import importlib.util
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tarfile
import tempfile
import unittest
from unittest.mock import patch

REPO = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(REPO / "scripts"))
SPEC = importlib.util.spec_from_file_location("eelinux_rootfs", REPO / "scripts/rootfs.py")
rootfs = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(rootfs)


def small_elf(machine: int = 8, flags: int = 0x20921000,
              fp_abi: int = 3, isa_level: int = 3, extension: int = 6) -> bytes:
    identification = b"\x7fELF\x01\x01\x01" + b"\0" * 9
    header = struct.pack("<16sHHIIIIIHHHHHH", identification, 2, machine,
                         1, 0, 52, 0, flags, 52, 32, 1, 40, 0, 0)
    program = struct.pack("<8I", 0x70000003, 84, 0, 0, 24, 24, 4, 8)
    attributes = struct.pack("<H6B4I", 0, isa_level, 0, 1, 0, 0,
                             fp_abi, extension, 0, 0, 0)
    return header + program + attributes


class RootImageValidation(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.folder = Path(self.temporary.name)

    def tearDown(self):
        self.temporary.cleanup()

    def test_wrong_architecture_and_abi_are_rejected(self):
        binary = self.folder / "program"
        for machine, flags in ((62, 0), (8, 0x20), (8, 0x2000)):
            binary.write_bytes(small_elf(machine, flags))
            with self.assertRaises(rootfs.BuildError):
                rootfs.elf_info(binary)
        binary.write_bytes(small_elf())
        self.assertEqual(rootfs.elf_info(binary), {"interpreter": None, "needed": []})

    def test_direct_staging_rejects_incompatible_cpu_and_float(self):
        binary = self.folder / "libc.so.6"
        for changes in ({"flags": 0x70001000, "isa_level": 32},
                        {"fp_abi": 1}, {"fp_abi": 5}, {"extension": 3}):
            with self.subTest(changes=changes):
                binary.write_bytes(small_elf(**changes))
                with self.assertRaises(rootfs.BuildError):
                    rootfs.elf_info(binary)

    @unittest.skipIf(os.name == "nt", "requires POSIX symlinks")
    def test_guest_absolute_symlinks_cannot_write_to_host(self):
        tree = self.folder / "tree"
        tree.mkdir()
        (tree / "usr/etc").mkdir(parents=True)
        (tree / "etc").symlink_to("/usr/etc")
        rootfs.install_file(tree, "/etc/test.conf", content="inside guest\n")
        self.assertEqual((tree / "usr/etc/test.conf").read_text(), "inside guest\n")
        (tree / "escape").symlink_to("../../host")
        with self.assertRaises(rootfs.BuildError):
            rootfs.guest_path(tree, "/escape/file")

    def test_incomplete_userland_does_not_build_an_image(self):
        stage = self.folder / "stage"
        output = self.folder / "output"
        stage.mkdir()
        result = subprocess.run([sys.executable, str(REPO / "scripts/rootfs.py"),
                                 "--staging", str(stage), "--output-dir", str(output)],
                                capture_output=True, text=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("incomplete staged userland", result.stderr)
        self.assertFalse(output.exists())

    def test_pam_policy_rejects_missing_includes_modules_and_cycles(self):
        tree = self.folder / "pam-root"
        (tree / "etc/pam.d").mkdir(parents=True)
        (tree / "lib/security").mkdir(parents=True)
        (tree / "etc/pam.d/login").write_text("auth include system-auth\n")
        (tree / "etc/pam.d/passwd").write_text("password include system-auth\n")
        with patch.object(rootfs, "find_command", return_value="/bin/login"), \
                patch.object(rootfs, "elf_info", return_value={"needed": ["libpam.so.0"]}):
            with self.assertRaisesRegex(rootfs.BuildError, "missing PAM policy"):
                rootfs.validate_login_policy(tree)
            (tree / "etc/pam.d/system-auth").write_text("auth required pam_unix.so\n")
            with self.assertRaisesRegex(rootfs.BuildError, "missing PAM module"):
                rootfs.validate_login_policy(tree)
            (tree / "lib/security/pam_unix.so").write_bytes(b"module fixture")
            rootfs.validate_login_policy(tree)
            (tree / "etc/pam.d/system-auth").write_text("auth include login\n")
            with self.assertRaisesRegex(rootfs.BuildError, "PAM include cycle"):
                rootfs.validate_login_policy(tree)

    def test_tar_is_deterministic_and_preserves_permissions(self):
        tree = self.folder / "tree"
        tree.mkdir()
        secret = tree / "shadow"
        secret.write_text("root:!\n")
        secret.chmod(0o600)
        one, two = self.folder / "one.tar", self.folder / "two.tar"
        rootfs.create_tar(tree, one, 946684800)
        os.utime(secret, (1700000000, 1700000000))
        rootfs.create_tar(tree, two, 946684800)
        self.assertEqual(one.read_bytes(), two.read_bytes())
        with tarfile.open(one) as tar:
            item = tar.getmember("./shadow")
            self.assertEqual((item.uid, item.gid, item.mtime), (0, 0, 946684800))
            if os.name != "nt":
                self.assertEqual(item.mode, 0o600)

    @unittest.skipUnless(sys.platform == "linux", "requires Linux executable permissions")
    def test_busybox_cannot_replace_gnu_userland(self):
        stage = self.folder / "stage"
        (stage / "bin").mkdir(parents=True)
        for command in rootfs.GNU_COMMANDS + rootfs.SYSTEM_COMMANDS:
            binary = stage / "bin" / command
            binary.write_bytes(small_elf())
            binary.chmod(0o755)
        (stage / "bin/cat").unlink()
        (stage / "bin/cat").symlink_to("busybox")
        with self.assertRaisesRegex(rootfs.BuildError, "BusyBox payload"):
            rootfs.check_staging(stage, desktop=False)

    @unittest.skipUnless(sys.platform == "linux" and shutil.which("fakeroot") and shutil.which("mke2fs") and shutil.which("e2fsck"), "requires Linux image tools")
    def test_regular_ext4_image_is_reproducible_and_has_console(self):
        # This is an image-assembler fixture; these tiny ELF files are deliberately
        # not claimed to be a runnable Linux userland.
        stage = self.folder / "stage"
        (stage / "bin").mkdir(parents=True)
        (stage / "lib").mkdir()
        for command in rootfs.GNU_COMMANDS + rootfs.SYSTEM_COMMANDS:
            binary = stage / "bin" / command
            binary.write_bytes(small_elf())
            binary.chmod(0o755)
        (stage / "lib/libc.so.6").write_bytes(small_elf())
        (stage / "usr/share/keymaps").mkdir(parents=True)
        (stage / "usr/share/keymaps/uk.map").write_text("keycode 2 = one exclam\n")
        (stage / "THIS_IS_NOT_YOUR_ROOT_FILESYSTEM").write_text("Buildroot staging warning\n")
        (stage / "bin/passwd").chmod(0o4755)
        hashes = []
        for suffix in ("one", "two"):
            output = self.folder / suffix
            result = subprocess.run([sys.executable, str(REPO / "scripts/rootfs.py"),
                                     "--staging", str(stage), "--output-dir", str(output),
                                     "--size-mib", "32", "--no-desktop"], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            hashes.append((output / "SHA256SUMS").read_text())
            self.assertEqual((output / "rootfs.ext4").stat().st_size, 32 * 1024 * 1024)
            from emulator_disk import filesystem_info
            with (output / "rootfs.ext4").open("rb") as stream:
                self.assertEqual(filesystem_info(stream, 32 * 1024 * 1024)["type"], "ext4")
            with tarfile.open(output / "rootfs.tar") as tar:
                self.assertNotIn("./THIS_IS_NOT_YOUR_ROOT_FILESYSTEM", tar.getnames())
                self.assertEqual(tar.getmember("./var/empty").mode, 0o755)
                self.assertEqual(tar.getmember("./var/empty").uid, 0)
                self.assertIn("sshd:x:105:105:", tar.extractfile("./etc/passwd").read().decode())
                self.assertIn("sshd:!:", tar.extractfile("./etc/shadow").read().decode())
                console = tar.getmember("./dev/console")
                self.assertTrue(console.ischr())
                self.assertEqual((console.devmajor, console.devminor), (5, 1))
                self.assertEqual(tar.getmember("./etc/shadow").mode, 0o600)
                self.assertEqual(tar.getmember("./etc/securetty").mode, 0o644)
                self.assertEqual(tar.getmember("./etc/resolv.conf").linkname, "/run/resolv.conf")
                self.assertEqual(tar.getmember("./bin/passwd").mode, 0o4755)
                self.assertEqual(tar.getmember("./etc/rc.d/rc2.d/S20network").linkname, "../../init.d/network")
                self.assertEqual(tar.getmember("./etc/rc.d/rcS.d/S15keyboard").linkname, "../../init.d/keyboard")
                self.assertIn("MODE=576p\n", tar.extractfile("./etc/default/ps2-video").read().decode())
                self.assertEqual(tar.getmember("./etc/rc.d/rc6.d/S99halt").linkname, "../../init.d/halt")
        self.assertEqual(hashes[0], hashes[1])

        # The optional USB service must survive assembly, and its filesystem
        # must reserve real GDT space for mounted online expansion.
        import ps2_media
        ps2_media.configure_usb_expansion(stage,True)
        output=self.folder/'usb'
        result=subprocess.run([sys.executable,str(REPO/'scripts/rootfs.py'),
            '--staging',str(stage),'--output-dir',str(output),'--size-mib','32',
            '--no-desktop','--usb-auto-expand'],capture_output=True,text=True)
        self.assertEqual(result.returncode,0,result.stdout+result.stderr)
        image=output/'rootfs.ext4'
        with image.open('rb') as stream:
            info=filesystem_info(stream,32*1024*1024)
            self.assertTrue(info['online_resize_metadata_reserved'])
            stream.seek(1024);superblock=stream.read(1024)
        self.assertTrue(struct.unpack_from('<I',superblock,92)[0]&0x10)  # resize_inode
        self.assertGreater(struct.unpack_from('<H',superblock,206)[0],0)  # reserved GDT
        import io
        probe=bytearray(bytes(1024)+superblock)
        struct.pack_into('<I',probe,1024+92,struct.unpack_from('<I',superblock,92)[0]|0x80)
        with self.assertRaisesRegex(ValueError,'supported ext4 profile'):
            filesystem_info(io.BytesIO(probe),32*1024*1024)
        probe=bytearray(bytes(1024)+superblock);struct.pack_into('<H',probe,1024+206,0)
        with self.assertRaisesRegex(ValueError,'online-resize metadata'):
            filesystem_info(io.BytesIO(probe),32*1024*1024)
        with tarfile.open(output/'rootfs.tar') as tar:
            self.assertEqual(tar.getmember('./expand-usb-rootfs.sh').mode,0o700)
            self.assertEqual(tar.getmember('./etc/rc.d/rcS.d/S19usb-root-expand').linkname,'../../init.d/usb-root-expand')
        with image.open('r+b') as stream:stream.truncate(64*1024*1024)
        subprocess.run(['resize2fs',str(image)],check=True,stdout=subprocess.DEVNULL,stderr=subprocess.PIPE)
        subprocess.run(['e2fsck','-fn',str(image)],check=True,stdout=subprocess.DEVNULL,stderr=subprocess.PIPE)
        with image.open('rb') as stream:
            stream.seek(1024);grown=stream.read(1024)
        self.assertEqual(struct.unpack_from('<I',grown,4)[0]*4096,64*1024*1024)


@unittest.skipUnless(sys.platform == "linux" and shutil.which("bash"), "network behavior tests require Linux Bash")
class EthernetBehavior(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.folder = Path(self.temporary.name)
        self.log = self.folder / "commands.jsonl"
        mock = self.folder / "tool"
        mock.write_text("#!/usr/bin/python3\nimport json, os, sys\nwith open(os.environ['COMMAND_LOG'], 'a') as f:\n    f.write(json.dumps(sys.argv[1:]) + '\\n')\nif '--printpidfile' in sys.argv:\n    print(os.environ['EELINUX_RUN_DIR'] + '/dhcpcd/eth0-4.pid')\n")
        mock.chmod(0o755)
        self.config = self.folder / "network.conf"
        self.resolver = self.folder / "resolv.conf"
        self.environment = os.environ.copy()
        self.environment.update({
            "COMMAND_LOG": str(self.log), "EELINUX_IP": str(mock),
            "EELINUX_DHCPCD": str(mock), "EELINUX_NETWORK_CONFIG": str(self.config),
            "EELINUX_NETWORK_LIB": str(REPO / "rootfs/usr/lib/eelinux/network-common.sh"),
            "EELINUX_RUN_DIR": str(self.folder), "EELINUX_RESOLVER": str(self.resolver),
            "EELINUX_DHCP_HOOK": str(REPO / "rootfs/usr/lib/eelinux/dhcpcd-hook"),
            "EELINUX_DHCPCD_CONFIG": str(self.folder / "dhcpcd.conf"),
        })

    def tearDown(self):
        self.temporary.cleanup()

    def commands(self):
        return [json.loads(line) for line in self.log.read_text().splitlines()] if self.log.exists() else []

    def run_service(self, action="start"):
        return subprocess.run(["bash", str(REPO / "rootfs/usr/sbin/eelinux-network"), action],
                              env=self.environment, capture_output=True, text=True)

    def run_hook(self, event="bound", **lease):
        environment = self.environment | {"interface": "eth0", "reason": event.upper(), "new_domain_name_servers": "192.168.3.1 1.1.1.1"} | lease
        return subprocess.run(["bash", str(REPO / "rootfs/usr/lib/eelinux/dhcpcd-hook")],
                              env=environment, capture_output=True, text=True)

    def test_static_route_and_multiple_dns_servers(self):
        self.config.write_text("ENABLED=yes\nDEVICE=eth0\nMODE=static\nADDRESS=192.168.3.20/24\nGATEWAY=192.168.3.1\nDNS=192.168.3.1 1.1.1.1\n")
        result = self.run_service()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn(["-4", "address", "add", "192.168.3.20/24", "dev", "eth0"], self.commands())
        self.assertIn(["-4", "route", "replace", "default", "via", "192.168.3.1", "dev", "eth0"], self.commands())
        self.assertIn("nameserver 192.168.3.1\nnameserver 1.1.1.1\n", self.resolver.read_text())

    def test_dhcp_remains_running_for_renewal_and_retries(self):
        self.config.write_text("MODE=dhcp\n")
        result = self.run_service()
        self.assertEqual(result.returncode, 0, result.stderr)
        args = self.commands()[-1]
        self.assertIn("--background", args)
        self.assertIn("-4", args)
        self.assertNotIn("--oneshot", args)
        self.assertEqual(args[args.index("--script") + 1], self.environment["EELINUX_DHCP_HOOK"])
        self.assertEqual(args[args.index("--config") + 1], self.environment["EELINUX_DHCPCD_CONFIG"])
        self.assertTrue(any(arg.startswith("--hostname=") for arg in args))
        self.assertNotIn("--hostname", args)
        self.assertEqual(args[-1], "eth0")

    def test_renew_replaces_dns_without_duplicating_daemon_route_changes(self):
        self.assertEqual(self.run_hook().returncode, 0)
        result = self.run_hook("renew", new_domain_name_servers="9.9.9.9")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(self.commands(), [])
        self.assertEqual(self.resolver.read_text().splitlines()[1:], ["nameserver 9.9.9.9"])
        result = self.run_hook("renew", new_domain_name_servers="")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(self.resolver.read_text().splitlines()[1:], [])

    def test_invalid_lease_cannot_change_live_network(self):
        for lease in ({"new_domain_name_servers": "999.1.2.3"}, {"new_domain_name_servers": "$(touch /tmp/should-not-exist)"}, {"new_domain_name_servers": "1.1.1.1 garbage"}, {"interface": "-evil"}):
            result = self.run_hook(**lease)
            self.assertNotEqual(result.returncode, 0)
            self.assertEqual(self.commands(), [])
            self.assertFalse(self.resolver.exists())

    def test_dhcp_search_domain_renewal_and_invalid_input(self):
        result = self.run_hook(new_domain_search="example.test lab.example.test")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("search example.test lab.example.test\n", self.resolver.read_text())
        before = self.resolver.read_bytes()
        for domain in ("-bad.test", "bad..test", "$(touch /tmp/not-a-domain)", "a" * 64 + ".test"):
            result = self.run_hook("renew", new_domain_search=domain)
            self.assertNotEqual(result.returncode, 0)
            self.assertEqual(before, self.resolver.read_bytes())
        result = self.run_hook("renew", new_domain_name="fallback.test")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("search fallback.test\n", self.resolver.read_text())
        self.assertNotIn("example.test", self.resolver.read_text())

    def test_configuration_cannot_evaluate_shell_code(self):
        target = self.folder / "executed"
        self.config.write_text(f"MODE=static\nADDRESS=$(touch {target})\n")
        result = self.run_service()
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse(target.exists())
        self.assertEqual(self.commands(), [])

    def test_stop_does_not_kill_an_unrelated_pid(self):
        self.config.write_text("MODE=dhcp\n")
        (self.folder / "dhcpcd").mkdir()
        for arguments in (["sleep", "30"], ["bash", "-c", "exec -a dhcpcd sleep 30"]):
            with self.subTest(arguments=arguments):
                process = subprocess.Popen(arguments)
                try:
                    (self.folder / "dhcpcd/eth0-4.pid").write_text(str(process.pid) + "\n")
                    result = self.run_service("stop")
                    self.assertEqual(result.returncode, 0, result.stderr)
                    self.assertIsNone(process.poll())
                    self.assertFalse((self.folder / "dhcpcd/eth0-4.pid").exists())
                finally:
                    process.terminate()
                    process.wait()

    def test_expiry_clears_dns_without_duplicating_daemon_deconfiguration(self):
        self.resolver.write_text("nameserver 1.1.1.1\n")
        result = self.run_hook("expire")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(self.commands(), [])
        self.assertEqual(self.resolver.read_text().splitlines()[1:], [])


@unittest.skipUnless(sys.platform == "linux" and shutil.which("bash"), "requires Linux Bash and symlinks")
class SysVRunlevelBehavior(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.folder = Path(self.temporary.name)
        self.root = self.folder / "rc.d"
        self.services = self.folder / "init.d"
        self.services.mkdir()
        self.log = self.folder / "actions"
        for level in ("S", "0", "1", "2", "3", "4", "5", "6"):
            (self.root / f"rc{level}.d").mkdir(parents=True)
        self.environment = os.environ | {"EELINUX_RC_DIR": str(self.root), "ACTION_LOG": str(self.log), "PREVLEVEL": "N"}

    def tearDown(self):
        self.temporary.cleanup()

    def service(self, name, fail=False):
        path = self.services / name
        path.write_text(f'#!/bin/bash\nprintf "%s %s\\n" "{name}" "$1" >> "$ACTION_LOG"\nexit {1 if fail else 0}\n')
        path.chmod(0o755)
        return path

    def link(self, level, name, service):
        (self.root / f"rc{level}.d" / name).symlink_to(service)

    def run_level(self, level, previous="N"):
        return subprocess.run(["bash", str(REPO / "rootfs/etc/init.d/rc"), level],
                              env=self.environment | {"PREVLEVEL": previous}, capture_output=True, text=True)

    def actions(self):
        return self.log.read_text().splitlines() if self.log.exists() else []

    def test_stop_before_start_and_numeric_order(self):
        for name, service_name in (("K90late", "late"), ("K10early", "early"), ("S01first", "first"), ("S20second", "second")):
            self.link("3", name, self.service(service_name))
        result = self.run_level("3")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(self.actions(), ["early stop", "late stop", "first start", "second start"])

    def test_service_failure_is_reported_and_later_services_run(self):
        self.link("2", "S10bad", self.service("bad", fail=True))
        self.link("2", "S20good", self.service("good"))
        result = self.run_level("2")
        self.assertEqual(result.returncode, 1)
        self.assertIn("service start failed", result.stderr)
        self.assertEqual(self.actions(), ["bad start", "good start"])

    def test_equivalent_multiuser_transition_keeps_network_running(self):
        network = self.service("network")
        self.link("2", "S20network", network)
        self.link("3", "S20network", network)
        self.assertEqual(self.run_level("2").returncode, 0)
        self.assertEqual(self.run_level("3", previous="2").returncode, 0)
        self.assertEqual(self.actions(), ["network start"])
        self.link("3", "K20network", network)
        self.assertEqual(self.run_level("3", previous="2").returncode, 0)
        self.assertEqual(self.actions(), ["network start", "network stop", "network start"])

    def test_invalid_runlevel_cannot_execute_services(self):
        self.link("2", "S20network", self.service("network"))
        result = self.run_level("../../2")
        self.assertEqual(result.returncode, 2)
        self.assertEqual(self.actions(), [])


if __name__ == "__main__":
    unittest.main()
