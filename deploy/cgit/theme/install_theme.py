#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Apply the website's eeLinux branding to the existing account-local cgit."""

from __future__ import annotations

import argparse
from datetime import datetime, timezone
from hashlib import sha256
import json
import os
from pathlib import Path
import re
import secrets
import tempfile
from urllib.parse import urlsplit


DEFAULT_DOCUMENT_ROOT = "/home/falaneto/public_html/eelinux"
DEFAULT_PRIVATE_ROOT = "/home/falaneto/eelinux-cgit"
DEFAULT_REPOSITORY = "/home/falaneto/repos/eelinux.git"
DEFAULT_URL = "https://eelinux.falanet.org/git"
PUBLIC_FILES = ("eelinux-cgit.css", "eelinux-logo.svg", "favicon.svg")
PRIVATE_FILES = ("header.html", "footer.html", "head.html")
MANAGED_KEYS = frozenset(("logo", "favicon", "header", "footer", "head-include",
                          "root-title", "root-desc", "cache-root"))
KEY_LINE = re.compile(r"^\s*([A-Za-z0-9._-]+)\s*=")
DIGEST = re.compile(r"[0-9a-f]{64}")


def digest(data):
    return sha256(data).hexdigest()


def checked_path(value):
    """Reject ambiguous cgit values and symbolic links, including ancestors."""
    if not re.fullmatch(r"/[A-Za-z0-9_./+\-]+", str(value)):
        raise ValueError(f"Use an absolute Linux path without spaces or metacharacters: {value}")
    path = Path(value)
    if ".." in path.parts:
        raise ValueError(f"Parent traversal is not permitted: {path}")
    for entry in (path, *path.parents):
        if entry.is_symlink():
            raise ValueError(f"Refusing a symbolic-link path: {entry}")
    path = path.resolve()
    if path == Path("/"):
        raise ValueError("The filesystem root is not an installation directory")
    return path


def mutation_path(value, private, public, repository):
    path = checked_path(value)
    if not (path.is_relative_to(private) or path.is_relative_to(public)):
        raise ValueError(f"Destination is outside this cgit installation: {path}")
    if path == repository or path.is_relative_to(repository):
        raise ValueError(f"Refusing to write inside the Git repository: {path}")
    for parent in (path, *path.parents):
        if (parent / ".git").exists() or (parent / ".git").is_symlink():
            raise ValueError(f"Refusing to write inside a Git checkout: {path}")
    return path


def locations_for(document_root, private_root, repository, public_url):
    document = checked_path(document_root)
    private = checked_path(private_root)
    repo = checked_path(repository)
    url = urlsplit(public_url)
    if (url.scheme != "https" or url.path.rstrip("/") != "/git" or not url.hostname
            or url.username or url.password or url.query or url.fragment
            or not re.fullmatch(r"[A-Za-z0-9.\-]+(?::[0-9]+)?", url.netloc)):
        raise ValueError("Expected an HTTPS public URL ending in /git, without credentials or query")
    if not document.is_dir() or not private.is_dir() or not repo.is_dir():
        raise ValueError("This updater requires the existing document root, private cgit directory and repository")
    if private.is_relative_to(document) or document.is_relative_to(private):
        raise ValueError("Private cgit files and document root must be separate trees")
    if repo.is_relative_to(document) or private.is_relative_to(repo) or document.is_relative_to(repo):
        raise ValueError("The Git repository must be separate from every mutation destination")
    public = mutation_path(document / "git", private, document / "git", repo)
    if not public.is_dir():
        raise ValueError("The existing public /git directory is missing")
    return {"document_root": str(document), "private_root": str(private),
            "repository": str(repo), "public_url": f"https://{url.netloc}/git"}


def read_state(locations):
    private = Path(locations["private_root"])
    public = Path(locations["document_root"]) / "git"
    repository = Path(locations["repository"])
    state_path = mutation_path(private / "install-state.json", private, public, repository)
    if not state_path.is_file():
        raise ValueError("A known install-state.json is required; install or reconcile the existing cgit first")
    state_data = state_path.read_bytes()
    state = json.loads(state_data)
    if not isinstance(state, dict) or state.get("locations") != locations or not isinstance(state.get("files"), dict):
        raise ValueError("The existing cgit installation state belongs to different paths or URL")
    config_path = private / "cgitrc"
    if str(config_path) not in state["files"]:
        raise ValueError("The private cgitrc is not recorded in the existing installation state")
    for name, expected in state["files"].items():
        if not isinstance(name, str) or not isinstance(expected, str) or not DIGEST.fullmatch(expected):
            raise ValueError("The installation state has an invalid file entry")
        path = mutation_path(name, private, public, repository)
        if not path.is_file() or digest(path.read_bytes()) != expected:
            raise ValueError(f"A recorded cgit file changed; left untouched: {path}")
    return state_path, state_data, state


