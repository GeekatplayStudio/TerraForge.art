#!/usr/bin/env python3
"""Mutation testing: does a passing suite actually have teeth?

A green test proves the code runs. It does not prove the test would notice if
the code were wrong - and a test that would not notice is a comment with a
build cost. This breaks the source on purpose, one edit at a time, rebuilds,
and reruns the suite. A mutant the suite kills is a line the suite tests. A
mutant that survives is a line nobody is watching.

    python scripts/mutate.py               # every suite in tests/mutants.json
    python scripts/mutate.py points_thumb  # one of them

Mutants live in `tests/mutants.json` so they are reviewed like code. Writing
one is the interesting part: it should be a plausible mistake, not a
nonsense edit. "The clip runs in float instead of double" is a mistake
somebody would make; `return 0;` at the top is not.

Some correct code cannot be mutated into a detectable failure - a guard
against undefined behaviour, for instance, whose absence happens to produce
the same answer on this machine. Those belong in `not_mutated`, with the
reason, rather than quietly left out.
"""
import json
import os
import shutil
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BUILD = os.path.join(ROOT, "build")
SUITES = os.path.join(ROOT, "tests", "mutants.json")


def build(target):
    r = subprocess.run(["cmake", "--build", "build", "--target", target],
                       capture_output=True, text=True, cwd=ROOT)
    return r.returncode == 0


def run_suite(exe, timeout):
    path = os.path.join(BUILD, exe + (".exe" if os.name == "nt" else ""))
    try:
        r = subprocess.run([path], capture_output=True, text=True,
                           timeout=timeout)
    except subprocess.TimeoutExpired:
        return 1, "(timed out - which is a failure the suite should assert "
        "on itself, not leave to the runner)"
    first = next((l.strip() for l in r.stdout.splitlines() if "FAIL" in l), "")
    return r.returncode, first


def run(suite):
    src = os.path.join(ROOT, suite["file"])
    bak = src + ".mutate-backup"
    original = open(src, encoding="utf-8").read()
    timeout = suite.get("timeout", 300)

    print(f"== {suite['name']}: {suite['file']} -> {suite['run']}")
    if not build(suite["target"]):
        print("  the unmutated source does not build - nothing to say")
        return False
    code, first = run_suite(suite["run"], timeout)
    if code != 0:
        # Say what went wrong. A bare "it already fails" sent me looking for a
        # bug in the code under test twice, when both times the binary was
        # still being linked.
        print(f"  the unmutated suite already fails - fix that first\n"
              f"    exit {code}: {first or '(no FAIL line - it did not run)'}")
        return False

    shutil.copyfile(src, bak)
    killed, survived = 0, []
    try:
        for m in suite["mutants"]:
            if m["find"] not in original:
                print(f"  ??       {m['name']}: no longer matches the source")
                survived.append(m["name"] + " (stale)")
                continue
            open(src, "w", encoding="utf-8", newline="").write(
                original.replace(m["find"], m["replace"], 1))
            if not build(suite["target"]):
                # A mutant the compiler rejects is still a mutant the change
                # cannot ship as. Counted, and said plainly.
                print(f"  killed   {m['name']} (did not compile)")
                killed += 1
                continue
            code, first = run_suite(suite["run"], timeout)
            if code != 0:
                killed += 1
                print(f"  killed   {m['name']}")
                if first:
                    print(f"             {first}")
            else:
                survived.append(m["name"])
                print(f"  SURVIVED {m['name']}  <-- nothing tests this")
    finally:
        shutil.copyfile(bak, src)
        os.remove(bak)
        build(suite["target"])

    for note in suite.get("not_mutated", []):
        print(f"  --       {note['name']}: {note['why']}")
    print(f"  {killed}/{len(suite['mutants'])} killed\n")
    return not survived


def main():
    suites = json.load(open(SUITES, encoding="utf-8"))["suites"]
    want = sys.argv[1] if len(sys.argv) > 1 else None
    if want:
        suites = [s for s in suites if s["name"] == want]
        if not suites:
            raise SystemExit(f"no suite called '{want}' in tests/mutants.json")
    ok = all([run(s) for s in suites])
    print("every mutant killed" if ok else "some mutants survived")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
