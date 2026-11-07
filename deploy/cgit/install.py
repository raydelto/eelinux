#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Install an account-local cgit at https://eelinux.falanet.org/git/."""

from __future__ import annotations

import argparse
from dataclasses import dataclass
from hashlib import sha256
import json
import os
from pathlib import Path
import re
import shlex
import subprocess
import tempfile
from urllib.parse import urlsplit


LOCK = Path(__file__).with_name("source-lock.json")
DEFAULT_URL = "https://eelinux.falanet.org/git"
ASSETS = ("cgit.css", "cgit.js", "cgit.png", "favicon.ico")


def git(*args, cwd=None):
    return subprocess.check_output(["git", *map(str, args)], cwd=cwd, text=True).strip()


def account_path(value):
    """Keep shell wrappers, cgit values and Apache paths unambiguous."""
    if not re.fullmatch(r"/[A-Za-z0-9_./+\-]+", str(value)):
        raise ValueError(f"Use an absolute Linux path without spaces or metacharacters: {value}")
    path = Path(value)
    for parent in (path, *path.parents):
        if parent.is_symlink():
            raise ValueError(f"Refusing a path through a symbolic link: {parent}")
    result = path.resolve()
    if result == Path("/"):
        raise ValueError("The filesystem root is not an installation directory")
    return result


def url_settings(value):
    url = urlsplit(value)
    if url.scheme != "https" or not url.hostname or url.username or url.password or url.query or url.fragment:
        raise ValueError("The public URL must be HTTPS without credentials, query or fragment")
    prefix = url.path.rstrip("/")
    if prefix != "/git":
        raise ValueError("This deployment is deliberately scoped to the /git directory")
    if not re.fullmatch(r"[A-Za-z0-9.\-]+(?::[0-9]+)?", url.netloc):
        raise ValueError("Invalid public hostname")
    return f"https://{url.netloc}{prefix}", prefix


@dataclass
class Payload:
    data: bytes
    mode: int


def load_build(source):
    source = account_path(source)
    lock = json.loads(LOCK.read_text(encoding="utf-8"))
    report = json.loads((source / "eelinux-build.json").read_text(encoding="utf-8"))
    if report["cgit_commit"] != lock["commit"] or report["git_commit"] != lock["git_submodule_commit"]:
        raise ValueError("Build does not match deploy/cgit/source-lock.json")
    if not (source / ".git").exists():
        bundle = json.loads((source / "runtime-bundle.json").read_text(encoding="utf-8"))
        if bundle.get("schema") != 1 or bundle["cgit_commit"] != lock["commit"] or bundle["git_commit"] != lock["git_submodule_commit"]:
            raise ValueError("Runtime bundle does not match the source lock")
        required = {"cgit", "eelinux-build.json", "install.py", "source-lock.json", *ASSETS}
        if set(bundle["files"]) != required:
            raise ValueError("Runtime bundle has an unexpected file inventory")
        for name, digest in bundle["files"].items():
            file = source / name
            if file.is_symlink() or not file.is_file() or sha256(file.read_bytes()).hexdigest() != digest:
                raise ValueError(f"Runtime bundle file differs from its recorded hash: {name}")
        binary = (source / "cgit").read_bytes()
        if not binary.startswith(b"\x7fELF") or sha256(binary).hexdigest() != report["binary_sha256"]:
            raise ValueError("Runtime binary differs from the recorded build")
        return source, report
    if git("rev-parse", "HEAD", cwd=source) != lock["commit"]:
        raise ValueError("cgit checkout differs from the source lock")
    git("diff", "--exit-code", "--ignore-submodules=untracked", cwd=source)
    git("diff", "--cached", "--exit-code", "--ignore-submodules=untracked", cwd=source)
    gitlink = git("ls-tree", "HEAD", "git", cwd=source).split()[2]
    if gitlink != lock["git_submodule_commit"] or gitlink != report["git_commit"] or git("rev-parse", "HEAD", cwd=source / "git") != gitlink:
        raise ValueError("Build report and Git submodule revision differ")
    git("diff", "--exit-code", cwd=source / "git")
    git("diff", "--cached", "--exit-code", cwd=source / "git")
    binary = (source / "cgit").read_bytes()
    if not binary.startswith(b"\x7fELF") or sha256(binary).hexdigest() != report["binary_sha256"]:
        raise ValueError("Compiled binary differs from its recorded build hash")
    return source, report


