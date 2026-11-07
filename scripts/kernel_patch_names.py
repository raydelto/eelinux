"""Keep exported PS2 kernel patch identifiers independent of image build labels."""
from pathlib import Path
import re

def generic(text: str) -> str:
    text = text.replace("EELINUX_", "PS2_").replace("eelinux_", "ps2_")
    text = text.replace("eelinux-progress=", "ps2-progress=")
    text = text.replace("eeLinux", "ps2")
    # Remove labels without consuming a source or unified-diff newline.
    return re.sub(r"relay-v[0-9]+[ \t]*", "", text)

def normalize_source(source: Path) -> list[str]:
    """Normalize only reviewed source files named by the project's patches."""
    root = Path(__file__).resolve().parents[1]
    names = set()
    for patch in (root / "kernel/patches").glob("*.patch"):
        for line in patch.read_text().splitlines():
            if line.startswith("+++ b/"):
                names.add(line[6:])
    changed=[]
    for name in sorted(names):
        path=source/name
        if not path.is_file(): continue
        before=path.read_text(); after=generic(before)
        if before!=after:
            path.write_text(after); changed.append(name)
    return changed
