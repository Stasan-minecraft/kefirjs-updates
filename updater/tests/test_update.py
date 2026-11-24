"""Exercise the native C engine on disposable cards, optionally using a production feed."""
import argparse
import hashlib
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import time
import zipfile

HERE = Path(__file__).resolve().parent.parent
MARKER = "config/kefiriz/release.txt"
SYSTEM = "switch/KEFIRIZ-System/KEFIRIZ-System.nro"
UPDATER = "switch/KEFIRJS-Updater/KEFIRJS-Updater.nro"
RETIRED = "config/kefir-updater/.kefir-updater"
CUSTOM = "config/kefir-updater/custom_packs.json"
FILES = ("hbmenu.nro", UPDATER, SYSTEM, MARKER)


def put(root, path, data):
    p = root / path
    p.parent.mkdir(parents=True, exist_ok=True)
    p.write_bytes(data)


def digest(data):
    return hashlib.sha256(data).hexdigest()


def run_test(args):
    with tempfile.TemporaryDirectory(prefix="kefirjs-engine-") as td:
        tmp = Path(td)
        src = tmp / "src"
        shutil.copytree(HERE, src, ignore=shutil.ignore_patterns("__pycache__"))
        private = None
        if not args.feed:
            from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey
            private = Ed25519PrivateKey.generate()
            key = private.public_key().public_bytes_raw().hex()
            (src / "publisher.h").write_text(f'#define KF_REPO "test/example"\n#define KF_PUBKEY_HEX "{key}"\n')
        exe = tmp / ("engine.exe" if os.name == "nt" else "engine")
        subprocess.run([args.cc, "-std=gnu11", "-Wall", "-Wextra", "-Werror", "-O2", "-I", str(src),
                        str(src / "engine.c"), str(src / "tests/host.c"), str(src / "crypto/monocypher.c"),
                        str(src / "crypto/monocypher-ed25519.c"), "-lcurl", "-lcrypto", "-o", str(exe)], check=True)
        if args.feed:
            feed = args.feed.read_bytes()
            release_ver = feed.split(b"\nversion\t", 1)[1].split(b"\n", 1)[0].decode("ascii")
            assert release_ver == "1.2.0", "This integration scenario starts at version 1.1.0."
            blob = (args.feed.parent / release_ver / "update.bin").read_bytes()
            records, offset = {}, 0
            for line in feed.splitlines():
                if line.startswith(b"file\t"):
                    _, name, length, sha = line.split(b"\t")
                    size = int(length)
                    payload = blob[offset:offset+size]
                    assert len(payload) == size and digest(payload).encode() == sha
                    records[name.decode()] = payload
                    offset += size
            assert offset == len(blob) and tuple(records) == FILES
        else:
            release_ver = "1.2.0"
            records = {p: ("signed test content for " + p).encode() for p in FILES}
            records[MARKER] = b"KEFIRIZ 1.2.0 / Horizon edition 786\n"
            blob = b"".join(records.values())
            body = "KEFIRJS-UPDATE-1\nversion\t1.2.0\nbase\t20.5.0-786\nnote\tFixture changelog.\n"
            body += "".join(f"file\t{p}\t{len(raw)}\t{digest(raw)}\n" for p, raw in records.items())
            def sign(b):
                return b"KEFIRJS-SIGNED-1\n" + private.sign(b).hex().encode() + b"\n" + b
            feed = sign(body.encode())
        with zipfile.ZipFile(args.old_zip) as archive:
            base = {p: archive.read("sd/" + p) for p in ("atmosphere/package3", "hbmenu.nro", SYSTEM, MARKER)}
        assert digest(base["atmosphere/package3"]) == "1fca4f0a1d2a214d7d92c0574827066e59bbfc4e230750a800654534f1820ac2"
        base[RETIRED] = b""
        base[CUSTOM] = b"personal catalog choices"
        base["switch/KEFIRIZ-Downloads/sentinel"] = b"my games"
        feedpath = "config/kefirjs/offline/updates/native/stable.kfu"
        blobpath = f"config/kefirjs/offline/updates/native/{release_ver}/update.bin"

        def card(name):
            root = tmp / name
            for p, b in base.items():
                put(root, p, b)
            put(root, feedpath, feed)
            put(root, blobpath, blob)
            return root

        def run(root, cmd, expect=0, contain=None, **env):
            proc = subprocess.run([exe, cmd], cwd=root, env=dict(os.environ, **env), capture_output=True, text=True)
            assert proc.returncode == expect, (cmd, proc.returncode, proc.stdout, proc.stderr)
            if contain:
                assert contain in proc.stdout, proc.stdout
            return proc.stdout

        def unchanged(root, paths=base):
            for p in paths:
                assert (root / p).read_bytes() == base[p], p
            assert not (root / UPDATER).exists()

        def complete(root):
            for p, b in records.items():
                assert (root / p).read_bytes() == b
            assert not (root / RETIRED).exists()
            for p in ("atmosphere/package3", CUSTOM, "switch/KEFIRIZ-Downloads/sentinel"):
                assert (root / p).read_bytes() == base[p]
            ledger = root / "config/kefirjs/ota" / release_ver
            assert (ledger / "stable.kfu").read_bytes() == feed
            lines = (ledger / "originals.tsv").read_text().splitlines()
            assert lines[0] == "KEFIRJS-JOURNAL-1"
            for idx, line in enumerate(lines[1:]):
                n, p, old, new = line.split("\t")
                assert int(n) == idx
                if old != "-" and old != new:
                    assert digest((ledger / f"old-{idx:02d}.bin").read_bytes()) == old
                assert (root / p).exists() == (new != "-")
            run(root, "check", contain="The SD package is")

        root = card("ordinary")
        print(run(root, "check", contain="available"), end="")
        print(run(root, "apply", contain="installed"), end="")
        complete(root)
        for variable in ("KF_TEST_HOLD_PREPARE", "KF_TEST_HOLD_COMMIT"):
            root = card(variable)
            held = subprocess.Popen([exe, "apply"], cwd=root, env={**os.environ, variable: "1"})
            try:
                deadline = time.monotonic() + 10
                while not (root / "host-lock-ready").exists():
                    assert held.poll() is None and time.monotonic() < deadline
                    time.sleep(.025)
                # A second invocation must not discard a live preparation or
                # recover into an in-progress commit from the first invocation.
                run(root, "apply", 1, "lock the card")
                unchanged(root)
            finally:
                held.kill()
                held.wait()
            if variable == "KF_TEST_HOLD_COMMIT":
                run(root, "recover", contain="Recovered")
            else:
                run(root, "apply", contain="installed")
            complete(root)
        for point in range(5):  # hbmenu, updater, System, obsolete marker, release marker
            root = card(f"interrupted-{point}")
            run(root, "apply", 77, KF_TEST_INTERRUPT_BEFORE=str(point))
            run(root, "recover", contain="Recovered")
            complete(root)
        root = card("gap")
        run(root, "apply", 77, KF_TEST_INTERRUPT_BEFORE="0")
        (root / "hbmenu.nro").rename(root / "config/kefirjs/ota/active/swap-00.bin")
        run(root, "recover", contain="Recovered")
        complete(root)
        root = card("corrupt-stage")
        run(root, "apply", 77, KF_TEST_INTERRUPT_BEFORE="0")
        put(root, "config/kefirjs/ota/active/new-00.bin", b"changed")
        run(root, "recover", 1, "Recovery needs")
        unchanged(root)
        root = card("broken-backup")
        run(root, "apply", 77, KF_TEST_INTERRUPT_BEFORE="0")
        put(root, "config/kefirjs/ota/active/old-00.bin", b"changed")
        run(root, "recover", 1, "Recovery needs")
        unchanged(root)
        root = card("invalid")
        run(root, "apply", 1, "matching KEFIRIZ", KF_TEST_WRONG_HOS="1")
        unchanged(root)
        run(root, "apply", 1, "space", KF_TEST_FULL="1")
        unchanged(root)
        put(root, feedpath, feed.replace(b"786", b"785", 1))
        run(root, "apply", 1, "signature")
        unchanged(root)
        put(root, feedpath, feed)
        put(root, blobpath, blob[:-1] + bytes([blob[-1] ^ 0xff]))
        run(root, "apply", 1, "verify")
        unchanged(root)
        if private:
            put(root, feedpath, sign(body.replace("file\thbmenu.nro", "file\t../wrong").encode()))
            run(root, "apply", 1, "format")
            unchanged(root)
        print("Two-process exclusion, crash release, five interruptions, FAT gap, integrity and base checks passed.")


if __name__ == "__main__":
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--old-zip", required=True, type=Path)
    ap.add_argument("--feed", type=Path, help="signed stable.kfu beside <version>/update.bin")
    ap.add_argument("--cc", default="gcc")
    run_test(ap.parse_args())
