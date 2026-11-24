"""Build the KEFIRIZ Switch Linux FAT32 payload in a new host directory.

Run on Linux using squashfs-tools and an AArch64 GNU C compiler. The optional
Clang flags support distributions providing cross headers without cross GCC.
"""
import argparse
import hashlib
import os
from pathlib import Path, PurePosixPath
import re
import shutil
import struct
import subprocess
import tarfile
import tempfile
import urllib.request

PROJECT = Path(__file__).resolve().parents[1]
NAME = "Lakka-Switch.aarch64-5.0-devel-20240122151037-5f29696"
URL = f"https://download.switchroot.org/lakka/{NAME}.tar"
SHA256 = "e34a6e2a8b19cb95e1b1ced10c3ef32a1c4e6a10748a4926e2a2cd6041e6daaf"
EPOCH = 1705932829


def digest(path, algorithm="sha256"):
    h = hashlib.new(algorithm)
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1 << 20), b""):
            h.update(block)
    return h.hexdigest()


def acquire(path):
    if not path.exists():
        path.parent.mkdir(parents=True, exist_ok=True)
        with tempfile.NamedTemporaryFile(dir=path.parent, delete=False) as temp:
            pending = Path(temp.name)
        try:
            with urllib.request.urlopen(URL, timeout=90) as response, pending.open("wb") as target:
                shutil.copyfileobj(response, target, 1 << 20)
            if digest(pending) != SHA256:
                raise ValueError("Downloaded BSP does not match pinned SHA-256")
            pending.replace(path)
        finally:
            pending.unlink(missing_ok=True)
    elif digest(path) != SHA256:
        raise ValueError("Unexpected BSP archive")


def extract_card_files(archive, stage):
    with tarfile.open(archive, "r:") as source:
        for member in source:
            parts = PurePosixPath(member.name).parts
            if (not parts or parts[0] != NAME or ".." in parts or
                    not (member.isdir() or member.isfile())):
                raise ValueError("Invalid BSP member: " + member.name)
            if len(parts) < 2 or parts[1] != "lakka":
                continue
            dst = stage.joinpath(*parts[1:])
            if member.isdir():
                dst.mkdir(parents=True, exist_ok=True)
            else:
                dst.parent.mkdir(parents=True, exist_ok=True)
                with source.extractfile(member) as contents, dst.open("xb") as target:
                    shutil.copyfileobj(contents, target, 1 << 20)


def text_file(path, content, mode=0o644):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(content, encoding="utf-8", newline="\n")
    path.chmod(mode)


