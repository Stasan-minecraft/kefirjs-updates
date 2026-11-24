"""Run the normal Windows installer on disposable card directories."""
from pathlib import Path
import hashlib
import json
import shutil
import os
import subprocess
import sys
import tempfile

HERE = Path(__file__).resolve().parents[1]
sys.path.insert(0,str(HERE))
from prepare_release import ENTRY, HARDWARE, write_manifest


MAIN = (b"[config]\r\nautoboot=1\r\nautoboot_list=0\r\nbootwait=1\r\n"
        b"[KEFIRIZ]\r\npayload=bootloader/payloads/fusee.bin\r\nicon=bootloader/res/meridian.bmp\r\n"
        b"[Full Stock]\r\nfss0=atmosphere/package3\r\nstock=1\r\n{\xff original note}")
NYX = b"[config]\r\nhomescreen=0\r\nverification=1\r\n"
MAIN_PATH = Path("bootloader/hekate_ipl.ini")
NYX_PATH = Path("bootloader/nyx.ini")
STATE = Path("config/kefiriz-os/dual-boot")
RESTART_ROUTE = b"r2p_action=bootloader\r\n"


def put(file, data):
    file.parent.mkdir(parents=True, exist_ok=True)
    file.write_bytes(data)


def fixture(folder):
    folder.mkdir()
    for name in ["Install.ps1","Menu.ps1","CardFiles.ps1","Diagnose.ps1","INSTALL.bat"]:
        shutil.copyfile(HERE/name, folder/name)
    (folder/"boot-entry.ini").write_bytes(ENTRY.encode())
    for name in (*HARDWARE, "SYSTEM", "boot/kefiriz-logo.bmp", "boot/kefiriz-icon.bmp", "licenses/GPL-2.0.txt"):
        put(folder/"payload/lakka"/name, b"transaction fixture "+name.encode())
    write_manifest(folder, "0.1.0")


def card_files(card, nyx=True):
    put(card/MAIN_PATH, MAIN)
    if nyx:
        put(card/NYX_PATH, NYX)
    for path in ("bootloader/sys/nyx.bin", "atmosphere/package3", "bootloader/payloads/fusee.bin"):
        put(card/path,b"configured fixture")
    for name in ("bpmpfw.bin","bpmpfw_b01.bin","mtc_tbl_b01.bin","sc7entry.bin","sc7exit.bin","sc7exit_b01.bin"):
        put(card/"bootloader/sys/l4t"/name, b"configured hardware support")


def run(kit, act, card=None, ok=True):
    env = dict(os.environ, KEFIRIZ_TEST_CARD=str(card or ""))
    argument = ' "%KEFIRIZ_TEST_CARD%"' if card else ""
    proc=subprocess.run(f'cmd.exe /d /v:off /c INSTALL.bat {act}{argument}', cwd=kit,
                        env=env, capture_output=True, timeout=90)
    msg=(proc.stdout+proc.stderr).decode(errors="replace")
    assert (proc.returncode==0) == ok, (act,proc.returncode,msg)
    return msg


def legacy_install(kit, card):
    card_files(card)
    run(kit, "Install", card)
    active = card / STATE / "active"
    # Reproduce the launch menu and rollback snapshot a 0.1.0
    # installer produced, including the old digest in its existing receipt.
    installed = (active / "0.new").read_bytes()
    assert installed == (card / MAIN_PATH).read_bytes()
    assert installed.count(RESTART_ROUTE) == 1
    original_entry = ENTRY.replace("\n", "\r\n").encode()
    assert RESTART_ROUTE not in original_entry
    old = installed.replace(RESTART_ROUTE, b"")
    assert original_entry in old
    put(active / "0.new", old)
    put(card / MAIN_PATH, old)
    record = active / "receipt.json"
    receipt = json.loads(record.read_text("utf-8"))
    receipt["files"][0]["after"] = hashlib.sha256(old).hexdigest()
    assert receipt["manifest"] == (kit / "manifest.sha256").read_text("ascii").strip()
    put(record, json.dumps(receipt, separators=(",", ":")).encode("ascii"))
    return installed


