#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Exercise the actual cgit CGI and an HTTP clone using disposable repositories."""

from __future__ import annotations

import argparse
from functools import partial
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from io import BytesIO
import json
import os
from pathlib import Path
import subprocess
import tarfile
from tempfile import TemporaryDirectory
from threading import Thread
from urllib.parse import urlsplit
from urllib.request import urlopen

import install


def run_git(args, cwd=None, env=None):
    return subprocess.check_output(["git", *map(str, args)], cwd=cwd, env=env,
                                   stderr=subprocess.PIPE, text=True, timeout=30).strip()


def smoke(source):
    source, build = install.load_build(source)
    with TemporaryDirectory(prefix="eelinux-cgit-smoke-") as work:
        root = Path(work)
        home, docroot, tree = root / "home", root / "public", root / "source"
        for path in (home, docroot, tree):
            path.mkdir()
        env = dict(os.environ, HOME=str(home), GIT_CONFIG_NOSYSTEM="1", GIT_CONFIG_GLOBAL="/dev/null")
        run_git(["init", "-q", "-b", "main"], tree, env)
        (tree / "README.md").write_text("# eeLinux CGI fixture\nFirst revision.\n")
        run_git(["add", "."], tree, env)
        identity = ["-c", "user.name=eeLinux test", "-c", "user.email=test@example.invalid"]
        run_git([*identity, "commit", "-q", "-m", "Initial fixture"], tree, env)
        (tree / "README.md").write_text("# eeLinux CGI fixture\nSecond revision.\n")
        run_git(["add", "."], tree, env)
        run_git([*identity, "commit", "-q", "-m", "Update fixture"], tree, env)
        expected = run_git(["rev-parse", "HEAD"], tree, env)
        bare = root / "repos/eelinux.git"
        bare.parent.mkdir()
        run_git(["clone", "--bare", "-q", tree, bare], env=env)
        plan, locations = install.make_plan(docroot, root / "private", bare, source, install.DEFAULT_URL)
        install.install(plan, locations, build)
        cgi = docroot / "git/index.cgi"

        class Handler(SimpleHTTPRequestHandler):
            def log_message(self, *args):
                pass

            def do_GET(self):
                request = urlsplit(self.path)
                if request.path.startswith("/git/static/"):
                    return super().do_GET()
                if not request.path.startswith("/git/"):
                    return self.send_error(404)
                cgi_env = dict(env, REQUEST_METHOD="GET", QUERY_STRING=request.query,
                               SCRIPT_NAME="/git/index.cgi", PATH_INFO=request.path[len("/git"):],
                               REQUEST_URI=self.path, SERVER_NAME="eelinux.falanet.org",
                               SERVER_PORT="443", SERVER_PROTOCOL="HTTP/1.1", HTTPS="on")
                result = subprocess.run([str(cgi)], env=cgi_env, stdout=subprocess.PIPE,
                                        stderr=subprocess.PIPE, input=b"", timeout=30)
                if result.returncode:
                    raise RuntimeError(result.stderr.decode(errors="replace"))
                headers, separator, body = result.stdout.partition(b"\r\n\r\n")
                if not separator:
                    headers, separator, body = result.stdout.partition(b"\n\n")
                if not separator:
                    raise RuntimeError("CGI did not produce a header block")
                values = [line.split(":", 1) for line in headers.decode("latin1").splitlines() if ":" in line]
                status = next((int(value.strip().split()[0]) for name, value in values if name.lower() == "status"), 200)
                self.send_response(status)
                for name, value in values:
                    if name.lower() not in ("status", "content-length", "connection"):
                        self.send_header(name, value.strip())
                self.send_header("Content-Length", str(len(body)))
                self.end_headers()
                self.wfile.write(body)

        server = ThreadingHTTPServer(("127.0.0.1", 0), partial(Handler, directory=str(docroot)))
        server.daemon_threads = True
        thread = Thread(target=server.serve_forever, daemon=True)
        thread.start()
        origin = f"http://127.0.0.1:{server.server_port}"
        routes = ["/git/", "/git/eelinux/", "/git/eelinux/tree/", "/git/eelinux/log/",
                  f"/git/eelinux/commit/?id={expected}", f"/git/eelinux/diff/?id={expected}"]
        try:
            for route in routes:
                with urlopen(origin + route, timeout=30) as response:
                    body = response.read()
                    if response.status != 200 or b"eeLinux" not in body:
                        raise AssertionError(f"Unexpected CGI response for {route}")
            with urlopen(origin + "/git/eelinux/plain/README.md", timeout=30) as response:
                if response.read() != (tree / "README.md").read_bytes():
                    raise AssertionError("Plain source readback differs")
            with urlopen(origin + "/git/static/cgit.css", timeout=30) as response:
                if response.read() != (source / "cgit.css").read_bytes():
                    raise AssertionError("Static CSS readback differs")
            with urlopen(origin + "/git/eelinux/snapshot/eelinux-main.tar.gz", timeout=30) as response:
                archive = response.read()
            with tarfile.open(fileobj=BytesIO(archive), mode="r:gz") as snapshot:
                readme = snapshot.extractfile("eelinux-main/README.md")
                if readme is None or readme.read() != (tree / "README.md").read_bytes():
                    raise AssertionError("Snapshot source differs")
            clone = root / "http-clone"
            run_git(["clone", "-q", origin + "/git/eelinux", clone], env=env)
            if run_git(["rev-parse", "HEAD"], clone, env) != expected:
                raise AssertionError("HTTP clone did not reproduce the expected commit")
        finally:
            server.shutdown()
            server.server_close()
            thread.join()
        return {"result": "passed", "build": build, "cgi_pages": len(routes), "plain_readback": True,
                "static_asset_readback": True, "snapshot_readback": True, "http_clone": True,
                "fixture_commit": expected,
                "scope": "Local CGI execution and HTTP transport; does not qualify the hosting provider or its Apache/LiteSpeed rewrite handler."}


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", required=True)
    parser.add_argument("--report", type=Path)
    args = parser.parse_args()
    report = smoke(args.build_dir)
    payload = json.dumps(report, indent=2) + "\n"
    if args.report:
        args.report.write_text(payload, encoding="utf-8")
    print(payload, end="")
