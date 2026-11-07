"""Exercise the real DEV9 and SPD replacement C with bounded host MMIO mocks."""
from __future__ import annotations

import os
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import struct
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
import iop_dev9

COMMON = r'''
#ifndef MOCK_IOP
#define MOCK_IOP
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <setjmp.h>
typedef uint8_t u8; typedef uint16_t u16; typedef uint32_t u32; typedef int32_t s32;
#define BIT(n) (1u << (n))
#define DEV9_REGBASE 0xbf801460u
#define DEV9_REG(n) (DEV9_REGBASE + 2u*(n))
enum { DEV9_REG_1460,DEV9_REG_1462,DEV9_REG_1464,DEV9_REG_1466,DEV9_REG_1468,
DEV9_REG_146A,DEV9_REG_POWER,DEV9_REG_REV,DEV9_REG_1470,DEV9_REG_1472,
DEV9_REG_1474,DEV9_REG_1476,DEV9_REG_1478,DEV9_REG_147A,DEV9_REG_147C,DEV9_REG_147E };
#define SSBUS_REG_1418 0xbf801418u
#define SSBUS_REG_141c 0xbf80141cu
#define SSBUS_REG_1420 0xbf801420u
#define SPD_REGBASE 0xb0000000u
#define SPD_REG(n) (SPD_REGBASE+(n))
#define SPD_REG_REV_1 2
#define SPD_REG_REV_3 4
#define SPD_REG_INTR_STAT 0x28
#define SPD_REG_INTR_MASK 0x2a
#define IRQ_IOP_DEV9 13
#define IRQ_IOP_SPD_BASE 46
#define IRQ_IOP_SPD_ATA0 46
#define IRQ_IOP_SPD_ATA1 47
#define IRQ_IOP_SPD_TXDNV 48
#define IRQ_IOP_SPD_RXDNV 49
#define IRQ_IOP_SPD_TXEND 50
#define IRQ_IOP_SPD_RXEND 51
#define IRQ_IOP_SPD_EMAC3 52
#define IRQ_IOP_SPD_DVR 55
#define IRQ_IOP_SPD_UART 58
#define IRQ_IOP_SPD_LAST 58
enum irq_status { IRQ_HANDLED };
typedef enum irq_status (*irq_handler_t)(void*);
enum module_init_status { MODULE_EXIT, MODULE_RESIDENT };
#define module_init(fn) static void *mock_entry __attribute__((used)) = (void*)fn
#define THREAD_ATTR_C 0x02000000
struct iop_thread { u32 attr, option; void(*thread)(void*); u32 stacksize,priority; };
struct iop_sema { u32 attr,option,initial,max; };
struct sif_cmd_header { u32 packet_size:8, data_size:24; u32 data_addr,cmd,opt; };
#define sif_cmd_payload(h) ((void*)((h)+1))
#define SIF_DMA_STATUS_COMPLETED -1
static u16 dev_regs[16], spd_regs[0x2000];
static u8 cis[4096];
static u16 ee_words[4] = {0x1102,0x3322,0x5544,0x9968};
static unsigned int writes, irq_depth, ee_clocks, callbacks, delays, waits, signals;
static unsigned int stat_writes, smap_ack, latch_writes, failed_release, wake_waits;
static unsigned int created_semas, deleted_semas, create_fail, thread_fail;
static unsigned int dummy_reads;
static bool bad_dummy;
static bool attribute_mode, last_clock, worker_test;
static int dma_calls;
static void *dma_packet;
static unsigned char dma_snapshot[64];
static size_t dma_size;
static jmp_buf worker_return;
static u8 iord8(u32 addr) {
 if(addr>=DEV9_REGBASE && addr<DEV9_REGBASE+32) return dev_regs[(addr-DEV9_REGBASE)/2]&255;
 if(attribute_mode && addr>=SPD_REGBASE && addr<SPD_REGBASE+4096) return cis[addr-SPD_REGBASE];
 assert(addr>=SPD_REGBASE && addr<SPD_REGBASE+0x4000);
 if(addr==SPD_REG(0x2e) && ee_clocks==9) {
  dummy_reads++;
  return (spd_regs[0x2e/2]&~0x10) | (bad_dummy ? 0x10 : 0);
 }
 if(addr==SPD_REG(0x2e) && ee_clocks>=10 && ee_clocks<=73) {
  unsigned int bit=ee_clocks-10;
  return (spd_regs[0x2e/2]&~0x10) | (((ee_words[bit/16]>>(15-bit%16))&1)<<4);
 }
 return spd_regs[(addr-SPD_REGBASE)/2]&255;
}
static u16 iord16(u32 addr) {
 if(addr>=DEV9_REGBASE && addr<DEV9_REGBASE+32) return dev_regs[(addr-DEV9_REGBASE)/2];
 assert(addr>=SPD_REGBASE && addr<SPD_REGBASE+0x4000); return spd_regs[(addr-SPD_REGBASE)/2];
}
static void iowr8(u8 value,u32 addr) {
 assert(addr>=SPD_REGBASE && addr<SPD_REGBASE+0x4000); writes++;
 if(addr==SPD_REG(0x2e)) {
  if((value&0x40) && !last_clock) ee_clocks++;
  last_clock=!!(value&0x40);
 }
 spd_regs[(addr-SPD_REGBASE)/2]=(spd_regs[(addr-SPD_REGBASE)/2]&0xff00)|value;
}
static void iowr16(u16 value,u32 addr) {
 writes++;
 if(addr>=DEV9_REGBASE && addr<DEV9_REGBASE+32) {
  dev_regs[(addr-DEV9_REGBASE)/2]=value;
  if(addr==DEV9_REG(DEV9_REG_1466) || addr==DEV9_REG(DEV9_REG_147E)) latch_writes++;
  return;
 }
 assert(addr>=SPD_REGBASE && addr<SPD_REGBASE+0x4000);
 if(addr==SPD_REG(0x28)) stat_writes++;
 if(addr==SPD_REG(0x128)) { smap_ack|=value; spd_regs[0x28/2]&=~value; }
 else spd_regs[(addr-SPD_REGBASE)/2]=value;
}
static void iowr32(u32 value,u32 addr) {
 assert(addr==SSBUS_REG_1418 || addr==SSBUS_REG_141c || addr==SSBUS_REG_1420); writes++;
 if(addr==SSBUS_REG_1418) attribute_mode=value==0x1a00bb;
}
static int thbase_delay(int us) { assert(us>0 && us<=500000); delays++; return 0; }
static int thsemap_wait_sema(int id) {
 assert(!irq_depth); waits++;
 if(worker_test && id==2 && ++wake_waits>1) longjmp(worker_return,1);
 return 0;
}
static int thsemap_signal_sema(int id) { signals++; return 0; }
static int thsemap_isignal_sema(int id) { return 0; }
static int thsemap_create_sema(const struct iop_sema *s) {
 assert(s->max==1); created_semas++; return create_fail==created_semas ? -EIO : (int)created_semas;
}
static int thsemap_delete_sema(int id) { deleted_semas++; return 0; }
static int thbase_create(const struct iop_thread *t) { return thread_fail ? -EIO : 3; }
static int thbase_start(int id,void *arg) { return 0; }
static int thbase_delete(int id) { return 0; }
static int errno_for_iop_error(int err) { return err; }
static void sif_request_cmd(int cid,void(*cb)(const struct sif_cmd_header*,void*),void *arg) { assert((cid&0x7fffffff)<32); }
static unsigned int sifcmd_send_cmd(u32 cmd,void *packet,size_t size,const void *src,u32 dst,size_t count) {
 assert(cmd==0x8000001c && size==36); assert(!dma_calls);
 dma_calls++; dma_packet=packet; dma_size=size; memcpy(dma_snapshot,packet,size); return 1;
}
static int sifman_dma_stat(int id) { return 0; }
#define irq_save(flags) do { flags=0; irq_depth++; } while(0)
#define irq_restore(flags) do { (void)flags; assert(irq_depth); irq_depth--; } while(0)
#define pr_info(...) ((void)0)
#define pr_warn(...) ((void)0)
#define pr_err(...) ((void)0)
static bool spd_valid_irq(unsigned int irq) { return irq>=46 && irq<=58; }
static unsigned int fls(unsigned int value) { return value ? 32-__builtin_clz(value) : 0; }
static int request_irq(unsigned int irq,irq_handler_t cb,void* arg) { assert(irq==13); return 0; }
static int release_irq(unsigned int irq) { assert(irq==13); return failed_release ? -EIO : 0; }
static enum irq_status count_irq(void*arg) { callbacks++; return IRQ_HANDLED; }
#endif
'''

