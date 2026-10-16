import pathlib
import subprocess

version = subprocess.check_output(["clang-format", "--version"], text=True)
if "18.1.8" not in version:
    raise SystemExit("clang-format 18.1.8 required")
sources = [str(p) for folder in ["include", "src", "tools", "tests"]
           for p in pathlib.Path(folder).rglob("*")
           if p.suffix in [".cpp", ".hpp", ".inl"]]
subprocess.run(["clang-format", "--dry-run", "--Werror", *sources], check=True)