def diagnostic_cases(kit, card):
    assert not (kit / "payload").exists()
    def snapshot():
        return {p.relative_to(card): (p.read_bytes() if p.is_file() else None)
                for p in card.rglob("*")}

    def diagnose(ok):
        before = snapshot()
        message = run(kit, "Diagnose", card, ok=ok)
        assert snapshot() == before
        return message

    original = (card / MAIN_PATH).read_bytes()
    assert RESTART_ROUTE not in original
    message = diagnose(True)
    assert "Verified 6 release files" in message and "self" in message
    for relative in ("lakka/KERNEL", "bootloader/sys/l4t/sc7exit_b01.bin"):
        path = card / relative
        content = path.read_bytes()
        if relative == "lakka/KERNEL":
            # A same-sized substitution must be caught by the card checksum.
            put(path, bytes([content[0] ^ 1]) + content[1:])
        else:
            path.unlink()
        assert relative in diagnose(False)
        put(path, content)
    put(card / MAIN_PATH, original.replace(b"boot_prefixes=lakka/boot/", b"boot_prefixes=elsewhere/"))
    assert "boot_prefixes" in diagnose(False)
    for key in (b"payload", b"fss0", b"pkg3"):
        put(card / MAIN_PATH, original.replace(b"l4t=1\r\n", b"l4t=1\r\n" + key + b"=other.bin\r\n"))
        assert "Conflicting Hekate boot option" in diagnose(False)
    put(card / MAIN_PATH, original)


def boot_menu_cases(base, kit):
    small_kit = base / "menu-only kit"
    shutil.copytree(kit, small_kit, ignore=shutil.ignore_patterns("payload"))
    absent = base / "card not mounted"
    run(small_kit, "BootMenu", absent, ok=False)
    assert not absent.exists()
    unmanaged = base / "unmanaged card"
    card_files(unmanaged)
    run(small_kit, "BootMenu", unmanaged, ok=False)
    assert not (unmanaged / STATE).exists()
    assert (unmanaged / MAIN_PATH).read_bytes() == MAIN

    legacy = base / "legacy card"
    installed = legacy_install(kit, legacy)
    diagnostic_cases(small_kit, legacy)
    active = legacy / STATE / "active"
    originals = {p: (active / p).read_bytes()
                 for p in ("0.old", "1.old", "0.new", "1.new", "receipt.json")}
    old = (legacy / MAIN_PATH).read_bytes()
    # User changes outside the managed entry must survive migration and restore.
    put(legacy / MAIN_PATH, old.replace(b"bootwait=1", b"bootwait=4"))
    nyx = (legacy / NYX_PATH).read_bytes().replace(b"verification=1", b"verification=2")
    put(legacy / NYX_PATH, nyx)
    saved = legacy / "lakka/storage/keep.bin"
    put(saved, b"saved data")
    for _ in range(2):
        run(small_kit, "BootMenu", legacy)
        assert (legacy / MAIN_PATH).read_bytes() == installed.replace(b"bootwait=1", b"bootwait=4")
        assert (legacy / NYX_PATH).read_bytes() == nyx
        assert saved.read_bytes() == b"saved data"
        for name, content in originals.items():
            assert (active / name).read_bytes() == content, name
    run(small_kit, "Restore", legacy)
    assert (legacy / MAIN_PATH).read_bytes() == MAIN.replace(b"bootwait=1", b"bootwait=4")
    assert (legacy / NYX_PATH).read_bytes() == NYX.replace(b"verification=1", b"verification=2")
    assert next((legacy / STATE).glob("restored-*/saved-lakka/storage/keep.bin")).read_bytes() == b"saved data"

    untouched = base / "unpatched legacy restore"
    legacy_install(kit, untouched)
    unchanged = (untouched / MAIN_PATH).read_bytes().replace(b"bootwait=1", b"bootwait=5")
    put(untouched / MAIN_PATH, unchanged)
    ownership = untouched / "lakka/.kefiriz-dualboot-owner"
    owner_id = ownership.read_bytes()
    put(ownership, b"0" * 32)
    run(small_kit, "BootMenu", untouched, ok=False)
    assert (untouched / MAIN_PATH).read_bytes() == unchanged
    put(ownership, owner_id)
    run(small_kit, "Restore", untouched)
    assert (untouched / MAIN_PATH).read_bytes() == MAIN.replace(b"bootwait=1", b"bootwait=5")

    legacy_full = base / "full reinstall"
    desired = legacy_install(kit, legacy_full)
    run(kit, "Install", legacy_full)
    assert (legacy_full / MAIN_PATH).read_bytes() == desired
    run(kit, "Install", legacy_full)
    assert (legacy_full / MAIN_PATH).read_bytes() == desired
    put(legacy_full / MAIN_PATH, desired.replace(RESTART_ROUTE, b"r2p_action=self\r\n"))
    custom = (legacy_full / MAIN_PATH).read_bytes()
    for command in ("BootMenu", "Install", "Restore"):
        run(kit, command, legacy_full, ok=False)
        assert (legacy_full / MAIN_PATH).read_bytes() == custom

    interrupted = base / "incomplete legacy install"
    desired = legacy_install(kit, interrupted)
    entry_file = interrupted / MAIN_PATH
    active = interrupted / STATE / "active"
    (active / "installed").rmdir()
    put(entry_file, MAIN)
    run(small_kit, "BootMenu", interrupted, ok=False)
    assert entry_file.read_bytes() == MAIN and not (active / "installed").exists()
    run(kit, "Install", interrupted)
    assert entry_file.read_bytes() == desired and (active / "installed").is_dir()
    (active / "restoring").mkdir()
    run(small_kit, "BootMenu", interrupted, ok=False)
    assert entry_file.read_bytes() == desired and (active / "restoring").is_dir()