def read_theme(theme_dir):
    directory = checked_path(theme_dir)
    data = {}
    for name in (*PUBLIC_FILES, *PRIVATE_FILES):
        path = checked_path(directory / name)
        if not path.is_file():
            raise ValueError(f"Theme input is missing: {path}")
        data[name] = path.read_bytes()
    if data["head.html"].count(b"{{THEME_URL}}") != 1:
        raise ValueError("head.html must contain exactly one {{THEME_URL}} placeholder")
    for name, content in data.items():
        if name != "head.html" and b"{{THEME_URL}}" in content:
            raise ValueError(f"Unexpected theme URL placeholder in {name}")
    identity = sha256()
    for name in sorted(data):
        identity.update(name.encode("ascii") + b"\0" + len(data[name]).to_bytes(8, "big") + data[name])
    return data, identity.hexdigest()[:16]


def update_config(original, values):
    """Replace only the listed scalar keys, retaining every other line verbatim."""
    text = original.decode("utf-8")
    lines = text.splitlines(keepends=True)
    keys = [(KEY_LINE.match(line).group(1) if KEY_LINE.match(line) else None) for line in lines]
    if "repo.url" not in keys:
        raise ValueError("Expected the existing cgit repository configuration")
    if set(values) != MANAGED_KEYS:
        raise ValueError("Only the explicitly managed presentation keys may be changed")
    newline = "\r\n" if "\r\n" in text else "\n"
    missing = [key for key in values if key not in keys]
    result = []
    inserted = False
    for line, key in zip(lines, keys):
        if key == "repo.url" and not inserted:
            if result and not result[-1].endswith(("\n", "\r")):
                result.append(newline)
            result.extend(f"{name}={values[name]}{newline}" for name in missing)
            inserted = True
        if key in values:
            ending = "\r\n" if line.endswith("\r\n") else "\n" if line.endswith("\n") else ""
            result.append(f"{key}={values[key]}{ending}")
        else:
            result.append(line)
    return "".join(result).encode("utf-8")


def atomic_write(path, data, mode):
    if path.exists() and path.read_bytes() == data:
        return
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(dir=path.parent, prefix=".eelinux-theme-", delete=False) as out:
            temporary = Path(out.name)
            out.write(data)
            out.flush()
            os.fsync(out.fileno())
            temporary.chmod(mode)
        os.replace(temporary, path)
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)


def ensure_directory(path, mode, private, public, repository):
    path = mutation_path(path, private, public, repository)
    missing = []
    parent = path
    while not parent.exists():
        missing.append(parent)
        parent = parent.parent
    if not parent.is_dir():
        raise ValueError(f"A parent is not a directory: {parent}")
    for entry in reversed(missing):
        mutation_path(entry, private, public, repository)
        entry.mkdir(mode=mode)
        entry.chmod(mode)
    if not path.is_dir():
        raise ValueError(f"Expected a directory: {path}")
    # Dedicated private/public theme directories are ours, so repair their modes.
    path.chmod(mode)