DEV9_TESTS = r'''
static void setup(void) {
 dev_regs[DEV9_REG_REV]=0x30; spd_regs[2/2]=17; spd_regs[4/2]=1;
 spd_regs[0x2c/2]=3; spd_regs[0x2e/2]=3;
 lock_id=1;wake_id=2;
}
static void submit(unsigned int op,unsigned int cookie) {
 struct {struct sif_cmd_header header;struct dev9_request_packet request;} p={0};
 p.header.opt=REQUEST_TAG;p.request.operation=op;p.request.cookie=cookie;
 request_callback(&p.header,NULL);
}
int main(int argc,char **argv) {
 assert(argc==2); setup();
 if(!strcmp(argv[1],"power")) {
  assert(dev9_request()==0 && dev9_use_count==1 && hardware==0x30);
  assert(eeprom_valid && mac_checksum==0x9968 && !memcmp(mac_address,"\x02\x11\x22\x33\x44\x55",6));
  assert(dummy_reads==1 && ee_clocks==73);
  assert(spd_regs[0x2c/2]==3 && spd_regs[0x2e/2]==3);
  unsigned int before=writes; assert(dev9_request()==0 && writes==before && dev9_use_count==2);
  assert(dev9_release()==0 && dev9_release()==0 && dev9_release()==-EINVAL);
  assert(dev9_use_count==0 && waits==signals && !irq_depth);
 } else if(!strcmp(argv[1],"dummy")) {
  bad_dummy=true;
  struct dev9_request_packet r={1,42};
  assert(process_request(&r)==-EIO && !lease_cookie && !dev9_use_count);
  assert(dummy_reads==1 && ee_clocks==9 && !eeprom_valid);
  assert(spd_regs[0x2c/2]==3 && spd_regs[0x2e/2]==3 && !irq_depth);
 } else if(!strcmp(argv[1],"failure")) {
  dev_regs[DEV9_REG_REV]=0xff; assert(dev9_request()==-ENODEV && !dev9_use_count && waits==signals);
  dev_regs[DEV9_REG_REV]=0x30;dev_regs[DEV9_REG_1462]=1;
  assert(dev9_request()==-ENODEV && !dev9_use_count && waits==signals);
  dev_regs[DEV9_REG_1462]=0;ee_words[3]^=1;
  struct dev9_request_packet r={1,42};assert(process_request(&r)==-EIO && !lease_cookie && !dev9_use_count);
  assert(initialized && !eeprom_valid && waits==signals);
 } else if(!strcmp(argv[1],"pcmcia")) {
  dev_regs[DEV9_REG_REV]=0x20; dev_regs[DEV9_REG_1462]=0x100;
  cis[0]=0x20;cis[2]=4;cis[4]=0xf1;cis[6]=0;cis[8]=0;cis[10]=0x53;
  assert(dev9_request()==0 && hardware==0x20 && dev_regs[DEV9_REG_1474]==7);
  assert(!attribute_mode && eeprom_valid && dev9_use_count==1);
  initialized=false; dev9_use_count=0;dev_regs[DEV9_REG_POWER]=0;dev_regs[DEV9_REG_1474]=0;
  cis[4]=0xaa;assert(dev9_request()==-ENODEV && !dev9_use_count && waits==signals);
 } else if(!strcmp(argv[1],"lease")) {
  struct dev9_request_packet a={1,42},b={1,43},r={2,42};
  assert(process_request(&a)==0 && lease_cookie==42 && dev9_use_count==1);
  assert(process_request(&a)==0 && dev9_use_count==1);
  assert(process_request(&b)==-EBUSY && dev9_use_count==1);
  b.operation=2;assert(process_request(&b)==0 && dev9_use_count==1);
  assert(process_request(&r)==0 && !lease_cookie && !dev9_use_count);
  assert(process_request(&r)==0 && !dev9_use_count);
 } else if(!strcmp(argv[1],"timeout")) {
  submit(1,42);submit(2,42);worker_test=true;
  if(!setjmp(worker_return)) dev9_worker(NULL);
  assert(!lease_cookie && !dev9_use_count && dma_calls==1);
  assert(!memcmp(dma_snapshot,dma_packet,dma_size));
  assert(((struct sif_cmd_header*)dma_packet)->opt==0x44560114);
 } else if(!strcmp(argv[1],"overflow")) {
  submit(1,42);for(unsigned int i=1;i<QUEUE_SIZE;i++) submit(1,100+i);
  submit(2,42);assert(queue_count==QUEUE_SIZE && cleanup_cookie==42 && dropped_requests==1);
  worker_test=true;if(!setjmp(worker_return)) dev9_worker(NULL);
  assert(!lease_cookie && !dev9_use_count && !cleanup_cookie);
 } else if(!strcmp(argv[1],"init")) {
  assert(dev9_init(0,NULL)==MODULE_RESIDENT && !writes && !initialized);
  created_semas=deleted_semas=0;create_fail=2;
  assert(dev9_init(0,NULL)==MODULE_EXIT && deleted_semas==1 && !writes);
  created_semas=deleted_semas=0;create_fail=0;thread_fail=1;
  assert(dev9_init(0,NULL)==MODULE_EXIT && deleted_semas==2 && !writes);
 } else assert(0);
 return 0;
}
'''

