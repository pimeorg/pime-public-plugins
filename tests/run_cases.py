#!/usr/bin/env python3
"""Run per-plugin test cases through the native simulator.

Each case (tests/cases/*.yaml) describes one call:

    name: ...
    event: { type: text, text: nihao }
    state: { buf: ni }               # optional plugin state
    workspace: { plugin/x/y: ... }   # optional input workspace
    context: { locale: zh-CN }       # optional
    time: { now_ms: 1790478360000, tz_min: 480 }   # optional wall clock
    expect:
      handled: true
      commands: [ ... ]              # exact list, each an exact map
      workspace: [ { k: ..., v: ... } ]
      first_candidate: { text: ... }   # or candidates_include: [ ... ]
      state: { ... }

The simulator is built by CMake (`pime_sim` target) from the plugin's own
sources; see tests/host/sim_main.cpp.
"""

from __future__ import annotations

import argparse
import os
import subprocess
import sys
from pathlib import Path

try:
    import yaml
except ImportError:  # pragma: no cover
    sys.exit("run_cases: PyYAML is required (pip install pyyaml)")

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import cbor  # noqa: E402


def subset(expected, actual) -> bool:
    if isinstance(expected, dict):
        if not isinstance(actual, dict):
            return False
        return all(key in actual and subset(value, actual[key])
                   for key, value in expected.items())
    if isinstance(expected, list):
        if not isinstance(actual, list) or len(expected) > len(actual):
            return False
        return all(subset(item, actual[index])
                   for index, item in enumerate(expected))
    return expected == actual


def defaults(manifest: dict) -> dict:
    settings = {}
    for entry in manifest.get("settings") or []:
        if isinstance(entry, dict) and "key" in entry:
            settings[entry["key"]] = entry.get("default")
    return settings


def frame_for(case: dict, settings: dict) -> dict:
    frame = {
        "event": case.get("event") or {"type": "reset"},
        "workspace": case.get("workspace") or {},
        "context": case.get("context") or {},
        "config": {"settings": settings},
        "assets": {},
    }
    # Leave `state` out when the case has none: the simulator then carries the
    # previous step's state over, like the host does between calls.
    if case.get("state"):
        frame["state"] = case["state"]
    return frame


def compare(case: dict, output: dict) -> list[str]:
    problems = []
    expect = case.get("expect") or {}
    if "handled" in expect and output.get("handled") != expect["handled"]:
        problems.append(f"handled {output.get('handled')} != {expect['handled']}")

    if "commands" in expect:
        commands = output.get("commands") or []
        if len(commands) != len(expect["commands"]):
            problems.append(f"commands {commands} != {expect['commands']}")
        else:
            for index, command in enumerate(expect["commands"]):
                if not subset(command, commands[index]):
                    problems.append(
                        f"command[{index}] {commands[index]} != {command}")

    workspace = output.get("workspace") or {}
    if isinstance(workspace, list):
        workspace = {entry.get("k"): entry.get("v") for entry in workspace
                     if isinstance(entry, dict)}
    if "workspace" in expect:
        if isinstance(expect["workspace"], list):
            for entry in expect["workspace"]:
                key = entry["k"]
                if key not in workspace or not subset(entry.get("v"), workspace[key]):
                    problems.append(
                        f"workspace[{key}] {workspace.get(key)} != {entry.get('v')}")
        elif not subset(expect["workspace"], workspace):
            problems.append(f"workspace {workspace} != {expect['workspace']}")
    if "first_candidate" in expect:
        candidates = workspace.get("candidates") or []
        first = candidates[0] if candidates else None
        if not subset(expect["first_candidate"], first):
            problems.append(f"first_candidate {first} != {expect['first_candidate']}")
    if "candidates_include" in expect:
        candidates = workspace.get("candidates") or []
        texts = [entry.get("text") for entry in candidates
                 if isinstance(entry, dict)]
        for wanted in expect["candidates_include"]:
            if wanted not in texts:
                problems.append(f"candidates {texts} do not include {wanted}")

    if "state" in expect:
        if not subset(expect["state"], output.get("state") or {}):
            problems.append(f"state {output.get('state')} != {expect['state']}")
    return problems