def apply_theme(theme_dir, document_root=DEFAULT_DOCUMENT_ROOT, private_root=DEFAULT_PRIVATE_ROOT,
                repository=DEFAULT_REPOSITORY, public_url=DEFAULT_URL, dry_run=False):
    locations = locations_for(document_root, private_root, repository, public_url)
    private = Path(locations["private_root"])
    public = Path(locations["document_root"]) / "git"
    repo = Path(locations["repository"])
    state_path, state_data, state = read_state(locations)
    inputs, theme_id = read_theme(theme_dir)
    theme_url = f"/git/static/eelinux-theme-{theme_id}"
    public_theme = mutation_path(public / "static" / f"eelinux-theme-{theme_id}", private, public, repo)
    private_theme = mutation_path(private / "themes" / theme_id, private, public, repo)
    previous_theme = state.get("theme", {})
    if not isinstance(previous_theme, dict):
        raise ValueError("Invalid recorded theme state")
    if previous_theme.get("id") == theme_id:
        cache = mutation_path(previous_theme.get("cache_root", ""), private, public, repo)
        if cache.parent != private / "theme-cache" or not cache.name.startswith(theme_id + "-"):
            raise ValueError("Invalid recorded theme cache path")
    else:
        stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
        cache = mutation_path(private / "theme-cache" / f"{theme_id}-{stamp}-{secrets.token_hex(4)}", private, public, repo)
        if cache.exists():
            raise ValueError("The new theme cache directory already exists; rerun to select a fresh one")
    plan = {}
    for name in PUBLIC_FILES:
        plan[public_theme / name] = (inputs[name], 0o644)
    for name in PRIVATE_FILES:
        content = inputs[name].replace(b"{{THEME_URL}}", theme_url.encode("ascii"))
        plan[private_theme / name] = (content, 0o600)
    values = {
        "logo": f"{theme_url}/eelinux-logo.svg",
        "favicon": f"{theme_url}/favicon.svg",
        "header": str(private_theme / "header.html"),
        "footer": str(private_theme / "footer.html"),
        "head-include": str(private_theme / "head.html"),
        "root-title": "eeLinux source",
        "root-desc": "GNU/Linux for the PlayStation 2",
        "cache-root": str(cache),
    }
    config_path = private / "cgitrc"
    original_config = config_path.read_bytes()
    config_data = update_config(original_config, values)
    plan[config_path] = (config_data, 0o600)
    for path, (content, _mode) in plan.items():
        mutation_path(path, private, public, repo)
        if path.exists() and (not path.is_file() or (path != config_path and path.read_bytes() != content)):
            raise ValueError(f"An immutable theme destination differs; left untouched: {path}")
    changed = any(not path.exists() or path.read_bytes() != content for path, (content, _) in plan.items())
    info = {"theme_id": theme_id, "public_url": locations["public_url"] + "/",
            "theme_url": theme_url, "cache_root": str(cache), "changed": changed,
            "files": [str(path) for path in plan]}
    if dry_run:
        return info
    # Recheck immediately before the first write; an interrupted or concurrent
    # cgit update must never be silently accepted as the previous baseline.
    _state_path, latest_state, _ = read_state(locations)
    if latest_state != state_data or config_path.read_bytes() != original_config:
        raise ValueError("The installation changed during validation; run the updater again")
    if not changed:
        ensure_directory(cache, 0o700, private, public, repo)
        return info
    stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    backup = mutation_path(private / "theme-backups" / f"{stamp}-{theme_id}-{secrets.token_hex(4)}", private, public, repo)
    if backup.exists():
        raise ValueError("The new backup directory already exists; rerun to select a fresh one")
    ensure_directory(backup, 0o700, private, public, repo)
    backup_records = []
    # Back up every pre-existing destination and the state before changing any
    # live file. Versioned assets usually have no predecessor to overwrite.
    for number, path in enumerate([state_path, *plan]):
        if path.exists():
            content = path.read_bytes()
            name = f"{number:02d}-{path.name}"
            atomic_write(backup / name, content, 0o600)
            backup_records.append({"path": str(path), "backup": name, "sha256": digest(content),
                                   "mode": path.stat().st_mode & 0o777})
    atomic_write(backup / "backup.json", (json.dumps({"files": backup_records}, indent=2) + "\n").encode(), 0o600)
    ensure_directory(public_theme, 0o755, private, public, repo)
    ensure_directory(private_theme, 0o700, private, public, repo)
    ensure_directory(cache, 0o700, private, public, repo)
    for path, (content, mode) in plan.items():
        if path != config_path:
            atomic_write(path, content, mode)
    # One atomic config replacement activates the complete theme. Existing CGI,
    # binary, repository, clone endpoint and original cache are left in place.
    atomic_write(config_path, config_data, 0o600)
    state["files"].update({str(path): digest(content) for path, (content, _) in plan.items()})
    state["theme"] = {"id": theme_id, "url": theme_url, "cache_root": str(cache),
                      "backup": str(backup), "installer": "deploy/cgit/theme/install_theme.py"}
    atomic_write(state_path, (json.dumps(state, indent=2) + "\n").encode(), 0o600)
    info["backup"] = str(backup)
    return info


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--theme-dir", default=str(Path(__file__).parent))
    parser.add_argument("--document-root", default=DEFAULT_DOCUMENT_ROOT)
    parser.add_argument("--public-root", help="Existing public cgit directory, ending in /git")
    parser.add_argument("--private-root", default=DEFAULT_PRIVATE_ROOT)
    parser.add_argument("--repository", default=DEFAULT_REPOSITORY)
    parser.add_argument("--public-url", default=DEFAULT_URL)
    parser.add_argument("--dry-run", "--check-only", dest="dry_run", action="store_true",
                        help="Validate and print the plan without writing")
    args = parser.parse_args(argv)
    if os.name != "posix":
        parser.error("Run this updater on the Linux hosting server")
    try:
        if args.public_root:
            public = checked_path(args.public_root)
            if public.name != "git":
                raise ValueError("The public cgit directory must end in /git")
            args.document_root = str(public.parent)
        info = apply_theme(args.theme_dir, args.document_root, args.private_root,
                           args.repository, args.public_url, args.dry_run)
        if args.dry_run:
            print(json.dumps(info, indent=2))
        else:
            print(("Installed" if info["changed"] else "Already installed") + ": " + info["public_url"])
            print("Theme:", info["theme_id"])
            if info.get("backup"):
                print("Private backup:", info["backup"])
            print("Verify repository browsing and HTTP clone after installation.")
    except (OSError, ValueError, KeyError, TypeError) as error:
        parser.exit(1, f"cgit theme stopped: {error}\n")


if __name__ == "__main__":
    main()