def configure(root, version):
    units = root / "usr/lib/systemd/system"
    tools = root / "usr/lib/kefiriz"
    default_target = units / "default.target"
    if (default_target.readlink() != Path("retroarch.target") or
            not (units / "graphical.target.wants/xorg.service").is_file() or
            not (root / "usr/bin/dock-hotplug").is_file()):
        raise ValueError("Unexpected Switchroot graphical boot configuration")
    release = root / "etc/os-release"
    original = release.read_text()
    if 'BUILD_ID="5f29696259313f50a215bb88b1b2d5e606ab93b5"' not in original:
        raise ValueError("Unexpected upstream OS release")
    (tools / "upstream-os-release").write_text(original)
    retained = [x for x in original.splitlines() if x.startswith(("LIBREELEC_", "BUILD_ID="))]
    text_file(release, f'NAME="KEFIRIZ OS"\nVERSION="{version}"\nID=kefiriz\nID_LIKE="lakka libreelec"\n'
                       f'VERSION_ID="{version}"\nPRETTY_NAME="KEFIRIZ OS {version}"\n'
                       + "\n".join(retained) + "\n")
    # The BSP's normal graphical target already starts Xorg and joycond. Its
    # dock udev rule adjusts the active X11 window with xrandr and xdotool.
    # Start our own session there without pulling in the emulator targets.
    default_target.unlink()
    default_target.symlink_to("graphical.target")
    text_file(units / "kefiriz-desktop.service", """[Unit]
Description=KEFIRIZ desktop
Requires=xorg.service kefiriz-storage.service
After=xorg.service fix-permissions.service kefiriz-storage.service
Wants=kefiriz-transfers.service kefiriz-network.service

[Service]
User=Lakka
Group=Lakka
Environment=HOME=/storage DISPLAY=:0 PULSE_SERVER=127.0.0.1
ExecStartPre=/usr/lib/kefiriz/profile-init
ExecStartPre=/usr/bin/dock-hotplug
ExecStart=/usr/lib/kefiriz/desktop
Restart=always
RestartSec=2

[Install]
WantedBy=graphical.target
""")
    graphical = units / "graphical.target.wants"
    (graphical / "kefiriz-desktop.service").symlink_to("../kefiriz-desktop.service")
    if (units / "retroarch.target.wants/wii-u-gc-adapter.service").is_symlink():
        (graphical / "wii-u-gc-adapter.service").symlink_to("../wii-u-gc-adapter.service")
    # FAT32 can supply ownership through mount options. Never recurse over a
    # pre-existing profile directory or reset user preferences on update.
    text_file(tools / "storage-setup", """#!/bin/sh
set -eu
target=/storage/kefiriz
[ ! -L "$target" ] || exit 1
[ -d "$target" ] || mkdir -m 0750 "$target"
chown 1000:1000 "$target" 2>/dev/null || :
""", 0o755)
    text_file(units / "kefiriz-storage.service", """[Unit]
Description=Prepare KEFIRIZ profile directory
RequiresMountsFor=/storage
Before=kefiriz-desktop.service kefiriz-transfers.service

[Service]
Type=oneshot
ExecStart=/usr/lib/kefiriz/storage-setup
RemainAfterExit=yes
""")
    text_file(units / "kefiriz-transfers.service", """[Unit]
Description=KEFIRIZ download queue
Requires=kefiriz-storage.service
After=kefiriz-storage.service connman.service

[Service]
User=Lakka
Group=Lakka
Environment=HOME=/storage
ExecStartPre=/usr/lib/kefiriz/profile-init
ExecStart=/usr/lib/kefiriz/transfer-worker
Restart=on-failure
RestartSec=3
NoNewPrivileges=yes
""")
    text_file(units / "kefiriz-network.service", """[Unit]
Description=KEFIRIZ local Wi-Fi settings
Wants=connman.service
After=dbus.service connman.service

[Service]
ExecStart=/usr/lib/kefiriz/transfer-worker network
RuntimeDirectory=kefiriz-network
RuntimeDirectoryMode=0755
ProtectSystem=strict
ProtectHome=yes
PrivateTmp=yes
NoNewPrivileges=yes
CapabilityBoundingSet=CAP_CHOWN CAP_DAC_OVERRIDE
Restart=on-failure
RestartSec=3
""")
    text_file(root / "usr/lib/tmpfiles.d/kefiriz-power.conf",
              "d /run/kefiriz-power 0700 Lakka Lakka - -\n")
    for operation in ("poweroff", "reboot", "suspend"):
        unit = "kefiriz-" + operation
        request = "/run/kefiriz-power/" + operation
        text_file(units / (unit + ".path"), f"""[Unit]
Description=Watch for a local KEFIRIZ {operation} request
After=systemd-tmpfiles-setup.service

[Path]
PathExists={request}
""")
        text_file(units / (unit + ".service"), f"""[Unit]
Description=Apply a local KEFIRIZ {operation} request

[Service]
Type=oneshot
ExecStartPre=/bin/rm -f {request}
ExecStart=/usr/bin/systemctl --no-block {operation}
""")
        (units / "multi-user.target.wants" / (unit + ".path")).symlink_to("../" + unit + ".path")


def compile_desktop(args, root):
    lib = root / "usr/lib"
    dst = lib / "kefiriz/desktop"
    cc = [args.cc]
    if args.clang_headers:
        cc += ["--target=aarch64-linux-gnu", "-isystem", str(args.clang_headers)]
    if args.lld:
        cc += ["-fuse-ld=" + str(args.lld)]
    # Link the image's exact runtime libraries. Only the ELF startup objects
    # and C headers come from the cross toolchain.
    if args.cross_lib:
        crt = args.cross_lib
    else:
        scrt = Path(subprocess.check_output([*cc, "-print-file-name=Scrt1.o"], text=True).strip())
        if not scrt.is_absolute() or not scrt.is_file():
            raise ValueError("Install AArch64 libc development files or set --cross-lib")
        crt = scrt.parent
    native = PROJECT / "native"
    subprocess.run([*cc, "-std=gnu11", "-O2", "-fPIE", "-pie", "-nostdlib", "-march=armv8-a",
                    "-Wl,-z,defs,-z,relro,-z,now", "-Wl,--dynamic-linker,/lib/ld-linux-aarch64.so.1",
                    "-Wl,-rpath-link," + str(lib), "-I" + str(native), "-idirafter", "/usr/include",
                    crt / "Scrt1.o", crt / "crti.o", native / "desktop.c", native / "core.c",
                    native / "draw.c", native / "desktop_input.c", lib / "libXrender.so.1", lib / "libX11.so.6",
                    lib / "libm.so.6", lib / "libc.so.6", crt / "crtn.o",
                    "-o", str(dst)], check=True)
    data = dst.read_bytes()
    if (data[:6] != b"\x7fELF\x02\x01" or struct.unpack_from("<HH", data, 16) != (3, 183)
            or struct.unpack_from("<Q", data, 24)[0] == 0):
        raise ValueError("Desktop must be an executable AArch64 PIE with an entry point")


