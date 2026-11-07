"""Apply the preserved legacy SMAP cold-start correction to its exact baseline."""
from pathlib import Path
import hashlib
import json
import subprocess
import tempfile
from kernel_patch_names import generic, normalize_source

ROOT = Path(__file__).resolve().parents[1]
DRIVER = "drivers/net/ethernet/ps2/ps2-smap.c"
BASELINE_SHA256 = "356f1a53e82aa919c5186ae0597b090e62aa2d9626a29575373069fa390d33f4"


def install(source: Path, patch: Path) -> dict:
    source = Path(source)
    original = (source / DRIVER).read_bytes()
    if hashlib.sha256(original).hexdigest() != BASELINE_SHA256:
        raise RuntimeError("Review native SMAP startup baseline; refusing unrelated or repeated update")
    # The current driver also needs IOP source files and a new SIF API.
    # This legacy installer must not copy only its entry-point .c file.
    reviewed = ROOT / "kernel/patches/ps2-smap-startup.patch"
    blob = reviewed.read_bytes()
    banner = source / "init/main.c"
    before = generic(banner.read_text())
    marker = "ps2: page-cache writeback kernel"
    if before.count(marker) != 1:
        raise RuntimeError("Review preserved kernel banner")
    with tempfile.TemporaryDirectory(prefix="ps2-smap-startup-") as temporary:
        tree = Path(temporary)
        for name, text in ((DRIVER, generic(original.decode())), ("init/main.c", before)):
            p = tree / name; p.parent.mkdir(parents=True, exist_ok=True); p.write_text(text)
        subprocess.run(["git", "apply", "--check", str(reviewed.resolve())], cwd=tree, check=True)
        subprocess.run(["git", "apply", str(reviewed.resolve())], cwd=tree, check=True)
        updated = (tree / DRIVER).read_bytes()
    normalize_source(source)
    patch.write_bytes(blob)
    subprocess.run(["git", "apply", "--check", str(patch.resolve())], cwd=source, check=True)
    subprocess.run(["git", "apply", str(patch.resolve())], cwd=source, check=True)
    assert (source / DRIVER).read_bytes() == updated
    report = {"before_sha256": BASELINE_SHA256, "driver_sha256": hashlib.sha256(updated).hexdigest(),
              "patch_sha256": hashlib.sha256(patch.read_bytes()).hexdigest(),
              "cold_MDIO_initial_DONE_not_required": True, "timeout_transaction_ownership_preserved": True,
              "FIFO_reset_before_EMAC": True, "probe_phase_diagnostics": True,
              "physical_network_result": "pending"}
    (source / ".eelinux-smap-startup.json").write_text(json.dumps(report, indent=2) + "\n")
    return report
