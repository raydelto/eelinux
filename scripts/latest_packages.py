"""Apply checksum-pinned package upgrades before Buildroot evaluates recipes."""
from __future__ import annotations

import hashlib
import io
import json
import lzma
from pathlib import Path, PurePosixPath
import re
import shutil
import struct
import tarfile
import urllib.parse
import zipfile
import zlib

from common import ROOT, download

MANIFEST = ROOT / "configs/latest-packages.json"


class LatestPackageError(RuntimeError):
    pass


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def local_file(relative):
    parts = PurePosixPath(relative)
    if parts.is_absolute() or ".." in parts.parts:
        raise LatestPackageError("Unsafe latest-package input path: " + str(relative))
    path = ROOT.joinpath(*parts.parts)
    if not path.resolve().is_relative_to(ROOT.resolve()) or not path.is_file():
        raise LatestPackageError("Missing latest-package input: " + str(relative))
    return path


def manifest(path=None):
    path = Path(path) if path is not None else MANIFEST
    if not path.exists():
        return {"schema": 1, "packages": {}}
    data = json.loads(path.read_text(encoding="utf-8"))
    if data.get("schema") != 1 or not isinstance(data.get("packages"), dict):
        raise LatestPackageError("Invalid latest package manifest schema")
    for name, record in data["packages"].items():
        if not re.fullmatch(r"[a-z0-9][a-z0-9+_-]*", name):
            raise LatestPackageError("Invalid latest package name: " + name)
        filename = record.get("filename", record.get("source"))
        url = record.get("url", record.get("upstream", record.get("source_url")))
        if not filename or PurePosixPath(filename).name != filename or "\\" in filename:
            raise LatestPackageError("Invalid source filename for " + name)
        if not isinstance(record.get("version"), str) or any(c in record["version"] for c in "\n\r\0"):
            raise LatestPackageError("Invalid version for " + name)
        if urllib.parse.urlparse(url or "").scheme not in ("https", "http"):
            raise LatestPackageError("Latest source requires a complete HTTP(S) URL: " + name)
        if not re.fullmatch(r"[0-9a-f]{64}", record.get("sha256", "")):
            raise LatestPackageError("Invalid source SHA256 for " + name)
    return data


def identity(data=None):
    data = manifest() if data is None else data
    return {"manifest_sha256": digest(MANIFEST) if MANIFEST.exists() else None,
            "records_sha256": hashlib.sha256(json.dumps(data, sort_keys=True, separators=(",", ":")).encode()).hexdigest(),
            "overlay_builder_sha256": digest(__file__),
            "recipe_overrides": {name: digest(local_file(item["recipe_override"]))
                                 for name, item in data["packages"].items()
                                 if item.get("recipe_override")}}


def cache_sources(data, work):
    """Seed Buildroot's per-package cache from complete upstream URLs."""
    paths = {}
    for name, record in sorted(data["packages"].items()):
        filename = record.get("filename", record.get("source"))
        recipe = record.get("recipe", f"package/{name}/{name}.mk")
        cache_name = record.get("download_subdir", PurePosixPath(recipe).stem)
        if not re.fullmatch(r"[a-z0-9][a-z0-9+_-]*", cache_name):
            raise LatestPackageError("Unsafe download subdirectory: " + cache_name)
        destination = Path(work) / "downloads" / cache_name / filename
        vendored = ROOT / "userland/sources" / filename
        if not destination.exists() and vendored.exists():
            if digest(vendored) != record["sha256"]:
                raise LatestPackageError("Vendored latest source checksum mismatch: " + str(vendored))
            destination.parent.mkdir(parents=True, exist_ok=True)
            temporary = destination.with_name(filename + ".part")
            shutil.copyfile(vendored, temporary)
            temporary.replace(destination)
        try:
            download(record.get("url", record.get("upstream", record.get("source_url"))),
                     destination, record["sha256"])
        except RuntimeError as exc:
            raise LatestPackageError(str(exc)) from exc
        paths[name] = destination
    return paths