def build(args):
    acquire(args.archive)
    if args.output.exists():
        raise ValueError("Select a new output directory for the assembled release")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="kefiriz-root-", dir=args.scratch) as work, \
         tempfile.TemporaryDirectory(prefix="kefiriz-stage-", dir=args.output.parent) as dest:
        root = Path(work) / "rootfs"
        stage = Path(dest) / "payload"
        extract_card_files(args.archive, stage)
        lakka = stage / "lakka"
        system = lakka / "SYSTEM"
        subprocess.run(["unsquashfs", "-no-progress", "-d", root, system], check=True)
        tools = root / "usr/lib/kefiriz"
        shutil.copytree(PROJECT / "runtime", tools)
        if not (tools / "VERSION").is_file() or not (tools / "transfer-worker").is_file():
            raise ValueError("Build the KEFIRIZ runtime before preparing the image")
        header = (PROJECT / "native/version.h").read_text()
        release = re.search(r'^#define\s+KF_OS_VERSION\s+"([^"\n]+)"\s*$', header, re.MULTILINE)
        if ((tools / "VERSION").read_text().strip() != args.version or
                not release or release[1] != args.version):
            raise ValueError("Desktop runtime version differs from the requested Linux release")
        with (tools / "transfer-worker").open("rb") as worker:
            elf = worker.read(20)
        if (len(elf) < 20 or elf[:6] != b"\x7fELF\x02\x01" or
                struct.unpack_from("<HH", elf, 16) not in ((2, 183), (3, 183))):
            raise ValueError("The transfer service must be a Linux AArch64 executable")
        for path in (tools, *tools.rglob("*")):
            path.chmod(0o755 if path.is_dir() else 0o644)
        if not all((tools / "notices" / name).is_file() for name in
                   ("Barlow-OFL.txt", "PTSans-OFL.txt", "Go-LICENSE.txt", "transfer-dependencies.txt")):
            raise ValueError("Missing bundled typography or service notices")
        licenses = root / "usr/share/licenses/kefiriz"
        licenses.parent.mkdir(parents=True, exist_ok=True)
        licenses.symlink_to("../../lib/kefiriz/notices", target_is_directory=True)
        for filename in ("profile-init", "transfer-worker"):
            (tools / filename).chmod(0o755)
        configure(root, args.version)
        compile_desktop(args, root)
        for name in ("kefiriz-logo.bmp", "kefiriz-icon.bmp"):
            shutil.copyfile(PROJECT / "ui/boot" / name, lakka / "boot" / name)
        output = Path(work) / "SYSTEM"
        subprocess.run(["mksquashfs", root, output, "-noappend", "-no-progress", "-comp", "lzo",
                        "-Xalgorithm", "lzo1x_1", "-b", "524288", "-processors", str(args.jobs),
                        "-mkfs-time", str(EPOCH), "-all-time", str(EPOCH)], check=True)
        shutil.copyfile(output, system)
        # Keep the upstream checksum file's relative path spelling.
        old = (lakka / "SYSTEM.md5").read_text().strip().split(maxsplit=1)
        text_file(lakka / "SYSTEM.md5", f"{digest(system, 'md5')}  {old[1]}\n")
        if system.stat().st_size >= 2**32:
            raise ValueError("SquashFS exceeds the FAT32 file size limit")
        stage.rename(args.output)
        print("KEFIRIZ SYSTEM SHA-256", digest(args.output / "lakka/SYSTEM"))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--archive", type=Path, required=True, help="Cached official Lakka TAR (fetched if missing)")
    parser.add_argument("--output", type=Path, required=True, help="New host-side FAT32 payload directory")
    parser.add_argument("--scratch", type=Path, help="Linux filesystem temporary directory, e.g. /var/tmp")
    parser.add_argument("--version", default=(PROJECT / "runtime/VERSION").read_text().strip())
    parser.add_argument("--cc", default="aarch64-linux-gnu-gcc")
    parser.add_argument("--clang-headers", type=Path)
    parser.add_argument("--lld", type=Path)
    parser.add_argument("--cross-lib", type=Path, help="AArch64 libc startup object directory")
    parser.add_argument("--jobs", type=int, default=4)
    args = parser.parse_args()
    if not (1 <= args.jobs <= 64) or not args.version or any(
            c not in "0123456789.ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz-" for c in args.version):
        parser.error("Invalid version or compression job count")
    if os.geteuid() != 0:
        parser.error("Use root or fakeroot in the build environment to preserve BSP filesystem metadata")
    build(args)
