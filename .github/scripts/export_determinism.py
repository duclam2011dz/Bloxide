"""Export reviewed block probes by compiler profile; never update golden values."""
import argparse
import json
from pathlib import Path
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument("build", type=Path)
args = parser.parse_args()
suffix = ".exe" if (args.build / "BloxideNoiseBenchmark.exe").is_file() else ""
probe = (args.build / ("BloxideNoiseBenchmark" + suffix)).resolve()

def run(arguments):
    return subprocess.check_output([str(probe), *arguments], text=True).strip()

report = {"build": json.loads(run(["--version", "--json"])), "probes": {}}
for version, mode in [("simplex-1", []), ("terrain-fields-1", ["--fields"]),
                      ("density-world-1", ["--density"])]:
    for seed in ["0", "130", "18446744073709551615"]:
        command = [*mode, "--probe", "--seed", seed]
        reference = run(command)
        if run([*command, "--reverse"]) != reference:
            raise RuntimeError("Order-dependent output: " + version + "/" + seed)
        if run([*command, "--reverse", "--threads"]) != reference:
            raise RuntimeError("Worker-dependent output: " + version + "/" + seed)
        report["probes"][version + "/" + seed] = reference.splitlines()
(args.build / "determinism-profile.json").write_text(
    json.dumps(report, indent=2) + "\n", encoding="utf-8")
print(report["build"]["determinism_profile"], "36 exact coordinate probes exported")