def set_variable(text, variable, value):
    """Replace a recipe's base assignment while preserving feature additions."""
    if not re.fullmatch(r"[A-Z][A-Z0-9_]*", variable) or not isinstance(value, str):
        raise LatestPackageError("Invalid recipe variable: " + str(variable))
    if any(c in value for c in "\n\r\0"):
        raise LatestPackageError("Recipe values must use one line: " + variable)
    lines = text.splitlines(keepends=True)
    matches = [i for i, line in enumerate(lines)
               if re.match(r"^" + re.escape(variable) + r"\s*(?::=|\?=|=)", line)]
    replacement = variable + " = " + value + "\n"
    if len(matches) > 1:
        raise LatestPackageError("Multiple base assignments require a reviewed recipe override: " + variable)
    if matches:
        start = matches[0]
        end = start + 1
        while end <= len(lines) and lines[end - 1].rstrip().endswith("\\"):
            end += 1
        lines[start:end] = [replacement]
    else:
        # New variables must be present before the package infrastructure's eval.
        at = next((i for i, line in enumerate(lines) if line.startswith("$(eval")), len(lines))
        lines[at:at] = [replacement]
    return "".join(lines)


def variables(text):
    values = {}
    for match in re.finditer(r"^([A-Z][A-Z0-9_]*)[ \t]*(?::=|\?=|=)[ \t]*((?:[^\n]*\\\n)*[^\n]*)", text, re.M):
        values[match[1]] = re.sub(r"\\\n\s*", " ", match[2]).strip()
    for _ in range(20):
        updated = {key: re.sub(r"\$\(([A-Z][A-Z0-9_]*)\)",
                              lambda m: values.get(m[1], m[0]), value)
                   for key, value in values.items()}
        if updated == values:
            break
        values = updated
    return values


