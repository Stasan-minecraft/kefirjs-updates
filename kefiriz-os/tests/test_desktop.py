"""Exercise the native X11 desktop on an isolated, non-16:9 Xvfb display."""
import argparse
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import tempfile
import time

ROOT = Path(__file__).resolve().parents[1]
NATIVE = ROOT / "native"


def wait_for(check, message):
    deadline = time.monotonic() + 5
    while time.monotonic() < deadline:
        result = check()
        if result:
            return result
        time.sleep(.04)
    raise AssertionError(message)


def pixel(frame, x, y):
    header = struct.unpack_from(">25I", frame)
    assert header[1] == 7 and header[4:6] == (960, 600)
    assert header[7] == 0 and header[11] == 32
    offset = header[0] + header[19] * 12 + y * header[12] + x * 4
    return frame[offset:offset + 3]


def board_markers(work, exe, env):
    """Run the unmodified native binary with synthetic device trees in a private chroot."""
    image = work / "marker-test-root"
    image.mkdir()
    shutil.copy2(exe, image / "desktop")
    for library in re.findall(r"(/[^\s]+) \(0x", subprocess.check_output(["ldd", exe], text=True)):
        local = image / library.lstrip("/")
        local.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(library, local)
    compatible = image / "proc/device-tree/compatible"
    compatible.parent.mkdir(parents=True)
    storage = image / "storage/kefiriz"
    storage.mkdir(parents=True)
    version = (ROOT / "runtime/VERSION").read_text().strip()
    report = storage / f"session-{version}.txt"
    def enter():
        os.chroot(image)
        os.chdir("/")
    for board, board_list in [
        (None, b"nvidia,tegra210\x00nintendo,nx\x00"),
        (None, b"nintendo,icosa-dev\x00"),
        (None, b"nintendo,icosa"),
        *((f"nintendo,{sku}", f"nvidia,board\x00nintendo,{sku}\x00nintendo,nx\x00".encode())
          for sku in ("icosa", "iowa", "hoag", "aula")),
    ]:
        compatible.write_bytes(board_list)
        host = subprocess.Popen(["/desktop"], env=dict(env, KEFIRIZ_STATE_DIR="/storage/kefiriz"),
                                preexec_fn=enter)
        try:
            # A mapped window alone precedes first presentation. Look at the server's actual pixels.
            wait_for(lambda: pixel(subprocess.check_output(["xwd", "-root", "-silent"], env=env),
                                   240, 42) != bytes(3), "Chroot desktop never painted")
            if board:
                wait_for(report.is_file, "Matching Switch board failed to create a session marker")
                assert report.read_text() == f"version={version}\nboard={board}\n"
                assert not list(storage.glob(".session-*"))
                report.unlink()
            else:
                assert not report.exists(), "Generic, malformed or unrelated board was accepted"
        finally:
            host.terminate()
            assert host.wait(timeout=4) == 0


