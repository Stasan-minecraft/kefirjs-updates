"""Exercise in-place updates and interruptions through the Windows batch entry point."""
import json
from pathlib import Path
import shutil
import tempfile

from test_install import MAIN_PATH, NYX_PATH, STATE, fixture, legacy_install, put, run
from prepare_release import IMAGE_PATHS, write_manifest


def files(base, paths):
    return {p: (base / p).read_bytes() for p in paths}


def test():
    with tempfile.TemporaryDirectory(prefix="kefiriz-os-image-update-") as directory:
        base = Path(directory)
        old, new, compact = (base / name for name in ("old", "new", "compact"))
        fixture(old)
        shutil.copytree(old, new)
        for path in IMAGE_PATHS:
            put(new / "payload" / path, f"new {path}".encode())
        write_manifest(new, "0.2.0", old / "manifest.json")
        shutil.copytree(new, compact, ignore=shutil.ignore_patterns("payload"))
        old_files, new_files = (files(kit / "payload", IMAGE_PATHS) for kit in (old, new))
        new_pin = (new / "manifest.sha256").read_text().strip()
        card = base / "card"
        legacy_install(old, card)
        active = card / STATE / "active"
        receipt = active / "receipt.json"
        txn = active / ("image-update-" + new_pin)
        before = json.loads(receipt.read_bytes())
        menu = card / MAIN_PATH
        put(menu, menu.read_bytes().replace(b"bootwait=1", b"bootwait=5"))
        for p in ("lakka/storage/saves/game.sav", "lakka/storage/transfers/download.part",
                  "lakka/storage/.config/profile.ini", "Nintendo/example", "extra.txt"):
            put(card / p, p.encode())
        paths = [str(p.relative_to(card)) for p in card.rglob("*") if p.is_file()
                 and p not in [receipt, *(card / name for name in IMAGE_PATHS)]]
        original = files(card, paths)
        assert "0.1.0" in run(compact, "Diagnose", card)

        # Local edits and unknown ownership stop the update before any replacement.
        for path in (*IMAGE_PATHS, "lakka/KERNEL", "lakka/.kefiriz-dualboot-owner"):
            dest = card / path
            keep = dest.read_bytes()
            put(dest, bytes([keep[0] ^ 1]) + keep[1:])
            after_edit = files(card, [*paths, *IMAGE_PATHS])
            run(new, "Install", card, ok=False)
            assert files(card, [*paths, *IMAGE_PATHS]) == after_edit
            assert json.loads(receipt.read_bytes()) == before and not txn.exists()
            put(dest, keep)

        # Cancel a partial private copy before the first live replacement.
        put(txn / "new-0", b"interrupted private copy")
        run(compact, "RollbackUpdate", card)
        assert files(card, IMAGE_PATHS) == old_files and not txn.exists()

        # Resume a forward operation interrupted after SYSTEM was replaced.
        for index, path in enumerate(IMAGE_PATHS):
            put(txn / f"old-{index}", old_files[path])
            if index == 0:
                put(card / path, new_files[path])
            else:
                put(txn / f"new-{index}", new_files[path])
        (txn / "ready").mkdir()
        assert "incomplete" in run(new, "Status", card)
        run(new, "Install", card)
        assert files(card, IMAGE_PATHS) == new_files and files(card, paths) == original
        assert json.loads(receipt.read_bytes()) == dict(before, manifest=new_pin)
        run(new, "Install", card)
        report = run(compact, "Diagnose", card)
        assert "Verified 6 release files" in report and "No recognized" in report
        marker = card / "lakka/storage/kefiriz/session-0.2.0.txt"
        for board in ("icosa", "iowa", "hoag", "aula"):
            evidence = f"version=0.2.0\nboard=nintendo,{board}\n".encode()
            put(marker, evidence)
            report = run(compact, "Diagnose", card)
            assert f"previously displayed a frame on nintendo,{board}" in report
            assert marker.read_bytes() == evidence
        put(marker, b"version=0.2.0\nboard=test,pc\n")
        assert "unrecognized format" in run(compact, "Diagnose", card)

        # Validate both backups before replacing either file during rollback.
        original_backup = (txn / "old-1").read_bytes()
        put(txn / "old-1", b"damaged backup")
        run(compact, "RollbackUpdate", card, ok=False)
        assert files(card, IMAGE_PATHS) == new_files and not (txn / "rolling-back").exists()
        put(txn / "old-1", original_backup)

        # Resume rollback when its first backup was already moved into place.
        (txn / "rolling-back").mkdir()
        (txn / "old-0").replace(card / IMAGE_PATHS[0])
        assert "Rollback is incomplete" in run(new, "Status", card)
        run(new, "Install", card, ok=False)
        run(compact, "RollbackUpdate", card)
        assert files(card, IMAGE_PATHS) == old_files and files(card, paths) == original
        assert json.loads(receipt.read_bytes()) == before and not txn.exists()
        run(compact, "RollbackUpdate", card)
        assert "are installed" in run(old, "Status", card)

        # Complete the normal in-place update and then remove Linux using its
        # original menu snapshots, preserving all user data in the restore archive.
        run(new, "Install", card)
        assert files(card, IMAGE_PATHS) == new_files and files(card, paths) == original
        assert json.loads(receipt.read_bytes()) == dict(before, manifest=new_pin)
        run(compact, "Restore", card)
        assert b"bootwait=5" in menu.read_bytes() and b"[KEFIRIZ OS]" not in menu.read_bytes()
        assert b"homescreen=0" in (card / NYX_PATH).read_bytes()
        archived, = (card / STATE).glob("restored-*")
        for rel in original:
            p = Path(rel)
            if p.is_relative_to("lakka/storage"):
                assert (archived / "saved-lakka" / p.relative_to("lakka")).read_bytes() == original[rel]
        assert (card / "Nintendo/example").read_bytes() == b"Nintendo/example"
    print("Passed image upgrade, backup validation, interruption recovery, compact rollback and user data preservation.")


if __name__ == "__main__":
    test()