SPD_TESTS = r'''
static void *find_map(unsigned int irq) { return irq==48 || irq==50 ? (void*)1 : NULL; }
static void enable_irq(unsigned int irq) { spd_enable_irq__(irq); }
REARM_CALLBACK
int main(void) {
 for(unsigned int irq=48;irq<=52;irq++) assert(spd_request_irq__(irq,count_irq,NULL)==0);
 assert(spd_regs[0x2a/2]==0x7c && stat_writes==0);
 spd_regs[0x28/2]=0x8030;spd_regs[0x2a/2]|=0x8000;dev_regs[DEV9_REG_REV]=0x30;
 assert(spd_handle_irq(NULL)==IRQ_HANDLED && callbacks==2 && spd_regs[0x2a/2]==0);
 assert(smap_ack==0x7c && stat_writes==0 && latch_writes==2);
 struct {struct sif_cmd_header header;u32 mask;} p={0};
 p.header.opt=0x534d0104;p.mask=0x7c;smap_rearm(&p.header,NULL);
 assert(spd_regs[0x2a/2]==0x14);
 p.mask=0x80;smap_rearm(&p.header,NULL);assert(spd_regs[0x2a/2]==0x14);
 for(unsigned int irq=48;irq<52;irq++) assert(spd_release_irq__(irq)==0);
 failed_release=1;assert(spd_release_irq__(52)==-EIO && spd_irqs[6].cb && spd_irq_count==1);
 failed_release=0;assert(spd_release_irq__(52)==0 && !spd_irq_count);
 spd_enable_irq__(52);assert(!spd_regs[0x2a/2]);
 assert(!irq_depth && stat_writes==0);
 return 0;
}
'''