def make_plan(document_root, private_root, repository, build_dir, public_url, branch="main"):
    docroot = account_path(document_root)
    private = account_path(private_root)
    repo = account_path(repository)
    public_url, prefix = url_settings(public_url)
    if not docroot.is_dir():
        raise ValueError("The existing domain document root must be supplied")
    if private.is_relative_to(docroot) or docroot.is_relative_to(private):
        raise ValueError("Private cgit storage and domain document root must be separate trees")
    if repo.is_relative_to(docroot):
        raise ValueError("The bare Git repository must be outside the document root")
    if repo == private or private.is_relative_to(repo):
        raise ValueError("Private cgit storage must not be placed inside the Git repository")
    if not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9._/\-]*", branch) or ".." in branch:
        raise ValueError("Invalid default branch")
    public = docroot / prefix.strip("/")
    binary = private / "bin" / "cgit"
    config = private / "cgitrc"
    wrapper = (
        "#!/bin/sh\n"
        "# Managed by eeLinux deploy/cgit/install.py.\n"
        "unset CDPATH ENV BASH_ENV LD_PRELOAD LD_LIBRARY_PATH\n"
        f"export CGIT_CONFIG={shlex.quote(str(config))}\n"
        f"exec {shlex.quote(str(binary))}\n"
    )
    cgitrc = f"""# Managed by eeLinux deploy/cgit/install.py.
root-title=eeLinux source
root-desc=GNU/Linux for the PlayStation 2
virtual-root={prefix}/
css={prefix}/static/cgit.css
js={prefix}/static/cgit.js
logo={prefix}/static/cgit.png
favicon={prefix}/static/favicon.ico
logo-link=https://eelinux.falanet.org/
cache-root={private}/cache
cache-size=256
cache-dynamic-ttl=2
enable-http-clone=1
enable-index-links=1
enable-commit-graph=1
enable-log-filecount=1
enable-log-linecount=1
enable-filter-overrides=0
enable-html-serving=0
max-blob-size=1024
snapshots=tar.gz

repo.url=eelinux
repo.name=eeLinux
repo.path={repo}
repo.desc=Kernel, firmware, GNU userland and native GS desktop
repo.defbranch={branch}
repo.homepage=https://eelinux.falanet.org/
repo.clone-url={public_url}/eelinux
"""
    htaccess = f"""# Managed by eeLinux deploy/cgit/install.py.
Options +ExecCGI -Indexes
AddHandler cgi-script .cgi
DirectoryIndex index.cgi
AcceptPathInfo On

RewriteEngine On
RewriteBase {prefix}/
RewriteRule ^(?:static(?:/|$)|index\\.cgi(?:/|$)) - [END]
RewriteRule ^$ index.cgi [END]
RewriteRule ^(.*)$ index.cgi/$1 [END,QSA]
"""
    plan = {
        binary: Payload((build_dir / "cgit").read_bytes(), 0o700),
        config: Payload(cgitrc.encode(), 0o600),
        public / "index.cgi": Payload(wrapper.encode(), 0o755),
        public / ".htaccess": Payload(htaccess.encode(), 0o644),
    }
    for name in ASSETS:
        plan[public / "static" / name] = Payload((build_dir / name).read_bytes(), 0o644)
    locations = {"document_root": str(docroot), "private_root": str(private), "repository": str(repo), "public_url": public_url}
    return plan, locations


