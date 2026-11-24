"""Assemble the local Windows installer using the pinned Linux image builder."""
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile


def main():
    project = Path(__file__).resolve().parents[1]
    output = Path(os.environ["KEFIRIZ_BUILD_OUTPUT"]).resolve()
    archive = Path(os.environ["KEFIRIZ_BUILD_ARCHIVE"]).resolve()
    scratch = Path("/var/tmp")
    if sys.version_info < (3, 10):
        raise ValueError("Install Python 3.10 or newer in the default WSL distribution.")
    if os.geteuid():
        raise ValueError("The Linux root filesystem builder requires WSL root.")
    if output.exists() or archive.is_relative_to(output) or project.is_relative_to(output):
        raise ValueError("Select a new output folder separate from the upstream archive and source.")
    compiler = os.environ.get("KEFIRIZ_CC", "aarch64-linux-gnu-gcc")
    missing = [tool for tool in ("go", "unsquashfs", "mksquashfs", compiler) if not shutil.which(tool)]
    if missing:
        raise ValueError("Missing WSL build tools: " + ", ".join(missing) +
                         ". Run BUILD-INSTALLER.bat --help for the Ubuntu packages.")
    with tempfile.TemporaryDirectory(prefix="kefiriz-os-build-", dir=scratch) as temp:
        source = Path(temp) / "source"
        ignore = shutil.ignore_patterns("__pycache__", "*.pyc", "transfer-worker")
        for folder in ("build", "native", "runtime", "transfers", "ui"):
            shutil.copytree(project / folder, source / folder, ignore=ignore)
        payload = Path(temp) / "payload"
        py = [sys.executable, "-B", "-u"]
        subprocess.run([*py, source / "build/worker.py"], check=True)
        subprocess.run([*py, source / "build/rootfs.py", "--archive", archive, "--output",
                        payload, "--scratch", scratch, "--cc", compiler], check=True)
        subprocess.run([*py, project / "install/prepare_release.py", "--payload", payload,
                        "--upstream-archive", archive, "--output", output], check=True)


if __name__ == "__main__":
    try:
        main()
    except (OSError, KeyError, ValueError, subprocess.CalledProcessError) as exc:
        sys.exit("KEFIRIZ installer build failed: " + str(exc))