def run(work, markers):
    exe = work / "desktop"
    sources = [NATIVE / name for name in ["desktop.c", "desktop_input.c", "core.c", "draw.c"]]
    subprocess.run(["cc", "-O2", "-Wall", "-Wextra", "-Werror", "-std=gnu11",
                    *sources, "-lXrender", "-lX11", "-lm", "-o", exe], check=True)
    subprocess.run(["cc", "-O2", "-Wall", "-Wextra", "-Werror", "-std=gnu11",
                    ROOT / "tests/desktop_pad.c", "-o", work / "pad-test"], check=True)
    subprocess.run([work / "pad-test"], check=True)
    blank = subprocess.run([exe], env=dict(os.environ, DISPLAY=""),
                           capture_output=True, timeout=4)
    assert blank.returncode != 0 and b"Cannot open" in blank.stderr

    state = work / "state"
    state.mkdir()
    reader, writer = os.pipe()
    # Use the Linux abstract socket. WSLg's filesystem X11 socket directory is read-only.
    server = subprocess.Popen(["Xvfb", "-displayfd", str(writer), "-screen", "0", "960x600x24",
                               "-nolisten", "tcp", "-nolisten", "unix"],
                              pass_fds=(writer,), stderr=subprocess.DEVNULL)
    os.close(writer)
    host = None
    try:
        with os.fdopen(reader) as number:
            screen = number.readline().strip()
        assert screen.isdigit(), "Xvfb failed to open the display"
        env = dict(os.environ, DISPLAY=f":{screen}", KEFIRIZ_STATE_DIR=str(state),
                   KEFIRIZ_SYSTEM_DIR=str(ROOT / "runtime"),
                   KEFIRIZ_NETWORK_DIR=str(work / "network"), KEFIRIZ_POWER_DIR=str(work / "power"))
        host = subprocess.Popen([exe], env=env)

        def xdo(*args, check=True):
            return subprocess.run(["xdotool", *map(str, args)], env=env, check=check,
                                  capture_output=True, text=True).stdout.strip()

        def frame():
            return subprocess.check_output(["xwd", "-root", "-silent"], env=env)

        window = wait_for(lambda: xdo("search", "--onlyvisible", "--name", "^KEFIRIZ OS$",
                                      check=False), "Desktop never mapped")
        wait_for(lambda: xdo("getwindowfocus") == window, "Desktop never received focus")
        xdo("key", "Escape")
        wait_for(lambda: list(state.glob("guide-*")), "Keyboard could not exit the guide")
        dark = frame()
        assert pixel(dark, 240, 29) == pixel(dark, 240, 570) == bytes(3)
        assert pixel(dark, 240, 42) != bytes(3)
        def quick_chord():
            xdo("keydown", "r", "keydown", "t", "keyup", "t", "keyup", "r")
        quick_chord()
        wait_for(lambda: pixel(frame(), 310, 228) != pixel(dark, 310, 228),
                 "Desktop R + ZR shortcut did not show Quick Settings")
        quick_chord()
        wait_for(lambda: pixel(frame(), 310, 228) == pixel(dark, 310, 228),
                 "Desktop R + ZR shortcut did not restore HOME")

        # Pass both transitions to X immediately. The host must keep a sub-frame tap.
        xdo("mousemove", 564, 445, "mousedown", 1, "mouseup", 1)
        time.sleep(.22)
        xdo("key", "Return")
        def light():
            prefs = state / "profile-1.ini"
            return prefs.is_file() and "light=1" in prefs.read_text()
        wait_for(light, "Scaled tap did not open Settings or A did not change the theme")
        xdo("key", "Escape")
        wait_for(lambda: pixel(frame(), 240, 42) != pixel(dark, 240, 42),
                 "New theme was not painted")

        # The dock helper resizes the active window. Hit the same dock button at
        # the new scale and offset after resizing to a square.
        xdo("windowsize", window, 600, 600)
        wait_for(lambda: pixel(frame(), 200, 100) == bytes(3), "Viewport did not resize")
        xdo("mousemove", 352, 391, "mousedown", 1, "mouseup", 1)
        time.sleep(.22)
        xdo("key", "Return")
        wait_for(lambda: not light(), "Resized pointer viewport missed Settings")
        xdo("key", "Escape", "y")
        shot = wait_for(lambda: next((state / "album").glob("*.ppm"), None),
                        "Keyboard capture did not create a photo")
        assert shot.read_bytes().startswith(b"P6\n640 360\n255\n")
        host.terminate()
        assert host.wait(timeout=4) == 0
        if markers:
            board_markers(work, exe, env)
    finally:
        if host is not None and host.poll() is None:
            host.kill()
            host.wait()
        server.terminate()
        server.wait(timeout=5)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--markers", action="store_true", help="test the board marker in a disposable chroot")
    opts = parser.parse_args()
    if opts.markers and os.geteuid() != 0:
        parser.error("--markers needs permission to chroot")
    with tempfile.TemporaryDirectory(prefix="kefiriz-desktop-") as folder:
        run(Path(folder), opts.markers)
    print("Passed native X11 presentation, keyboard and pointer, theme changes, scaled dock, capture, "
          "evdev input and clean shutdown.")
