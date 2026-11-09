# eeLinux cgit theme

This theme carries the eeLinux website's paper background, navy wordmark, sage sidebar, Georgia text and monospace source views into the existing cgit installation. It uses the website's SVG `ee` mark. The native cgit stylesheet loads first; the theme adds overrides through `head-include`. cgit's tree, log, diff, search, snapshot and clone functions continue to use the existing runtime.

The three HTML fragments are deliberately independent. cgit inserts `header` immediately after `<body>` and inserts `footer` inside `#cgit`, after its content. The sidebar closes completely in `header.html`; no wrapper starts in one include and ends in another. `head.html` supplies the viewport, browser theme colour and the additional stylesheet. The supported options and insertion points are documented in [cgitrc.5](https://git.zx2c4.com/cgit/tree/cgitrc.5.txt) and [ui-shared.c](https://git.zx2c4.com/cgit/tree/ui-shared.c).

On phones and tablets, the project navigation folds into a native Menu disclosure and the repository uses the full page width. Both navigation menus include Forum and Mailing list links alongside Git. Repository tables, paths, hashes, clone URLs, commit text and commands wrap within the available space. Search, branch selection and repository tabs retain their native cgit actions, with larger touch targets.

The deferred `eelinux-cgit.js` helper keeps source line numbers beside their corresponding text when long lines wrap. It works from the HTML already rendered by cgit. The theme does not alter the stored source, raw file responses, patches, snapshots or clone transport. The original cgit script remains loaded for its existing controls.

## Apply on the existing server

The uploaded, self-contained updater can be activated directly in the hosting account's shell:

```sh
python3 -B "$(find /home/falaneto -type f -name 'eelinux-cgit-theme-b108683b5a8a421a.pyz' -print -quit)"
```

FTP transfers the updater but cannot execute it. This command uses the same installer and defaults described below; no Git commit is required. To validate without activating, append `--check-only`. Do not run it concurrently with another deployment.

Python 3.9 or newer is required. Run this in the server shell after uploading or extracting the theme files together:

```sh
python3 -B install_theme.py --theme-dir "$PWD" --check-only
python3 -B install_theme.py --theme-dir "$PWD"
```

The defaults describe the existing installation:

| Setting | Default |
| --- | --- |
| Public cgit directory | `/home/falaneto/public_html/eelinux/git` |
| Private cgit directory | `/home/falaneto/eelinux-cgit` |
| Bare repository | `/home/falaneto/repos/eelinux.git` |
| Public URL | `https://eelinux.falanet.org/git/` |

For a separate test installation, pass `--public-root /absolute/path/git`, `--private-root /absolute/private/path`, `--repository /absolute/repository.git` and, if needed, `--public-url https://example.org/git`. `--document-root` accepts the parent of `/git`; `--dry-run` is an alias for `--check-only`.

The updater requires the existing installer's `install-state.json` and verifies every recorded file against its SHA-256 hash. It refuses changed deployment files, unknown differing theme files, symbolic links and destinations inside a Git checkout or the bare repository. Read-only validation creates no directories or files. An identical existing theme asset can be adopted and is backed up before activation.

Only these cgitrc keys are managed: `logo`, `favicon`, `header`, `footer`, `head-include`, `root-title`, `root-desc` and `cache-root`. Every other configuration line is retained exactly, including `css`, `js`, repository paths, clone URLs, filters and HTTP clone settings. The runtime binary, CGI wrapper, rewrite rules and stock assets are retained.

Public assets use `/git/static/eelinux-theme-<content-hash>/`; HTML includes stay private under `themes/<content-hash>/`. The complete new theme is written before an atomic configuration replacement activates it. Each changed installation selects a fresh private cache directory so the old generated HTML is not served. Existing caches are retained. Repeating the same installation leaves its configuration, state and cache namespace in place.

Before activation, a dated directory under `eelinux-cgit/theme-backups/` receives the original cgitrc, installation state and any existing files at the new theme destinations. The backup directory is private (0700), and its files are private (0600). `backup.json` maps every copy to its original absolute path and records its hash and permissions. The updater prints the backup path. Keep it until live browsing and cloning have been checked.

To roll back, first check the backup records and SHA-256 hashes, then restore the backed-up `cgitrc` and `install-state.json` to their recorded paths with permissions 0600. The original stylesheet, logo and cache are still present; restoring the configuration returns to them. New versioned theme files can remain unused. If a process is interrupted after configuration activation but before the state write, restore that pair from the backup before rerunning.

The updater merges its file hashes into the existing installation state and preserves the build metadata. A later run of the original `deploy/cgit/install.py --update` can replace the configuration with that installer's base configuration; reapply this theme afterwards. Keep the theme source with the deployment tools.

After applying, inspect the repository index, summary, tree, source file, log and diff, then verify HTTP clone against `https://eelinux.falanet.org/git/eelinux`. No Git commit is needed to install the theme.

## Local checks

To build a new self-contained updater, choose an output directory outside the source checkout:

```sh
python3 -B package_theme.py --output-directory /absolute/path/to/deployment-artifacts
```

The package includes the stylesheet, source-view helper, SVG assets, HTML includes, installer and a checked file manifest. Its generated filename identifies that exact package; use the new filename when uploading and activating a later version. Keep generated packages outside this repository.

Run on Linux or WSL, from this directory:

```sh
PYTHONDONTWRITEBYTECODE=1 python3 -B -m unittest -v test_theme_install
```

The tests cover nonpresentation configuration preservation, manifest hashes, private backups, idempotent reruns, unknown file protection, identical asset adoption, modified installation refusal, symbolic links, Git checkout protection, read-only validation, navigation consistency, theme upgrades and balanced HTML fragments at cgit's include positions.