def lzip_member(body):
    """Read a single Lzip v1 source archive, checking all container checksums."""
    if len(body) < 26 or body[:5] != b"LZIP\x01":
        raise LatestPackageError("Invalid Lzip source archive header")
    code = body[5]
    base = 1 << (code & 31)
    dictionary = base - (base // 16) * (code >> 5)
    if dictionary < 4096 or dictionary > 1 << 29:
        raise LatestPackageError("Invalid Lzip dictionary size")
    crc, size, member_size = struct.unpack("<IQQ", body[-20:])
    if member_size != len(body):
        raise LatestPackageError("Concatenated or truncated Lzip source archive")
    result = lzma.decompress(body[6:-20], format=lzma.FORMAT_RAW,
                             filters=[{"id": lzma.FILTER_LZMA1, "dict_size": dictionary,
                                       "lc": 3, "lp": 0, "pb": 2}])
    if len(result) != size or zlib.crc32(result) != crc:
        raise LatestPackageError("Lzip source archive checksum mismatch")
    return result


def archive_files(path):
    """Read regular source files without extracting anything into the host tree."""
    files = {}
    if zipfile.is_zipfile(path):
        with zipfile.ZipFile(path) as archive:
            for item in archive.infolist():
                if not item.is_dir():
                    files[item.filename] = archive.read(item)
    else:
        body = io.BytesIO(lzip_member(Path(path).read_bytes())) if str(path).endswith(".tar.lz") else None
        with tarfile.open(fileobj=body, name=None if body is not None else path) as archive:
            for item in archive:
                if item.isfile():
                    files[item.name] = archive.extractfile(item).read()
    normalized = {}
    for name, body in files.items():
        name = name.removeprefix("./")
        parts = PurePosixPath(name)
        if parts.is_absolute() or ".." in parts.parts:
            raise LatestPackageError("Unsafe source archive member: " + name)
        normalized[name] = body
    roots = {name.split("/", 1)[0] for name in normalized}
    if len(roots) == 1 and all("/" in name for name in normalized):
        normalized = {name.split("/", 1)[1]: body for name, body in normalized.items()}
    return normalized


def update_hashes(path, recipe_text, prefix, record, archive):
    parsed = variables(recipe_text)
    licenses = record.get("license_files")
    if licenses is None:
        value = parsed.get(prefix + "_LICENSE_FILES", "")
        if "$" in value:
            raise LatestPackageError("Unresolved license paths need license_files override: " + prefix)
        licenses = value.split()
    if not isinstance(licenses, list) or any(not isinstance(item, str) for item in licenses):
        raise LatestPackageError("Invalid license_files for " + prefix)
    files = archive_files(archive)
    license_hashes = {}
    for name in licenses:
        if name not in files:
            raise LatestPackageError("Latest source lacks declared license file " + prefix + ": " + name)
        license_hashes[name] = hashlib.sha256(files[name]).hexdigest()
    filename = record.get("filename", record.get("source"))
    # Keep auxiliary downloads' hashes. Main source and license entries below
    # are recalculated from the actual pinned release instead of stale metadata.
    retained = []
    if path.exists():
        for line in path.read_text(encoding="utf-8").splitlines():
            fields = line.split()
            if len(fields) == 3 and fields[0] in ("sha256", "sha512", "sha1", "md5"):
                old_name = fields[2]
                if old_name not in licenses and not re.search(r"\.(?:tar\.(?:gz|xz|bz2|lz)|tgz|zip)$", old_name):
                    retained.append(line)
    lines = ["# Latest release source and license bytes verified by scripts/latest_packages.py.",
             f"sha256  {record['sha256']}  {filename}",
             *[f"sha256  {checksum}  {name}" for name, checksum in license_hashes.items()],
             *retained]
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")
    return license_hashes


def apply(source, work, data=None):
    data = manifest() if data is None else data
    source = Path(source).resolve()
    cache = cache_sources(data, work)
    report = {"identity": identity(data), "packages": {}, "files": {}, "removed_patches": []}
    for name, record in sorted(data["packages"].items()):
        recipe_name = record.get("recipe", f"package/{name}/{name}.mk")
        relative = PurePosixPath(recipe_name)
        if relative.is_absolute() or ".." in relative.parts or relative.parts[0] != "package":
            raise LatestPackageError("Unsafe backend recipe path: " + recipe_name)
        recipe = source.joinpath(*relative.parts)
        if record.get("external") or not recipe.exists() and (ROOT / "userland" / recipe_name).exists():
            report["packages"][name] = {"version": record["version"], "external_recipe": recipe_name,
                                        "source_sha256": record["sha256"]}
            continue
        if not recipe.is_file() or not recipe.resolve().is_relative_to(source):
            raise LatestPackageError("Missing backend recipe: " + recipe_name)
        text = local_file(record["recipe_override"]).read_text(encoding="utf-8") if record.get("recipe_override") else recipe.read_text(encoding="utf-8")
        prefix = record.get("variable_prefix", relative.stem.upper().replace("-", "_"))
        url = record.get("url", record.get("upstream", record.get("source_url")))
        updates = {prefix + "_VERSION": record["version"],
                   prefix + "_SOURCE": record.get("filename", record.get("source")),
                   prefix + "_SITE": url.rsplit("/", 1)[0], prefix + "_SITE_METHOD": "wget",
                   **record.get("variables", {})}
        for variable, value in updates.items():
            text = set_variable(text, variable, value)
        recipe.write_text(text, encoding="utf-8")
        for filename, expected in record.get("remove_patches", {}).items():
            if PurePosixPath(filename).name != filename or not filename.endswith(".patch"):
                raise LatestPackageError("Unsafe retired backend patch: " + filename)
            patch = recipe.parent / filename
            if not patch.is_file() or digest(patch) != expected:
                raise LatestPackageError("Retired backend patch checksum mismatch: " + str(patch))
            report["removed_patches"].append(patch.relative_to(source).as_posix())
            patch.unlink()
        hash_path = recipe.with_suffix(".hash")
        licenses = update_hashes(hash_path, text, prefix, record, cache[name])
        for path in (recipe, hash_path):
            report["files"][path.relative_to(source).as_posix()] = digest(path)
        report["packages"][name] = {"version": record["version"], "source_sha256": record["sha256"],
                                    "recipe": recipe_name, "license_sha256": licenses}
    (source / ".eelinux-latest-packages.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    return report


def verify(source, data=None):
    data = manifest() if data is None else data
    source = Path(source)
    report = json.loads((source / ".eelinux-latest-packages.json").read_text(encoding="utf-8"))
    if report["identity"] != identity(data):
        raise LatestPackageError("Latest package overlay inputs changed; use a new --work directory")
    for name, expected in report["files"].items():
        if not (source / name).is_file() or digest(source / name) != expected:
            raise LatestPackageError("Latest backend recipe was altered: " + name)
    if any((source / name).exists() for name in report["removed_patches"]):
        raise LatestPackageError("A retired latest-package patch reappeared")
    return report
