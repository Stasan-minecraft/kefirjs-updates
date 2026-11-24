"""Launch the production AArch64 desktop and drive its X11 window under QEMU."""
import argparse
from contextlib import contextmanager
import os
from pathlib import Path
import select
import struct
import subprocess
import tempfile
import time


@contextmanager
def child(command, **options):
    proc = subprocess.Popen(command, **options)
    try:
        yield proc
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.wait()


def capture(window, env):
    """Read the actual Xvfb RGB888 window, excluding XWD's volatile header."""
    raw = subprocess.check_output(["xwd", "-silent", "-id", window], env=env, timeout=10)
    fields = struct.unpack_from(">25I", raw)
    header, version, image_format, depth, width, height = fields[:6]
    if (version, image_format, depth, fields[7], fields[11], fields[14:17]) != (
            7, 2, 24, 0, 32, (0xff0000, 0xff00, 0xff)):
        raise ValueError("Unsupported XWD visual")
    start = header + fields[19] * 12
    pixels = raw[start:]
    if len(pixels) != width * height * 4:
        raise ValueError("Incomplete X11 frame")
    return width, height, pixels


def wait_frame(proc, window, env, size=(1280, 720), previous=None):
    end = time.monotonic() + 15
    while time.monotonic() < end:
        if proc.poll() is not None:
            raise RuntimeError(f"Desktop exited with status {proc.returncode}")
        w, h, pixels = capture(window, env)
        # Sample the body only. Exclude the clock and the X11 window name.
        sample = pixels[(w * 4 * h // 5):(w * 4 * h * 4 // 5):100]
        if ((w, h) == size and len(set(sample)) > 20 and
                (previous is None or sum(a != b for a, b in zip(sample, previous)) > 50)):
            return (w, h, pixels), sample
        time.sleep(0.08)
    raise RuntimeError("The desktop did not render the expected screen after navigation or resizing")


def save(frame, path):
    w, h, bgra = frame
    rgb = bytearray(w * h * 3)
    rgb[0::3], rgb[1::3], rgb[2::3] = bgra[2::4], bgra[1::4], bgra[0::4]
    path.write_bytes(f"P6\n{w} {h}\n255\n".encode() + rgb)


def exercise(args, image, work, env):
    tools = image / "usr/lib/kefiriz"
    version = (tools / "VERSION").read_text().strip()
    state = work / "state"
    for name in ("power", "wifi", "state"):
        (work / name).mkdir()
    env.update(KEFIRIZ_STATE_DIR=str(state), KEFIRIZ_POWER_DIR=str(work / "power"),
               KEFIRIZ_NETWORK_DIR=str(work / "wifi"), KEFIRIZ_SYSTEM_DIR=str(tools))

    def xdo(*command):
        return subprocess.check_output(["xdotool", *command], env=env, timeout=15).decode().strip()

    def launch():
        return child([args.qemu, "-L", image, tools / "desktop"], cwd=work, env=env)

    def window(proc):
        end = time.monotonic() + 15
        while time.monotonic() < end and proc.poll() is None:
            ids = subprocess.run(["xdotool", "search", "--onlyvisible", "--name", "^KEFIRIZ OS$"],
                                 env=env, capture_output=True, text=True, timeout=5)
            if ids.returncode == 0:
                found = ids.stdout.splitlines()
                if len(found) == 1:
                    xdo("windowfocus", found[0])
                    return found[0]
            time.sleep(0.1)
        raise RuntimeError(f"KEFIRIZ window unavailable, desktop exit status {proc.poll()}")

    with launch() as proc:
        win = window(proc)
        guide, prior = wait_frame(proc, win, env)
        for _ in range(3):
            xdo("key", "Return")
            home, prior = wait_frame(proc, win, env, previous=prior)
        if (state / ("guide-" + version)).read_text() != "seen\n":
            raise ValueError("The first-run guide did not persist completion")
        xdo("keydown", "r", "keydown", "t", "keyup", "t", "keyup", "r")
        quick, prior = wait_frame(proc, win, env, previous=prior)
        xdo("key", "Escape")
        wait_frame(proc, win, env, previous=prior)
        xdo("windowsize", win, "960", "540")
        resized, _ = wait_frame(proc, win, env, size=(960, 540))
        if args.frames:
            args.frames.mkdir(parents=True, exist_ok=True)
            for name, frame in (("guide", guide), ("home", home), ("quick", quick), ("resized", resized)):
                save(frame, args.frames / (name + ".ppm"))
    with launch() as proc:
        reopened, _ = wait_frame(proc, window(proc), env)
        # Ignore header time. HOME must be identical across a normal restart.
        middle = slice(1280 * 4 * 180, 1280 * 4 * 500)
        if reopened[2][middle] != home[2][middle]:
            raise ValueError("Desktop restart did not restore HOME")


def run(args):
    with tempfile.TemporaryDirectory(prefix="kefiriz-arm-smoke-") as temporary:
        work = Path(temporary)
        image = work / "image"
        units = "usr/lib/systemd/system/"
        assets = ["usr/lib/kefiriz", "usr/bin/dock-hotplug", units + "default.target",
                  units + "graphical.target", units + "graphical.target.wants/kefiriz-desktop.service",
                  units + "kefiriz-desktop.service", units + "graphical.target.wants/xorg.service",
                  units + "multi-user.target.wants/joycond.service", units + "xorg.service", units + "joycond.service"]
        libs = ("ld-linux-aarch64.so*", "libc.so*", "libm.so*", "libXrender.so*", "libX11.so*",
                "libxcb.so*", "libXau.so*")
        subprocess.run(["unsquashfs", "-no-progress", "-d", image, args.system.resolve(),
                        *assets, *("usr/lib/" + lib for lib in libs)], check=True, stdout=subprocess.DEVNULL)
        (image / "lib").symlink_to("usr/lib")
        unitdir = image / units
        unit = unitdir / "kefiriz-desktop.service"
        if ((unitdir / "default.target").readlink() != Path("graphical.target") or
                (unitdir / "graphical.target.wants/kefiriz-desktop.service").resolve() != unit or
                not (unitdir / "graphical.target.wants/xorg.service").is_file() or
                not (unitdir / "multi-user.target.wants/joycond.service").is_file() or
                "ExecStart=/usr/lib/kefiriz/desktop\n" not in unit.read_text() or
                "ExecStartPre=/usr/bin/dock-hotplug\n" not in unit.read_text()):
            raise ValueError("The packaged graphical boot target does not start the native desktop")
        # Use Linux's local abstract socket. WSLg's filesystem socket directory
        # can be mounted read-only for the build user.
        with child(["Xvfb", "-displayfd", "1", "-screen", "0", "1280x720x24",
                    "-nolisten", "tcp", "-nolisten", "unix"],
                   stdout=subprocess.PIPE) as server:
            if not select.select([server.stdout], [], [], 12)[0]:
                raise RuntimeError("Xvfb did not start")
            number = server.stdout.readline().strip().decode()
            if not number.isdigit():
                raise RuntimeError("Xvfb did not report its display")
            exercise(args, image, work, dict(os.environ, DISPLAY=":" + number))
        print("Verified the desktop boot unit, ARM64 X11 session, first-run tour, quick settings, "
              "resizing and HOME persistence")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("system", type=Path)
    parser.add_argument("--qemu", default="qemu-aarch64")
    parser.add_argument("--frames", type=Path)
    run(parser.parse_args())
