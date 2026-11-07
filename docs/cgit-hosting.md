# cgit at eelinux.falanet.org/git

The deployment target is `https://eelinux.falanet.org/git/`, with the repository
at `/git/eelinux`. The existing website and release downloads remain separate
from the Git repository. This folder contains configuration and installation
tools, not a claim that the hosting account has already been configured.

## Hosting requirements

The account must allow a Linux CGI executable, a shell CGI wrapper, `PATH_INFO`,
and Apache-compatible rewrite rules under the domain's document root. LiteSpeed
hosting needs the corresponding CGI handler enabled by the provider. The
installer expects CGI to run as the account owner, for example through suEXEC.
SSH Git access is used for pushes. A control panel's Git feature alone does not
establish that custom CGI binaries are permitted.

Confirm the SSH host, account, port and **actual domain document root** first.
An addon domain may have its own directory rather than the account's default
`public_html`. The tools require explicit absolute paths and do not guess them.

## Source and build

`deploy/cgit/source-lock.json` pins the official cgit v1.3.1 commit
`044821677c774cd24f25f1818ea51d09cc64b006`. The build script checks that the
release resolves to it and checks the Git submodule against that commit's
gitlink. This records exact upstream content; it does not assert independent
signature verification.

Build on the hosting server, or a Linux environment with matching architecture
and compatible libc/library versions. Do not upload a Windows executable or
assume that a binary built on a newer distribution will run on an older host.
The host needs Git, Python 3.9+, Make, a C compiler and the development libraries
needed by cgit/Git, including zlib and OpenSSL. Lua is disabled in this setup.
The build script does not install system packages or require root.

An optional third argument `static` disables optional OpenSSL, Expat and iconv
dependencies and asks the linker for a static executable. Static libc and zlib
development archives must already exist. The resulting report records whether
the ELF has a dynamic interpreter. This reduces shared-library dependencies;
the server architecture and permission to execute CGI still need verification.

Run from a source checkout on the server:

```sh
sh deploy/cgit/build.sh /home/ACCOUNT/.cache/eelinux-cgit 2
```

Use an external work directory, outside the project. The result is printed as
`/home/ACCOUNT/.cache/eelinux-cgit/cgit-044821677c774cd24f25f1818ea51d09cc64b006`.
It includes `eelinux-build.json` with the source revisions and binary hash.
Downloaded upstream source, object files and the executable belong in that work
directory, not this repository. A provider-installed binary needs a separately
reviewed adaptation; the installer deliberately accepts the verified build
report produced by this workflow.

To transfer a verified runtime without uploading its source checkout and object
files, run `deploy/cgit/stage_bundle.py --build-dir BUILD --destination DEST` on
the build machine. `DEST` must be a fresh external directory. It contains only
the executable, four assets, build report, installer, source lock and a hash
inventory. Upload that directory outside the host's document root, and run its
`install.py` with `--build-dir` pointing to the uploaded directory. The installer
checks every inventory hash before using it. Keep the installer and its source
lock from this project together; the hash inventory is an integrity record, not
an independently signed binary release.
Add `--strip` to remove unneeded symbols from the staged executable and record
the new binary hash; the original verified build remains unchanged. Run the CGI
smoke test against that final runtime bundle as well.

## Bare repository

Keep the repository outside the web root. After reviewing the clean local
source and making the initial commit, create the server repository once:

```sh
mkdir -p /home/ACCOUNT/repos
git init --bare -b main /home/ACCOUNT/repos/eelinux.git
```

From the local project, replacing the SSH host/account/port with the confirmed
values:

```sh
git remote add server ssh://ACCOUNT@SSH_HOST:SSH_PORT/home/ACCOUNT/repos/eelinux.git
git push -u server main
```

cgit is the public browser and read-only HTTP clone endpoint. Pushes use SSH.
Git does not preserve download-file hardlinks: committing one stores its full
bytes. Keep ISO/IMG files, generated ZIPs, build output and download caches out
of the source history. Keep recipes, patches, locks, checksums and licences.

## Install the account files

The paths below are examples. Use the confirmed document root, a dedicated
private cgit directory and the real repository path. The private directory and
repository must be outside the document root.

```sh
python3 deploy/cgit/install.py \
  --document-root /home/ACCOUNT/public_html/eelinux.falanet.org \
  --private-root /home/ACCOUNT/eelinux-cgit \
  --repository /home/ACCOUNT/repos/eelinux.git \
  --build-dir /home/ACCOUNT/.cache/eelinux-cgit/cgit-044821677c774cd24f25f1818ea51d09cc64b006 \
  --dry-run
```

