"""Build the Linux arm64 transfer worker and install its third-party notices."""
from pathlib import Path
import os
import shutil
import subprocess

project = Path(__file__).resolve().parents[1]
source = project / "transfers"
runtime = project / "runtime"
notices = runtime / "notices"
notices.mkdir(exist_ok=True)
env = dict(os.environ, CGO_ENABLED="0", GOOS="linux", GOARCH="arm64")
subprocess.run(["go", "mod", "verify"], cwd=source, env=env, check=True)
subprocess.run(["go", "build", "-mod=readonly", "-trimpath", "-ldflags=-s",
                "-o", str(runtime / "transfer-worker"), "."], cwd=source, env=env, check=True)
packages = subprocess.check_output(
    ["go", "list", "-deps", "-f", "{{with .Module}}{{.Path}}|{{.Version}}|{{.Dir}}{{end}}", "."],
    cwd=source, env=env, text=True).splitlines()
licenses = []
for row in sorted(set(packages)):
    if not row:
        continue
    module, version, directory = row.split("|", 2)
    if not version:
        continue
    root = Path(directory)
    files = sorted(f for f in root.iterdir() if f.is_file() and
                   f.name.upper().split(".")[0] in {"COPYING", "LICENSE", "LICENCE", "NOTICE"})
    for entry in files:
        licenses.append(f"{module} {version} / {entry.name}\n\n{entry.read_text(errors='replace')}\n")
    if not files:
        licenses.append(f"{module} {version}\nSource: https://{module}\n"
                        "The pinned module contains no top-level license file.\n")
(notices / "transfer-dependencies.txt").write_text("\n".join(licenses), encoding="utf-8", newline="\n")
go_root = subprocess.check_output(["go","env","GOROOT"], cwd=source, env=env, text=True).strip()
shutil.copyfile(Path(go_root) / "LICENSE", notices / "Go-LICENSE.txt")
shutil.copyfile(project / "ui/font/OFL.txt", notices / "Barlow-OFL.txt")
shutil.copyfile(project / "ui/font/ptsans/OFL.txt", notices / "PTSans-OFL.txt")