@unittest.skipIf(os.name == "nt" or not shutil.which("cc"), "native host C compiler required")
class Dev9ActualC(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary = tempfile.TemporaryDirectory()
        cls.work = Path(cls.temporary.name)
        include = cls.work / "iopmod"
        include.mkdir()
        (include / "mock.h").write_text(COMMON)
        for name in ("bits", "build-bug", "dev9", "errno", "io", "iop-error", "irq", "irqs",
                     "interrupt", "module", "printk", "sifcmd", "sifman", "spd", "spd-irq", "processor", "string", "thread", "types"):
            (include / (name + ".h")).write_text('#include "mock.h"\n')
        cls.dev9 = cls.work / "dev9"
        cls.spd = cls.work / "spd"
        actual_dev9 = (iop_dev9.OVERLAY / "module/dev9.c").read_text().replace('#include "smap-service.h"', 'static int smap_server_start(void) { return 0; } static int thbase_terminate(int id) { (void)id; return 0; }')
        actual_spd = (iop_dev9.OVERLAY / "builtin/spd-irq.c").read_text()
        relay = iop_dev9.irqrelay_source((iop_dev9.VENDOR / "snapshot/module/irqrelay.c").read_text())
        rearm = relay[relay.index("#define SMAP_REARM_CMD"):relay.index("static enum module_init_status irqrelay_init")]
        for source, executable, text in (("dev9.c", cls.dev9, actual_dev9 + DEV9_TESTS),
                                         ("spd.c", cls.spd, actual_spd + SPD_TESTS.replace("REARM_CALLBACK", rearm))):
            path = cls.work / source
            path.write_text(text)
            subprocess.run(["cc", "-std=gnu11", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
                            "-Wno-unused-function", "-Wno-unused-variable", "-Wno-unused-parameter",
                            "-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-I", str(cls.work),
                            str(path), "-o", str(executable)], check=True, capture_output=True, text=True)

    @classmethod
    def tearDownClass(cls):
        cls.temporary.cleanup()

    def run_case(self, case):
        subprocess.run([str(self.dev9), case], check=True, timeout=10, capture_output=True, text=True)

    def test_expansion_power_mac_and_balanced_users(self): self.run_case("power")
    def test_invalid_eeprom_dummy_restores_pio_without_a_lease(self): self.run_case("dummy")
    def test_absent_hardware_and_bad_eeprom_do_not_acquire(self): self.run_case("failure")
    def test_pcmcia_voltage_ready_cis_and_bus_transition(self): self.run_case("pcmcia")
    def test_idempotent_cookie_lease_and_matching_release(self): self.run_case("lease")
    def test_reply_timeout_preserves_inflight_packet_and_releases_lease(self): self.run_case("timeout")
    def test_queue_overflow_still_processes_timeout_cleanup(self): self.run_case("overflow")
    def test_module_init_defers_hardware_and_cleans_failed_resources(self): self.run_case("init")
    def test_irq_w1c_bounds_coalescing_live_rearm_and_release_failure(self):
        subprocess.run([str(self.spd)], check=True, timeout=10, capture_output=True, text=True)


class Dev9Source(unittest.TestCase):
    def make_firmware(self, folder):
        report = iop_dev9.prepare_source(folder / "source")
        directory = folder / "firmware"
        directory.mkdir()
        header = struct.pack("<16sHHIIIIIHHHHHH", b"\x7fELF\x01\x01\x01" + b"\0"*9,
                             0xff80, 8, 1, 0, 52, 224, 1, 52, 32, 2, 40, 3, 0)
        programs = struct.pack("<8I", 0x70000080, 192, 0, 0, 31, 0, 4, 4)
        programs += struct.pack("<8I", 1, 128, 0, 0, 64, 64, 5, 16)
        load = (b"\0"*4 + struct.pack("<III8s", 0x41c00000, 0, 0x0100, b"dev9")).ljust(64,b"\0")
        info = b"\0"*12 + struct.pack("<3IH", 64, 0, 0, 0x0102) + b"dev9\0"
        sections = b"\0"*40 + struct.pack("<10I",0,1,6,0,128,64,0,0,4,0)
        sections += struct.pack("<10I",0,0x70000080,0,0,192,31,0,0,4,0)
        data = ((header+programs).ljust(128,b"\0") + load + info).ljust(224,b"\0") + sections
        report["firmware_sha256"] = {}
        report["firmware_bytes"] = {}
        for name in iop_dev9.MODULES:
            filename = name+".irx"
            (directory / filename).write_bytes(data)
            report["firmware_sha256"][filename] = hashlib.sha256(data).hexdigest()
            report["firmware_bytes"][filename] = len(data)
        (directory / "manifest.json").write_text(json.dumps(report))
        return directory, report

    def test_pristine_import_verified_and_staging_is_isolated(self):
        before = iop_dev9.digest(iop_dev9.VENDOR / "snapshot/module/dev9.c")
        with tempfile.TemporaryDirectory() as temporary:
            stage = Path(temporary) / "source"
            report = iop_dev9.prepare_source(stage)
            self.assertEqual(report["source"]["revision"], iop_dev9.REVISION)
            self.assertIn("MODULE_ID(dev9, 0x0102);", (stage / "include/iopmod/module/dev9.h").read_text())
            self.assertIn("LIBRARY_ID(dev9, 0x0100);", (stage / "include/iopmod/module/dev9.h").read_text())
            with self.assertRaisesRegex(RuntimeError, "existing"):
                iop_dev9.prepare_source(stage)
        self.assertEqual(before, iop_dev9.digest(iop_dev9.VENDOR / "snapshot/module/dev9.c"))

    def test_unexpected_irq_relay_source_fails_closed(self):
        source = (iop_dev9.VENDOR / "snapshot/module/irqrelay.c").read_text()
        with self.assertRaisesRegex(RuntimeError, "Unexpected"):
            iop_dev9.irqrelay_source(source.replace("irqrelay_init(", "changed_init("))

    def test_load_segment_alignment_and_bss_metadata_are_required(self):
        with tempfile.TemporaryDirectory() as temporary:
            folder, _ = self.make_firmware(Path(temporary))
            path = folder / "irqrelay.irx"
            original = path.read_bytes()
            # A gap before .data belongs in the text boundary, not BSS.
            data = bytearray(original)
            struct.pack_into("<3I", data, 192+12, 48, 16, 0)
            path.write_bytes(data)
            iop_dev9.validate_irx_layout(path)
            for offset, value in ((84+16, 62), (84+20, 66), (192+12, 32)):
                data = bytearray(original)
                struct.pack_into("<I", data, offset, value)
                path.write_bytes(data)
                with self.assertRaisesRegex(RuntimeError, "unsafe"):
                    iop_dev9.validate_irx_layout(path)

    def test_linker_sizes_include_rodata_and_alignment_gaps(self):
        original = (iop_dev9.VENDOR / "snapshot/script/iop.ld").read_text()
        adapted = iop_dev9.linker_source(original)
        self.assertNotIn(".rodata :", adapted)
        self.assertIn("LONG (ADDR(.data));", adapted)
        self.assertIn("LONG (ADDR(.bss) - ADDR(.data));", adapted)
        self.assertNotIn("LONG (SIZEOF(.text));", adapted)
        self.assertEqual(adapted.count(". = ALIGN(16);"), 4)
        with self.assertRaisesRegex(RuntimeError, "boundaries"):
            iop_dev9.linker_source(adapted)

    def test_firmware_recipe_and_actual_module_version_checked(self):
        with tempfile.TemporaryDirectory() as temporary:
            folder, report = self.make_firmware(Path(temporary))
            iop_dev9.validate_overlay_firmware(folder)
            data = bytearray((folder / "dev9.irx").read_bytes())
            struct.pack_into("<H", data, 192+24, 0x0100)
            (folder / "dev9.irx").write_bytes(data)
            report["firmware_sha256"]["dev9.irx"] = hashlib.sha256(data).hexdigest()
            (folder / "manifest.json").write_text(json.dumps(report))
            with self.assertRaisesRegex(RuntimeError, "actual module"):
                iop_dev9.validate_overlay_firmware(folder)

    def test_recipe_tampering_and_preserved_source_outputs_rejected(self):
        with tempfile.TemporaryDirectory() as temporary:
            folder, report = self.make_firmware(Path(temporary))
            report["recipe_sha256"]["scripts/iop_dev9.py"] = "0"*64
            (folder / "manifest.json").write_text(json.dumps(report))
            with self.assertRaisesRegex(RuntimeError, "provenance"):
                iop_dev9.validate_overlay_firmware(folder)
        with self.assertRaisesRegex(RuntimeError, "preserved source"):
            iop_dev9.prepare_source(iop_dev9.VENDOR / "snapshot/new-build")


if __name__ == "__main__":
    unittest.main()
