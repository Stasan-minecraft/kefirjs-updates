"""Build the PC graphics renderer using Visual Studio Roslyn and .NET Framework 4.8."""
import argparse
from hashlib import sha256
import os
from pathlib import Path
import subprocess


SOURCE = Path(__file__).resolve().parent


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--csc", type=Path, required=True)
    parser.add_argument("--runtime", type=Path, required=True,
                        help="runtime directory containing the upstream Newtonsoft.Json.dll")
    parser.add_argument("--framework", type=Path, default=Path(os.environ["ProgramFiles(x86)"]) /
                        "Reference Assemblies/Microsoft/Framework/.NETFramework/v4.8")
    args = parser.parse_args()
    runtime = args.runtime.resolve()
    jsonlib = runtime / "Newtonsoft.Json.dll"
    if sha256(jsonlib.read_bytes()).hexdigest() != "c5c83bbc1741be6ff4c490c0aee34c162945423ec577c646538b2d21ce13199e":
        raise ValueError("Expected Newtonsoft.Json bundled with SwitchThemes 4.8.3")
    libs = ("mscorlib", "System", "System.Core", "System.Drawing", "System.Windows.Forms")
    sources = sorted((SOURCE / "SwitchThemesCommon").rglob("*.cs"))
    subprocess.run([args.csc, "/nologo", "/noconfig", "/nostdlib+", "/langversion:8.0",
                    "/define:WIN", "/target:exe", "/platform:anycpu", "/optimize+",
                    "/deterministic+", f"/pathmap:{SOURCE}=/_/renderer",
                    f"/out:{runtime / 'Render.exe'}", f"/reference:{jsonlib}",
                    *(f"/reference:{args.framework / (name + '.dll')}" for name in libs),
                    SOURCE / "Render.cs", SOURCE / "NativeHomeStyle.cs", *sources], check=True)


if __name__ == "__main__":
    main()