Review the printed locations, then run the same command without `--dry-run`.
It installs the binary, `cgitrc`, cache and installation state privately, and
only `git/index.cgi`, `git/.htaccess` and four static assets publicly. The CGI
wrapper sets the private `CGIT_CONFIG`; public URLs use `/git/` and rewrite to
the CGI program with path information. No configuration files or repository
internals are copied into the website tree.

An upstream cgit source tree may already have been uploaded to `/git`. Use
`--existing-public-directory` in both preview and installation only after
reviewing that directory. This permits adding the named files alongside it;
it does not remove the uploaded source or overwrite an unknown `.htaccess`,
`index.cgi` or asset. A collision stops before any installation writes. Preserve
and merge existing hosting rules deliberately rather than using a force flag.

For later installations, `--update` permits replacing only unchanged files
recorded by this installer. Locally modified managed files are left untouched.
Unrecognized files are never deleted. The generated state and cache are private
runtime data; do not commit them to the source repository.

## Verify the live result

Check all of the following against the deployed server:

```sh
curl -fIL https://eelinux.falanet.org/git/
curl -fI https://eelinux.falanet.org/git/static/cgit.css
curl -fI https://eelinux.falanet.org/git/eelinux/tree/
git clone https://eelinux.falanet.org/git/eelinux /path/to/fresh-checkout
```

Inspect the repository's summary, tree, log and a commit diff in a browser.
Check that the clone contains the expected commit and excludes image files and
build packages. Link the main website's source-code entry to `/git/eelinux/`.

If the host returns 500, inspect its error log. Common causes include forbidden
`Options` or rewrite directives, a disabled CGI handler, ownership/permission
rules or missing shared libraries. A directory listing of cgit's source files
means the CGI program is not yet serving that URL. Do not treat a successful
upload or an existing directory listing as completed deployment.

Local helper checks, without contacting the hosting server:

```sh
sh -n deploy/cgit/build.sh
python3 -m unittest discover -s deploy/cgit -p test_install.py -v
python3 deploy/cgit/smoke_test.py --build-dir /path/to/verified-build
```

## Primary references

- [cgit build instructions](https://git.zx2c4.com/cgit/plain/README)
- [cgit configuration manual](https://git.zx2c4.com/cgit/plain/cgitrc.5.txt)
- [cgit release references](https://git.zx2c4.com/cgit/refs/)
- [Apache CGI configuration](https://httpd.apache.org/docs/2.4/howto/cgi.html)
- [Apache rewrite configuration](https://httpd.apache.org/docs/2.4/mod/mod_rewrite.html)

## FTP upload followed by one shell command

FTP can upload files but cannot run the private installer. Package the verified
static Linux x86_64 runtime and a bare repository containing the reviewed source
commit into one Python zipapp. Generate it outside the checkout after committing:

```sh
python3 -B deploy/cgit/make_installer.py \
  --runtime /path/to/eelinux-cgit-deployment/runtime \
  --repository /path/to/reviewed/eelinux.git \
  --output /path/to/eelinux-cgit-deployment/eelinux-cgit-install-COMMIT.pyz
```

The repository is embedded as a self-contained Git bundle. Local repository
configuration, hooks, credentials and external object paths are not copied.
The generator records exact refs, source and runtime revisions, lengths and
SHA-256 hashes. It refuses to replace an existing installer. The hashes identify
the package; they are not an independent signature.

Upload the `.pyz` to a private location reachable by the account, then run it
from the hosting account's existing SSH shell or control-panel Terminal:

```sh
python3 -B /absolute/private/path/eelinux-cgit-install-COMMIT.pyz
```

The defaults for this account are:

```text
Document root: /home/falaneto/public_html/eelinux
Private cgit:  /home/falaneto/eelinux-cgit
Repository:   /home/falaneto/repos/eelinux.git
Setup work:   /home/falaneto/eelinux-cgit-setup
Public URL:   https://eelinux.falanet.org/git/
```

Explicit `--document-root`, `--private-root`, `--repository` and `--work-root`
arguments override those paths. `--check-only` verifies the package and
installation preflight without installing the repository or public files.
Python 3.9+, Git and Linux x86_64 are required. No password or FTP credential
belongs in the installer or source repository.

Before extracting or importing payload code, the zipapp checks the exact file
inventory, lengths, hashes, absence of symlinks and absence of traversal paths.
It stages files in a fresh private setup directory outside the document root.
A new bare repository is cloned from the embedded bundle and checked with Git
before installation. An existing repository is reused only when its default
branch, commit and complete ref inventory match the package. Other repositories
and unknown file collisions are preserved. The regular installer's
hash-recorded update rules still apply through `--update`.

This is a command-line handoff, not a web-accessible installer endpoint.
After it runs, verify the live pages and HTTP clone as described above.
