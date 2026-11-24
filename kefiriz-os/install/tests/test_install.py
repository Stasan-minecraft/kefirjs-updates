"""Run the normal Windows installer on disposable card directories."""
from pathlib import Path
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


def put(file, data):
    file.parent.mkdir(parents=True, exist_ok=True)
    file.write_bytes(data)


def fixture(folder):
    folder.mkdir()
    for name in ["Install.ps1","Menu.ps1","CardFiles.ps1","INSTALL.bat"]:
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
        damaged=base/"damaged";card_files(damaged);put(kit/"payload/lakka/SYSTEM",b"damaged")
        run(kit,"Install",damaged,False)
        assert not (damaged/STATE).exists() and (damaged/MAIN_PATH).read_bytes()==MAIN
    print("Passed Windows dual boot, config and save preservation, interrupted transaction, reinstall, rollback and conflicts.")


if __name__=="__main__":
    test()
