"""Build and sign a KEFIRJS release feed from a verified Windows distribution."""
import argparse
from hashlib import sha256
import json
import os
from pathlib import Path
import re
from zipfile import ZIP_DEFLATED, ZipFile, ZipInfo

from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey

REPO = "Stasan-minecraft/kefirjs-updates"
PUBLIC_HEADER = '''#pragma once
#define KF_REPO "%s"
#define KF_PUBKEY_HEX "%s"
'''
FILES = (
    "hbmenu.nro",
    "switch/KEFIRJS-Updater/KEFIRJS-Updater.nro",
    "switch/KEFIRIZ-System/KEFIRIZ-System.nro",
    "config/kefiriz/release.txt",
)


def signing_key(create=False):
    import win32crypt

    path = Path(os.environ["LOCALAPPDATA"]) / "KEFIRJS/publishing/ed25519.dpapi"
    if create and not path.exists():
        path.parent.mkdir(parents=True, exist_ok=True)
        seed = Ed25519PrivateKey.generate().private_bytes_raw()
        protected = win32crypt.CryptProtectData(seed, "KEFIRJS update signer", None, None, None, 1)
        with path.open("xb") as handle:
            handle.write(protected)
    _, secret = win32crypt.CryptUnprotectData(path.read_bytes(), None, None, None, 1)
    return Ed25519PrivateKey.from_private_bytes(secret)


def main():
    cli = argparse.ArgumentParser(description=__doc__)
    cli.add_argument("--public-header", type=Path, required=True)
    cli.add_argument("--initialize", action="store_true")
    cli.add_argument("--archive", type=Path)
    cli.add_argument("--version")
    cli.add_argument("--notes", type=Path, help="One printable ASCII line per note")
    cli.add_argument("--output", type=Path, help="The repository updates directory")
    cli.add_argument("--offline-zip", type=Path, help="Write the mobile offline update ZIP")
    args = cli.parse_args()
    key = signing_key(args.initialize)
    public = key.public_key().public_bytes(serialization.Encoding.Raw, serialization.PublicFormat.Raw).hex()
    header = (PUBLIC_HEADER % (REPO, public)).encode("ascii")
    if args.initialize:
        args.public_header.parent.mkdir(exist_ok=True, parents=True)
        if args.public_header.exists() and args.public_header.read_bytes() != header:
            raise ValueError("Existing source pins a different publisher key")
        args.public_header.write_bytes(header)
        print("Pinned publisher public key", public)
        return
    if args.public_header.read_bytes() != header:
        raise ValueError("This user does not own the pinned publisher key")
    if not args.archive or not args.notes or not args.version or not args.output:
        cli.error("signing needs --archive, --version, --notes and --output")
    if not re.fullmatch(r"(0|[1-9][0-9]{0,3})\.(0|[1-9][0-9]{0,3})\.(0|[1-9][0-9]{0,3})", args.version):
        raise ValueError("Invalid semantic version")
    notes = args.notes.read_text(encoding="ascii").splitlines()
    if not 1 <= len(notes) <= 8 or any(not re.fullmatch(r"[ -~]{1,112}", x) for x in notes):
        raise ValueError("Expected 1 to 8 single-line release notes")
    body = ["KEFIRJS-UPDATE-1", "version\t" + args.version, "base\t20.5.0-786"]
    body.extend("note\t" + note for note in notes)
    data = bytearray()
    with ZipFile(args.archive) as pack:
        manifest = json.loads(pack.read("release.json"))
        entries = {e["path"]:e for e in manifest["files"]}
        if manifest["version"] != args.version or manifest["horizon"] != "20.5.0":
            raise ValueError("Version or base mismatch")
        if manifest["id"] != "KEFIRIZ-786" or len(entries) != len(manifest["files"]):
            raise ValueError("Wrong edition or duplicate SD destinations")
        baseline = json.loads(Path(__file__).with_name("base-1.1.0.json").read_text("ascii"))
        changed = {name for name in baseline.keys() | entries.keys()
                   if baseline.get(name) != entries.get(name, {}).get("sha256")}
        if changed.difference(FILES):
            raise ValueError("The native client needs support for these additional changes: " +
                             ", ".join(sorted(changed.difference(FILES))))
        for name in FILES:
            file = pack.read("sd/" + name)
            size, digest = len(file), sha256(file).hexdigest()
            if entries[name]["sha256"] != digest or entries[name]["size"] != size:
                raise ValueError("Release file is damaged: " + name)
            if name == FILES[-1] and file.splitlines() != [f"KEFIRIZ {args.version} / Horizon edition 786".encode()]:
                raise ValueError("Wrong installed-release marker")
            data.extend(file)
            body.append(f"file\t{name}\t{size}\t{digest}")
        standalone = pack.read("sd/" + FILES[1])
    content = ("\n".join(body) + "\n").encode("ascii")
    envelope = b"KEFIRJS-SIGNED-1\n" + key.sign(content).hex().encode() + b"\n" + content
    folder = args.output / args.version
    folder.mkdir(parents=True, exist_ok=True)
    for name, value in [("update.bin", data), ("release.kfu", envelope)]:
        path = folder / name
        if path.exists() and path.read_bytes() != value:
            raise ValueError("Use a new version for changed release files: " + name)
        path.write_bytes(value)
    (args.output / "stable.kfu").write_bytes(envelope)
    if args.offline_zip:
        members = {FILES[1]: standalone, "config/kefirjs/offline/updates/stable.kfu": envelope,
                   f"config/kefirjs/offline/updates/{args.version}/update.bin": data}
        with ZipFile(args.offline_zip, "w") as pack:
            for name, value in members.items():
                entry = ZipInfo(name)
                entry.compress_type = ZIP_DEFLATED
                pack.writestr(entry, value)
    print(f"Signed {args.version} for {len(FILES)} files, {len(data):,} bytes")


if __name__ == "__main__":
    main()