def test():
    with tempfile.TemporaryDirectory(prefix="kefiriz-dualboot-qa-") as tmp:
        base=Path(tmp); kit=base/"installer"; card=base/"card"
        fixture(kit); card_files(card)
        run(kit,"Verify")
        run(kit,"Install",card)
        installed=(card/MAIN_PATH).read_bytes()
        assert b"autoboot=0" in installed and b"[KEFIRIZ Meridian]\r\npayload=bootloader/payloads/fusee.bin" in installed
        assert b"[KEFIRIZ OS]\r\nl4t=1" in installed and installed.split(b"[Full Stock]")[1] == MAIN.split(b"[Full Stock]")[1]
        assert installed.count(b"bootargs_extra=nosplash\r\n") == 1
        assert installed.count(RESTART_ROUTE) == 1
        assert b"homescreen=2" in (card/NYX_PATH).read_bytes()
        assert (card/"lakka/storage").is_dir()
        assert "are installed" in run(kit,"Status",card)
        run(kit,"Install",card)
        active=card/STATE/"active"
        (active/"installed").rmdir()
        (card/"lakka").rename(active/"staging/lakka")
        put(active/"staging/lakka/KERNEL",b"interrupted copy")
        put(card/MAIN_PATH,MAIN);put(card/NYX_PATH,NYX)
        run(kit,"Install",card)
        assert (card/"lakka/KERNEL").read_bytes()==(kit/"payload/lakka/KERNEL").read_bytes()
        assert (card/MAIN_PATH).read_bytes()==installed
        put(card/"lakka/storage/save.dat",b"console save")
        put(card/MAIN_PATH,installed.replace(b"bootwait=1",b"bootwait=4"))
        put(card/NYX_PATH,(card/NYX_PATH).read_bytes().replace(b"verification=1",b"verification=2"))
        run(kit,"Restore",card)
        assert (card/MAIN_PATH).read_bytes()==MAIN.replace(b"bootwait=1",b"bootwait=4")
        assert (card/NYX_PATH).read_bytes()==NYX.replace(b"verification=1",b"verification=2")
        assert not (card/"lakka").exists()
        archived,=(card/STATE).glob("restored-*")
        assert (archived/"saved-lakka/storage/save.dat").read_bytes()==b"console save"
        run(kit,"Restore",card)
        run(kit,"Install",card)
        # Full Stock remains selected when removing the intervening Linux tile.
        working=(card/MAIN_PATH).read_bytes()
        put(card/MAIN_PATH,working.replace(b"autoboot=0",b"autoboot=3"))
        run(kit,"Restore",card)
        assert b"autoboot=2" in (card/MAIN_PATH).read_bytes()

        conflict=base/"foreign"; card_files(conflict);put(conflict/"lakka/storage/save",b"independent save")
        run(kit,"Install",conflict,False)
        assert (conflict/MAIN_PATH).read_bytes()==MAIN and (conflict/"lakka/storage/save").read_bytes()==b"independent save"
        incomplete=base/"missing-board-firmware";card_files(incomplete)
        (incomplete/"bootloader/sys/l4t/sc7exit_b01.bin").unlink()
        assert "sc7exit_b01.bin" in run(kit,"Install",incomplete,False)
        assert not (incomplete/STATE).exists() and (incomplete/MAIN_PATH).read_bytes()==MAIN
        empty=base/"missing-nyx";card_files(empty,False)
        run(kit,"Install",empty)
        put(empty/"lakka/KERNEL",b"later independent kernel")
        run(kit,"Install",empty,False)
        assert (empty/"lakka/KERNEL").read_bytes()==b"later independent kernel"
        (empty/"atmosphere/package3").unlink()
        run(kit,"Restore",empty)
        assert not (empty/NYX_PATH).exists() and (empty/MAIN_PATH).read_bytes()==MAIN
        boot_menu_cases(base, kit)
        damaged=base/"damaged";card_files(damaged);put(kit/"payload/lakka/SYSTEM",b"damaged")
        run(kit,"Install",damaged,False)
        assert not (damaged/STATE).exists() and (damaged/MAIN_PATH).read_bytes()==MAIN
    print("Passed dual boot, read-only diagnostics, boot-menu migration, rollback, interrupted "
          "install and preservation of user settings and saves.")


if __name__=="__main__":
    test()
