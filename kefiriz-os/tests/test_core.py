"""Run the production shell through libretro callbacks in isolated Linux directories."""
import argparse
import binascii
import os
from pathlib import Path
import socket
import struct
import subprocess
import tempfile
import threading
import time
import zlib

ROOT = Path(__file__).resolve().parents[1]
BUTTONS = {"B": 0, "Y": 1, "Plus": 3, "Up": 4, "Down": 5, "Left": 6, "Right": 7, "A": 8, "X": 9}


def png(ppm, target):
    header = b"P6\n1280 720\n255\n"
    raw = ppm.read_bytes()
    assert raw.startswith(header) and len(raw) == len(header)+1280*720*3
    raw = raw[len(header):]
    def chunk(code, data):
        return struct.pack(">I", len(data)) + code + data + struct.pack(">I", binascii.crc32(code+data))
    rows = b"".join(b"\0"+raw[y*3840:(y+1)*3840] for y in range(720))
    target.write_bytes(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">2I5B",1280,720,8,2,0,0,0))
                       + chunk(b"IDAT", zlib.compress(rows, 9)) + chunk(b"IEND", b""))
    return raw


def run(tmp, previews, sanitize):
    state, sysdir, net, power = [tmp / x for x in ["state", "system", "net", "power"]]
    for p in (state/"actions", sysdir/"catalog", net, power): p.mkdir(parents=True)
    (net/"status").write_text("Ready")
    (net/"networks.tsv").write_text("wifi_1\tHome network\tpassword\tidle\n")
    sample = b"d4:infod4:name4:testee"
    (sysdir/"catalog/2048.torrent").write_bytes(sample)
    (state/"actions/Read status.sh").write_text(
        'printf x >> "$KEFIRIZ_STATE_DIR/launches"\n'
        'i=0; while [ ! -f "$KEFIRIZ_STATE_DIR/proceed" ] && [ "$i" -lt 300 ]; do '
        'sleep .01; i=$((i + 1)); done\nprintf "Action completed\\n"\n')
    exe = tmp / "host"
    flags = ["-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-no-pie"] if sanitize else []
    subprocess.run(["cc", "-std=c11", "-O1", "-g", *flags, ROOT/"tests/core_host.c",
                    ROOT/"native/core.c", ROOT/"native/draw.c", "-o", exe], check=True)
    env = dict(os.environ, KEFIRIZ_STATE_DIR=str(state), KEFIRIZ_SYSTEM_DIR=str(sysdir),
               KEFIRIZ_NETWORK_DIR=str(net), KEFIRIZ_POWER_DIR=str(power),
               ASAN_OPTIONS="detect_leaks=1:halt_on_error=1", UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1")
    server = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    server.bind(str(net/"control")); server.listen(4); server.settimeout(1.5)
    host = subprocess.Popen([exe], cwd=tmp, env=env, stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)
    def command(cmd):
        host.stdin.write(cmd+"\n"); host.stdin.flush()
        assert host.stdout.readline().strip()=="ok", f"libretro host failed after {cmd}"
    def press(*buttons):
        for b in buttons: command(f"press {BUTTONS[b]}")
    def tap(x, y): command(f"touch {x} {y}")
    def snapshot(name, publish=False):
        command("save "+name)
        folder = previews if publish and previews else tmp
        folder.mkdir(parents=True, exist_ok=True)
        return png(tmp/(name+".ppm"), folder/(name+".png"))
    def recv():
        peer,_=server.accept()
        with peer: return peer.recv(512)
    def read_pref():return (state/"profile-1.ini").read_text()
    def await_file(p, contents):
        for _ in range(150):
            if p.exists() and p.read_bytes()==contents:return
            time.sleep(.01)
        raise AssertionError(f"{p.name} did not reach expected state")
    try:
        assert host.stdout.readline().strip()=="ready"
        assert not (state/"guide-0.1.0").exists()
        press("A","A","A")
        assert (state/"guide-0.1.0").read_text().strip()=="seen"
        dark=snapshot("home", True)
        # All screenshot pixels come from the libretro video callback.
        assert dark[(240*1280+150)*3:(240*1280+150)*3+3] != dark[(15*1280+15)*3:(15*1280+15)*3+3]
        tap(752,554); settings=snapshot("settings",True);assert settings!=dark
        press("A","B"); assert "light=1" in read_pref()
        command("reinit"); light=snapshot("light",True); assert dark[:3]!=light[:3]
        # Readers sharing the state directory must only observe complete snapshots while a profile changes.
        stop=threading.Event()
        observed=[]
        def read_updates():
            while not stop.is_set():
                observed.append(read_pref().splitlines())
                stop.wait(.001)
        reader=threading.Thread(target=read_updates)
        tap(752,554);reader.start()
        try:
            for _ in range(18): press("A")
        finally:
            stop.set();reader.join()
        expected=["light","clock12"]+[f"tile{i}" for i in range(4)]+[f"dock{i}" for i in range(6)]
        assert observed and all([line.split("=",1)[0] for line in snapshot]==expected for snapshot in observed)
        command("reinit")
        # Reassign an empty tile to a local action. A second launch while it runs is suppressed.
        press("Right","A","Up","A")
        assert "tile1=app/Read status.sh" in read_pref()
        press("A");await_file(state/"launches",b"x")
        press("B","A"); assert (state/"launches").read_bytes()==b"x"
        (state/"proceed").touch();await_file(state/"action-output.txt",b"Action completed\n")
        command("frames 35"); press("B")
        command("reinit");press("Down","Plus","Up","A")
        assert "dock0=app/Read status.sh" in read_pref()
        command("reinit"); tap(376,554);await_file(state/"launches",b"xx")
        command("frames 35");press("B")
        # Each local profile has independent defaults. Switching back restores both custom slots.
        press("X","Down","A"); assert (state/"active-profile").read_text().strip()=="2"
        home2=snapshot("profile-two");assert home2[:3]==dark[:3]
        press("X","A"); assert (state/"active-profile").read_text().strip()=="1"
        # Join a mock Wi-Fi network through the visible keyboard. Secrets stay off disk.
        tap(752,554);tap(460,440);assert recv()==b"scan\n"
        tap(460,314);tap(225,278);tap(318,278);tap(410,278);tap(1011,519)
        assert recv()==b"connect\twifi_1\t123\n"
        press("B","B")
        # Exercise both Power choices without access to any machine or console power interface.
        tap(846,554);press("A","A"); assert not list(power.iterdir())
        press("A","Down","A"); assert (power/"reboot").is_file()
        command("reinit");tap(846,554);press("Down","A","Down","A");assert (power/"poweroff").is_file()
        # Normal Torrent window enqueues the owned fixture; the worker is outside this UI test.
        command("reinit");press("A","Down","A");assert (state/"queue/2048.torrent").read_bytes()==sample
        # A partial, oversized retry must preserve the complete request already in the queue.
        (sysdir/"catalog/2048.torrent").write_bytes(b"q"*(4*1024*1024+1))
        command("reinit");press("A","Down","A");assert (state/"queue/2048.torrent").read_bytes()==sample
        assert not list((state/"queue").glob(".request-*"))
        command("reinit");press("Y");shot,=list((state/"album").glob("*.ppm"))
        assert shot.read_bytes().startswith(b"P6\n640 360\n255\n")
        assert not list(state.glob(".prefs-*"))
        host.stdin.write("quit\n");host.stdin.flush()
        assert host.wait(timeout=4)==0
    finally:
        if host.poll() is None:host.kill();host.wait()
        server.close()
    print("Passed libretro guide, HOME, both themes, profiles, custom actions, dock persistence, Wi-Fi keyboard, power requests, queue and capture.")


if __name__ == "__main__":
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--previews", type=Path)
    parser.add_argument("--sanitize", action="store_true")
    opt=parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="kz-core-") as work:
        run(Path(work), opt.previews.resolve() if opt.previews else None, opt.sanitize)
