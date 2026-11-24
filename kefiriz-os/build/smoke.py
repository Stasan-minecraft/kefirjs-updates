"""Run the shipped ARM64 desktop against its own SquashFS libraries in QEMU."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import tempfile


PROJECT = Path(__file__).resolve().parents[1]


def run(args):
    system = args.system.resolve()
    with tempfile.TemporaryDirectory(prefix="kefiriz-arm-smoke-") as temporary:
        work = Path(temporary)
        image = work / "image"
        subprocess.run(["unsquashfs", "-no-progress", "-d", image, system,
                        "usr/lib/kefiriz", "usr/lib/libc.so.6", "usr/lib/libm.so.6",
                        "usr/lib/ld-linux-aarch64.so.1"], check=True, stdout=subprocess.DEVNULL)
        (image / "lib").symlink_to("usr/lib")
        lib = image / "usr/lib"
        tools = lib / "kefiriz"
        crt = args.cross_lib
        cc = [args.cc]
        if args.clang_headers:
            cc += ["--target=aarch64-linux-gnu", "-isystem", str(args.clang_headers)]
        if args.lld:
            cc += ["-fuse-ld=" + str(args.lld)]
        executable = work / "frontend"
        subprocess.run([*cc, "-std=gnu11", "-O2", "-fPIE", "-pie", "-nostdlib",
                        "-Wl,--dynamic-linker,/lib/ld-linux-aarch64.so.1",
                        crt / "Scrt1.o", crt / "crti.o", PROJECT / "tests/core_host.c",
                        tools / "desktop.so", lib / "libc.so.6", lib / "libm.so.6", crt / "crtn.o",
                        "-o", executable], check=True)
        env = dict(os.environ, KEFIRIZ_STATE_DIR=str(work / "state"), KEFIRIZ_POWER_DIR=str(work / "power"),
                   KEFIRIZ_NETWORK_DIR=str(work / "wifi"), KEFIRIZ_SYSTEM_DIR=str(tools))
        for name in ("power", "wifi"):
            (work / name).mkdir()
        result = subprocess.run([args.qemu, "-L", image, "-E", "LD_LIBRARY_PATH=" + str(tools),
                                 executable], cwd=work, env=env,
                                input="save guide\npress 8\nframes 4\nsave next\nquit\n",
                                capture_output=True, text=True, timeout=60)
        if result.returncode:
            raise RuntimeError(result.stderr or result.stdout)
        guide = (work / "guide.ppm").read_bytes()
        other = (work / "next.ppm").read_bytes()
        header = b"P6\n1280 720\n255\n"
        if not guide.startswith(header) or len(guide) != len(header) + 1280 * 720 * 3 or guide == other:
            raise ValueError("The ARM core did not draw and navigate the 1280x720 first-run guide")
        if args.frames:
            args.frames.mkdir(parents=True, exist_ok=True)
            for name in ("guide.ppm", "next.ppm"):
                shutil.copyfile(work / name, args.frames / name)
        print("Verified shipped ARM64 desktop, dynamic loader, guide, gamepad input and video frames")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("system", type=Path)
    parser.add_argument("--qemu", default="qemu-aarch64")
    parser.add_argument("--cc", default="aarch64-linux-gnu-gcc")
    parser.add_argument("--cross-lib", type=Path, default=Path("/usr/aarch64-linux-gnu/lib"))
    parser.add_argument("--clang-headers", type=Path)
    parser.add_argument("--lld", type=Path)
    parser.add_argument("--frames", type=Path)
    run(parser.parse_args())
