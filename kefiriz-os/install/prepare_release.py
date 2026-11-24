"""Build a self-contained Windows dual-boot installer from the customized Lakka FAT32 tree."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import tarfile
import tempfile
from zipfile import ZIP_DEFLATED, ZIP_STORED, ZipFile, ZipInfo

HERE = Path(__file__).resolve().parent
UPSTREAM = "Lakka-Switch.aarch64-5.0-devel-20240122151037-5f29696"
UPSTREAM_SHA256 = "e34a6e2a8b19cb95e1b1ced10c3ef32a1c4e6a10748a4926e2a2cd6041e6daaf"
HARDWARE = ("KERNEL", "nx-plat.dtimg", "boot/bl31.bin", "boot/bl33.bin", "boot/boot.scr")
ORIGINAL_ENTRY = (
    "[Lakka]\nl4t=1\nboot_prefixes=lakka/boot/\nlogopath=lakka/boot/splash.bmp\n"
    "id=SWR-LAK\nicon=lakka/boot/icon_lakka_hue.bmp\n"
)
ENTRY = ORIGINAL_ENTRY.replace("[Lakka]", "[KEFIRIZ OS]").replace("boot/splash.bmp", "boot/kefiriz-logo.bmp").replace(
    "boot/icon_lakka_hue.bmp", "boot/kefiriz-icon.bmp") + "bootargs_extra=nosplash\n"


def stream_hash(stream, algorithm="sha256"):
    digest = hashlib.new(algorithm)
    for block in iter(lambda: stream.read(2**20), b""):
        digest.update(block)
    return digest.hexdigest()


def digest(path, algorithm="sha256"):
    with path.open("rb") as reader:
        return stream_hash(reader, algorithm)


def verify_input(tree, archive):
    if digest(archive) != UPSTREAM_SHA256:
        raise ValueError("Expected the pinned Lakka Switch 5f29696 upstream archive.")
    with tarfile.open(archive, "r:") as source:
        def member(name):
            return source.extractfile(f"{UPSTREAM}/{name}")
        with member("bootloader/ini/Lakka.ini") as ini:
            if ini.read().decode("ascii").replace("\r\n", "\n") != ORIGINAL_ENTRY:
                raise ValueError("Unexpected upstream Hekate boot entry.")
        for name in HARDWARE:
            with member("lakka/" + name) as handle:
                if stream_hash(handle) != digest(tree / name):
                    raise ValueError(f"Unexpected change to Switch boot hardware file: {name}")
        with member("lakka/SYSTEM.md5") as handle:
            stock_system = handle.read().decode("ascii").split()[0]
    system = tree / "SYSTEM"
    with system.open("rb") as reader:
        if reader.read(4) != b"hsqs":
            raise ValueError("Expected the assembled, customized SquashFS SYSTEM.")
    new_md5 = digest(system, "md5")
    if new_md5 == stock_system or (tree / "SYSTEM.md5").read_text("ascii").split()[0] != new_md5:
        raise ValueError("SYSTEM must be customized and have its matching boot checksum.")
    if digest(tree/"KERNEL", "md5") != (tree/"KERNEL.md5").read_text("ascii").split()[0]:
        raise ValueError("KERNEL.md5 and the upstream kernel disagree.")


def linked(path):
    return path.is_symlink() or bool(getattr(path.lstat(), "st_file_attributes", 0) & 0x400)


def copy_regular_tree(src, dst, *, sources=False):
    if linked(src) or not src.is_dir():
        raise ValueError(f"Expected a plain source directory: {src}")
    dst.mkdir()
    for base, dirs, files in os.walk(src, followlinks=False):
        if sources:
            dirs[:] = [d for d in dirs if d not in {".git", "__pycache__"}]
            files = [f for f in files if not f.endswith((".pyc", ".pyo"))]
        base = Path(base)
        sub = dst / base.relative_to(src)
        for name in sorted(dirs + files):
            p = base / name
            if linked(p):
                raise ValueError(f"Links cannot be included in this package: {p}")
            if p.is_dir():
                (sub/name).mkdir()
            elif p.is_file():
                shutil.copyfile(p, sub/name)
            else:
                raise ValueError(f"Unsupported package input: {p}")


def write_manifest(out, version):
    payload = out / "payload"
    rows, used = [], set()
    for f in sorted((payload/"lakka").rglob("*")):
        if not f.is_file():
            continue
        name = f.relative_to(payload).as_posix()
        if (not re.fullmatch(r"lakka/[ -~]+", name) or re.search(r'[\\:*?"<>|]|[. ](/|$)', name)
                or any(s.split(".", 1)[0].upper() in {"CON", "NUL", "PRN", "AUX", *(f"COM{i}" for i in range(1, 10)),
                    *(f"LPT{i}" for i in range(1, 10))} for s in name.split("/"))
                or name.casefold() in used or f.stat().st_size >= 2**32):
            raise ValueError(f"Unsupported or colliding FAT32 filename: {name}")
        used.add(name.casefold())
        rows.append({"path": name, "size": f.stat().st_size, "sha256": digest(f)})
    manifest = out / "manifest.json"
    manifest.write_bytes((json.dumps({"format": 1, "product": "KEFIRIZ OS", "version": version,
        "upstreamArchiveSha256": UPSTREAM_SHA256, "entrySha256": digest(out/"boot-entry.ini"), "files": rows},
        indent=2, sort_keys=True)+"\n").encode("ascii"))
    (out/"manifest.sha256").write_bytes((digest(manifest)+"\n").encode("ascii"))


def pack_zip(folder, output):
    if output.exists() or output.is_relative_to(folder):
        raise ValueError("Select a new ZIP path outside the installer directory.")
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix=".kefiriz-zip-", dir=output.parent) as staging:
        staged = Path(staging) / "archive.zip"
        with ZipFile(staged, "x", allowZip64=True) as archive:
            for file in sorted(path for path in folder.rglob("*") if path.is_file()):
                info = ZipInfo(file.relative_to(folder).as_posix(), (2026, 1, 1, 0, 0, 0))
                info.compress_type = ZIP_STORED if file.name in ("SYSTEM","KERNEL") else ZIP_DEFLATED
                info.create_system = 3
                info.external_attr = 0o100644 << 16
                with file.open("rb") as src, archive.open(info, "w", force_zip64=file.stat().st_size > 2**31-1) as dst:
                    shutil.copyfileobj(src, dst, 2**20)
        with ZipFile(staged) as archive:
            if archive.testzip():
                raise ValueError("Final archive CRC validation failed.")
        staged.rename(output)
    print(output, digest(output))


def build(args):
    out = args.output.resolve()
    tree = args.payload.resolve() / "lakka"
    if out.exists() or out.is_relative_to(tree) or any(out.is_relative_to(x.resolve()) for x in args.sources):
        raise ValueError("Choose a new output directory outside the input trees.")
    if not re.fullmatch(r"[0-9]+\.[0-9]+\.[0-9]+", args.version):
        raise ValueError("Use a three-part release version.")
    verify_input(tree, args.upstream_archive)
    out.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix=".kefiriz-build-", dir=out.parent) as staging:
        result = Path(staging) / "installer"
        (result/"payload").mkdir(parents=True)
        copy_regular_tree(tree, result/"payload/lakka")
        (result/"payload/lakka/storage").mkdir(exist_ok=True)
        for bmp in ("kefiriz-icon.bmp","kefiriz-logo.bmp"):
            shutil.copyfile(HERE.parent/"ui/boot"/bmp, result/"payload/lakka/boot"/bmp)
        for name in ("Install.ps1", "Menu.ps1", "CardFiles.ps1", "INSTALL.bat", "README.md"):
            shutil.copyfile(HERE/name, result/name)
        (result/"boot-entry.ini").write_bytes(ENTRY.encode("ascii"))
        if args.sources:
            (result/"source").mkdir()
            for path in args.sources:
                copy_regular_tree(path.resolve(),result/"source"/path.resolve().name, sources=True)
        write_manifest(result, args.version)
        result.rename(out)
    print(out)
    if args.zip:
        pack_zip(out, args.zip.resolve())


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--payload", required=True, type=Path, help="Assembled FAT32 directory containing lakka/")
    parser.add_argument("--upstream-archive", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--zip", type=Path)
    parser.add_argument("--version", default="0.1.0")
    parser.add_argument("--sources", type=Path, nargs="*", default=[], help="Optional prepared source directory trees")
    build(parser.parse_args())
