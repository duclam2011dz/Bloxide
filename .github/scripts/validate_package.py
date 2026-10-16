"""Validate a relocated install without source/build paths; Python is CI-only."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import stat
import subprocess
import tempfile
import time

parser = argparse.ArgumentParser()
parser.add_argument("archive", type=Path)
parser.add_argument("--gpu", action="store_true")
parser.add_argument("--full-gpu", action="store_true",
                    help="Run all five hardware fixtures against the relocated package")
parser.add_argument("--expected-commit")
args = parser.parse_args()
sidecar = args.archive.with_name(args.archive.name + ".sha256")
expected = sidecar.read_text(encoding="utf-8").split()[0].lower()
digest = hashlib.sha256()
with args.archive.open("rb") as archive:
    for part in iter(lambda: archive.read(1024 * 1024), b""):
        digest.update(part)
if len(expected) != 64 or digest.hexdigest() != expected:
    raise RuntimeError("Archive SHA-256 mismatch: " + args.archive.name)
with tempfile.TemporaryDirectory(prefix="bloxide-package-") as temporary:
    work = Path(temporary)
    extract = work / "relocated space-việt"
    shutil.unpack_archive(str(args.archive.resolve()), str(extract))
    candidates = [p for p in extract.rglob("Bloxide.exe" if os.name == "nt" else "Bloxide")
                  if p.is_file()]
    if len(candidates) != 1:
        raise RuntimeError("Package must contain exactly one game binary")
    game = candidates[0]
    prefix = game.parent.parent
    required = ["README.md", "DEVELOPMENT.md", "CONTRIBUTING.md", "CODE_STYLE.md"]
    for name in required:
        if not (prefix / "share/doc/Bloxide" / name).is_file():
            raise RuntimeError("Missing packaged documentation: " + name)
    notices = prefix / "share/licenses/Bloxide"
    if not (notices / "THIRD_PARTY_NOTICES.md").is_file():
        raise RuntimeError("Missing license notices")
    for name in ["LICENSE", "SDL2/copyright", "lodepng/LICENSE", "imgui/LICENSE.txt",
                 "glad/LICENSE", "glad/Apache-2.0.txt", "glad/CC0-1.0.txt", "glad/khrplatform.h"]:
        if not (notices / name).is_file():
            raise RuntimeError("Missing license provenance: " + name)
    for name in ["voxel.vert", "voxel.frag", "heightmap.vert", "surface.vert"]:
        if not (game.parent / "shaders" / name).is_file():
            raise RuntimeError("Missing shader: " + name)
    pngs = [p for p in (game.parent / "assets/textures").glob("*.png")
            if p.name not in ("contact_sheet.png", "tiled_preview.png")]
    if len(pngs) != 112:
        raise RuntimeError("Expected exactly 112 material PNGs")
    cwd = work / "unrelated cwd"
    cwd.mkdir()
    env = os.environ.copy()
    env["BLOXIDE_DATA_HOME"] = str(work / "user-data")

    def run(command, timeout=240, success=True):
        result = subprocess.run([str(x) for x in command], cwd=cwd, env=env,
                                capture_output=True, timeout=timeout)
        if success and result.returncode:
            raise RuntimeError(result.stdout.decode("utf-8", "replace") +
                               result.stderr.decode("utf-8", "replace"))
        if not success and not result.returncode:
            raise RuntimeError("Invalid assets unexpectedly accepted")
        return result

    identity = json.loads(run([game, "--version", "--json"]).stdout)
    if identity["version"] != "1.5.0":
        raise RuntimeError("Wrong package version")
    packaged_identity = json.loads((prefix / "build-manifest.json").read_text())
    if packaged_identity != identity:
        raise RuntimeError("Installed manifest differs from executable identity")
    if args.expected_commit and (identity["commit"] != args.expected_commit or identity["dirty"]):
        raise RuntimeError("Package is dirty or belongs to another commit")
    run([game, "--validate-assets"])
    assets = game.parent / "assets"
    if not (assets / "benchmarks/v1.5-world.meta").is_file():
        raise RuntimeError("Missing versioned benchmark world configuration")
    missing = game.parent / "assets-hidden"
    assets.rename(missing)
    try:
        run([game, "--validate-assets"], success=False)
    finally:
        missing.rename(assets)
    original = pngs[0].read_bytes()
    try:
        pngs[0].write_bytes(b"not a PNG")
        run([game, "--validate-assets"], success=False)
    finally:
        pngs[0].write_bytes(original)
    before = {p.relative_to(prefix).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest()
              for p in prefix.rglob("*") if p.is_file()}
    windows_user = None
    try:
        if os.name == "nt":
            windows_user = subprocess.check_output(["whoami"], text=True).strip()
            subprocess.run(["icacls", str(prefix), "/deny",
                            windows_user + ":(OI)(CI)(WD,AD,WEA,WA)"], check=True, capture_output=True)
        else:
            for p in prefix.rglob("*"):
                p.chmod(p.stat().st_mode & ~0o222)
            prefix.chmod(prefix.stat().st_mode & ~0o222)
        profiler = game.parent / ("BloxideProfiler.exe" if os.name == "nt" else "BloxideProfiler")
        run([profiler, "--suite", "noise", "--quick", "--output", work / "native-profiler"])
        manifest = json.loads((work / "native-profiler/manifest.json").read_text())
        if not manifest["suite_complete"] or manifest["build"]["commit"] != identity["commit"]:
            raise RuntimeError("Profiler provenance/incomplete suite")
        fixtures = []
        if args.full_gpu:
            fixtures = [("Density", ["--no-lod", "--preset", "mountainous"]),
                        ("Lod", ["--require-lod", "--preset", "mountainous", "--far-radius", "24"]),
                        ("Legacy", []),
                        ("Positive", ["--smoke-origin", "1099511627648"]),
                        ("Negative", ["--smoke-origin", "-1099511627648"])]
        elif args.gpu:
            fixtures = [("relocated", ["--no-lod", "--preset", "flat", "--caves", "off"])]
        for name, options in fixtures:
            start = time.monotonic()
            run([game, "--smoke-test", *options, "--seed", "130",
                 "--world", work / name / "world",
                 "--diagnostics-dir", work / name / "diagnostics"], timeout=1800)
            print(name + " GPU fixture passed in %.2f s" % (time.monotonic() - start), flush=True)
        after = {p.relative_to(prefix).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest()
                 for p in prefix.rglob("*") if p.is_file()}
        if before != after:
            raise RuntimeError("Read-only package contents changed")
    finally:
        if windows_user:
            subprocess.run(["icacls", str(prefix), "/remove:d", windows_user, "/T"],
                           check=True, capture_output=True)
        elif os.name != "nt":
            prefix.chmod(prefix.stat().st_mode | stat.S_IWUSR)
            for p in prefix.rglob("*"):
                p.chmod(p.stat().st_mode | stat.S_IWUSR)
    print(json.dumps({"package": args.archive.name, "build": identity,
                      "relocated_readonly": True, "gpu_tested": bool(fixtures),
                      "gpu_fixture_count": len(fixtures)}))