def preflight(plan, locations, update=False, existing_public=False):
    private = Path(locations["private_root"])
    public = Path(locations["document_root"]) / "git"
    state_path = private / "install-state.json"
    if state_path.is_symlink():
        raise ValueError("Refusing a symbolic-link installation manifest")
    state = json.loads(state_path.read_text(encoding="utf-8")) if state_path.exists() else None
    if state and state["locations"] != locations:
        raise ValueError("Existing installation belongs to different paths or URL")
    if not state and public.exists() and any(public.iterdir()) and not existing_public:
        raise ValueError(f"Refusing a nonempty unmanaged public directory: {public}")
    if not state and private.exists() and any(private.iterdir()):
        raise ValueError(f"Refusing a nonempty unmanaged private directory: {private}")
    for path, payload in plan.items():
        account_path(path)
        if not path.exists():
            continue
        if not path.is_file():
            raise ValueError(f"Destination is not a regular file: {path}")
        actual = sha256(path.read_bytes()).hexdigest()
        desired = sha256(payload.data).hexdigest()
        if actual == desired:
            continue
        previous = state.get("files", {}).get(str(path)) if state else None
        if not update or actual != previous:
            raise ValueError(f"Existing file differs; left untouched: {path}")
    return state_path


def atomic_write(path, data, mode):
    if path.exists() and path.read_bytes() == data:
        path.chmod(mode)
        return
    with tempfile.NamedTemporaryFile(dir=path.parent, prefix=".eelinux-", delete=False) as out:
        temporary = Path(out.name)
        try:
            out.write(data)
            out.flush()
            os.fsync(out.fileno())
            temporary.chmod(mode)
        except BaseException:
            temporary.unlink(missing_ok=True)
            raise
    os.replace(temporary, path)


def install(plan, locations, report, update=False, dry_run=False, existing_public=False):
    state_path = preflight(plan, locations, update, existing_public)
    if dry_run:
        print(json.dumps({"locations": locations, "files": [str(path) for path in plan]}, indent=2))
        return
    private = Path(locations["private_root"])
    private.mkdir(mode=0o700, parents=True, exist_ok=True)
    private.chmod(0o700)
    for child in (private / "bin", private / "cache"):
        child.mkdir(mode=0o700, exist_ok=True)
        child.chmod(0o700)
    public = Path(locations["document_root"]) / "git"
    public.mkdir(mode=0o755, exist_ok=True)
    (public / "static").mkdir(mode=0o755, exist_ok=True)
    for path, payload in plan.items():
        atomic_write(path, payload.data, payload.mode)
    state = {"locations": locations, "build": report, "files": {str(path): sha256(payload.data).hexdigest() for path, payload in plan.items()}}
    atomic_write(state_path, (json.dumps(state, indent=2) + "\n").encode(), 0o600)
    print("Installed:", locations["public_url"] + "/")
    print("Verify HTTP browsing and clone before considering deployment complete.")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--document-root", required=True, help="Existing domain document root, not necessarily public_html")
    parser.add_argument("--private-root", required=True, help="Dedicated private cgit directory, outside the document root")
    parser.add_argument("--repository", required=True, help="Existing bare Git repository outside the document root")
    parser.add_argument("--build-dir", required=True, help="Verified output directory printed by build.sh")
    parser.add_argument("--public-url", default=DEFAULT_URL)
    parser.add_argument("--branch", default="main")
    parser.add_argument("--update", action="store_true", help="Update only unchanged files recorded by this installer")
    parser.add_argument("--dry-run", action="store_true", help="Validate paths/build and print files without writing")
    parser.add_argument("--existing-public-directory", action="store_true", help="Install absent named files alongside an existing /git tree; never replace unknown files")
    args = parser.parse_args()
    if os.name != "posix":
        parser.error("Run this installer on the Linux hosting server or in a matching Linux environment")
    try:
        source, report = load_build(args.build_dir)
        repo = account_path(args.repository)
        if git("--git-dir", repo, "rev-parse", "--is-bare-repository") != "true":
            raise ValueError("An existing bare Git repository is required")
        plan, locations = make_plan(args.document_root, args.private_root, repo, source, args.public_url, args.branch)
        install(plan, locations, report, args.update, args.dry_run, args.existing_public_directory)
    except (OSError, ValueError, KeyError, subprocess.CalledProcessError) as error:
        parser.exit(1, f"cgit setup stopped: {error}\n")


if __name__ == "__main__":
    main()