def main() -> int:
    parser = argparse.ArgumentParser(description="Run plugin test cases")
    parser.add_argument("--root", default=str(Path(__file__).resolve().parents[1]))
    parser.add_argument("--sim-dir", required=True)
    parser.add_argument("--filter", default=None)
    args = parser.parse_args()

    root = Path(args.root).resolve()
    sim_dir = Path(args.sim_dir).resolve()
    suffix = ".exe" if sys.platform.startswith("win") else ""

    failures = 0
    total = 0
    for manifest_path in sorted((root / "plugins").glob("*/*/manifest.yaml")):
        if manifest_path.parent.name.startswith("_"):
            continue
        manifest = yaml.safe_load(manifest_path.read_text(encoding="utf-8"))
        if manifest.get("runtime") != "wasm":
            continue
        plugin_id = manifest.get("id", manifest_path.parent.name)
        if args.filter and args.filter not in plugin_id:
            continue
        binary = sim_dir / ("sim_" + plugin_id.replace("-", "_") + suffix)
        if not binary.exists():
            print(f"skip {plugin_id}: {binary.name} not built")
            continue
        settings = defaults(manifest)
        cases = sorted((manifest_path.parent / "tests" / "cases").glob("*.yaml"))
        for case_path in cases:
            total += 1
            case = yaml.safe_load(case_path.read_text(encoding="utf-8")) or {}
            case_settings = dict(settings)
            case_settings.update(case.get("settings") or {})
            env = dict(os.environ)
            clock = case.get("time") or {}
            if "now_ms" in clock:
                env["SIM_NOW_MS"] = str(clock["now_ms"])
            if "tz_min" in clock:
                env["SIM_TZ_MIN"] = str(clock["tz_min"])
            init = cbor.dumps({"settings": case_settings, "assets": {},
                               "abi": {"major": 1, "minor": 0}})
            if "steps" in case:
                frames = []
                previous_state = {}
                for step in case["steps"]:
                    frame = frame_for(step, case_settings)
                    if "state" not in step and previous_state:
                        frame["state"] = previous_state
                    frames.append(frame)
                payload = cbor.dumps(frames)
            else:
                payload = cbor.dumps(frame_for(case, case_settings))
            result = subprocess.run(
                [str(binary), "--assets", str(manifest_path.parent / "assets"),
                 "--init", init.hex()],
                input=payload, capture_output=True, env=env)
            if result.returncode != 0:
                print(f"FAIL {plugin_id}/{case_path.name}: "
                      f"exit {result.returncode} {result.stderr.decode(errors='replace').strip()}")
                failures += 1
                continue
            try:
                raw = cbor.loads(result.stdout)
                if "steps" in case:
                    outputs = [cbor.loads(bytes(item)) for item in raw]
                else:
                    outputs = [raw]
            except Exception as exc:  # noqa: BLE001
                print(f"FAIL {plugin_id}/{case_path.name}: bad output CBOR ({exc})")
                failures += 1
                continue
            steps = case.get("steps") or [case]
            if len(outputs) != len(steps):
                print(f"FAIL {plugin_id}/{case_path.name}: "
                      f"{len(outputs)} output(s) for {len(steps)} step(s)")
                failures += 1
                continue
            problems = []
            for index, (step, output) in enumerate(zip(steps, outputs)):
                for problem in compare(step, output):
                    label = f"step[{index}] " if len(steps) > 1 else ""
                    problems.append(label + problem)
                previous_state = output.get("state") or {}
            if problems:
                failures += 1
                print(f"FAIL {plugin_id}/{case_path.name}")
                for problem in problems:
                    print(f"  - {problem}")
            else:
                print(f"ok   {plugin_id}/{case_path.name}")

    print(f"{total - failures}/{total} case(s) passed")
    return 0 if failures == 0 else 1


if __name__ == "__main__":
    raise SystemExit(main())
