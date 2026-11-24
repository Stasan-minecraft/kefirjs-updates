"""Exercise native recovery against the repaired Atmosphere-edition SD tree."""
import argparse
import hashlib
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey

from test_update import HERE, MARKER, put


def test(sd, cc):
    with tempfile.TemporaryDirectory(prefix="kefirjs-startup-") as tmp:
        tmp = Path(tmp)
        src = tmp / "src"
        shutil.copytree(HERE, src)
        key = Ed25519PrivateKey.generate()
        (src / "publisher.h").write_text(
            '#define KF_REPO "test/example"\n'
            f'#define KF_PUBKEY_HEX "{key.public_key().public_bytes_raw().hex()}"\n')
        exe = tmp / ("engine.exe" if os.name == "nt" else "engine")
        subprocess.run([cc, "-std=gnu11", "-Wall", "-Wextra", "-Werror", "-O2",
                        "-DKF_ATMOSPHERE_EDITION", "-I", str(src), str(src / "engine.c"),
                        str(src / "tests/host.c"), str(src / "crypto/monocypher.c"),
                        str(src / "crypto/monocypher-ed25519.c"), "-lcrypto", "-lcurl",
                        "-o", str(exe)], check=True)
        old_marker = b"KEFIRIZ 2.0.0 / Atmosphere edition\n"
        new_marker = b"KEFIRIZ 2.0.2 / Atmosphere edition\n"
        roots = [Path("atmosphere/contents") / f"42000000004B465{x}" for x in ("0", "4")]
        modules = [(p / "exefs.nsp").as_posix() for p in roots]
        parked = [Path("config/kefiriz/profile-recovery") / name / "pending/boot2.flag"
                  for name in ("profiles", "transfer")]
        flags = [p / "flags/boot2.flag" for p in roots]
        data = [(sd / name).read_bytes() for name in modules]
        original = [b"\x00profile-selection\xff", b""]
        raw = dict(zip(modules, data))
        raw[MARKER] = new_marker
        receipt_dir = Path("config/kefirjs/atmosphere-ota/2.0.2")
        def sign(assets):
            body = ("KEFIRJS-UPDATE-1\nversion\t2.0.2\nbase\t20.5.0-ams-1.10.1\n"
                    "note\tRecover enabled services.\n")
            for name, value in assets.items():
                body += f"file\t{name}\t{len(value)}\t{hashlib.sha256(value).hexdigest()}\n"
            encoded = body.encode()
            return b"KEFIRJS-SIGNED-1\n" + key.sign(encoded).hex().encode() + b"\n" + encoded

        def startup(card, action, result=0, **extra):
            proc = subprocess.run([exe, action], cwd=card, capture_output=True, text=True,
                                  env={**os.environ, **extra})
            assert proc.returncode == result, (proc.stdout, proc.stderr)

        card = tmp / "reference"
        put(card, MARKER, old_marker)
        put(card, "atmosphere/package3", (sd / "atmosphere/package3").read_bytes())
        offline = Path("config/kefirjs/offline/updates/atmosphere/native")
        put(card, offline / "stable.kfu", sign(raw))
        put(card, offline / "2.0.2/update.bin", b"".join(raw.values()))
        for flag, pending, value in zip(flags, parked, original):
            (card / flag.parent).mkdir(parents=True)
            put(card, pending, value)

        startup(card, "recover")
        assert not any((card / f).exists() for f in flags)
        startup(card, "apply", 77, KF_TEST_INTERRUPT_BEFORE="2")
        assert [(card / p).read_bytes() for p in parked] == original
        startup(card, "recover")
        assert [(card / p).read_bytes() for p in flags] == original
        assert not any((card / p).exists() for p in parked)

        # The ledger has been finalized and power went out before optional
        # flag restoration. Only completed release receipts can retry it.
        for flag, pending in zip(flags, parked):
            (card / flag).rename(card / pending)
        cases = ["valid", "existing", "boot2.off", "custom", "program.off", "module.off",
                 "missing-dir", "directory-as-flag", "foreign-module", "wrong-marker",
                 "wrong-signature", "missing-journal", "omitted-service", "no-pending"]
        for case in cases:
            testcard = tmp / case
            shutil.copytree(card, testcard)
            if case in ("existing", "custom", "boot2.off"):
                name = {"existing": "boot2.flag", "custom": "my.flag", "boot2.off": "boot2.flag.off"}[case]
                put(testcard, flags[0].parent / name, b"existing-user-choice")
            elif case == "program.off":
                (testcard / (str(roots[0]) + ".off")).mkdir()
            elif case == "module.off":
                put(testcard, roots[0] / "exefs.nsp.off", b"disabled")
            elif case == "missing-dir":
                (testcard / flags[0].parent).rmdir()
            elif case == "directory-as-flag":
                (testcard / parked[0]).unlink()
                (testcard / parked[0]).mkdir()
            elif case == "foreign-module":
                put(testcard, modules[0], b"unknown module")
            elif case == "wrong-marker":
                put(testcard, MARKER, new_marker.replace(b"\n", b"\r\n"))
            elif case == "wrong-signature":
                put(testcard, receipt_dir / "stable.kfu", sign(raw)[:-1])
            elif case == "missing-journal":
                (testcard / receipt_dir / "originals.tsv").unlink()
            elif case == "omitted-service":
                put(testcard, receipt_dir / "stable.kfu", sign({MARKER: new_marker}))
            elif case == "no-pending":
                (testcard / parked[0]).unlink()
            for _ in range(2):
                startup(testcard, "recover")
            if case == "valid":
                assert (testcard / flags[0]).read_bytes() == original[0]
            elif case == "existing":
                assert (testcard / flags[0]).read_bytes() == b"existing-user-choice"
            else:
                assert not (testcard / flags[0]).exists(), case
            if case not in ("valid", "no-pending", "directory-as-flag"):
                assert (testcard / parked[0]).read_bytes() == original[0], case
            blocked = case in ("wrong-marker", "wrong-signature", "missing-journal", "omitted-service")
            assert (testcard / flags[1]).is_file() != blocked, case
            assert (testcard / parked[1]).is_file() == blocked, case
        print("Both services restored after interrupted apply; 14 restart, opt-out and ambiguity cases passed.")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sd", required=True, type=Path)
    parser.add_argument("--cc", default="gcc")
    args = parser.parse_args()
    test(args.sd, args.cc)
