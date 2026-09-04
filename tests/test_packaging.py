"""Packaging and install lock.

An installer is the one piece of a project that nobody exercises until a
stranger tries it, by which time the mistake has already shipped. These tests
check the things that break silently:

  * a shell script with CRLF endings, which macOS reports as
    "bad interpreter: /usr/bin/env bash^M" - a message that reads as a broken
    script rather than as a line-ending problem;
  * a script that lost its executable bit, so double-clicking it does nothing;
  * a Windows and a POSIX dependency fetcher that have drifted apart, so the
    two platforms build against different versions of Dear ImGui;
  * an installer that stages a directory the repository no longer has.

None of it needs a compiler, so it runs in the ordinary Python suite.
"""

import re
import stat
import subprocess
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[1]

POSIX_SCRIPTS = [
    "build.sh",
    "test.sh",
    "start.sh",
    "install.command",
    "scripts/get_deps.sh",
    "scripts/install.sh",
    "packaging/macos/make_app.sh",
    "packaging/macos/make_dmg.sh",
]

WINDOWS_SCRIPTS = [
    "install.bat",
    "build.ps1",
    "test.ps1",
    "start.ps1",
    "scripts/get_deps.ps1",
    "scripts/install.ps1",
    "packaging/windows/stage.ps1",
    "packaging/windows/make_installer.ps1",
    "packaging/windows/terraforge.iss",
]


@pytest.mark.parametrize("rel", POSIX_SCRIPTS + WINDOWS_SCRIPTS)
def test_script_exists(rel):
    assert (ROOT / rel).is_file(), f"{rel} is missing"


@pytest.mark.parametrize("rel", POSIX_SCRIPTS)
def test_posix_scripts_have_unix_line_endings(rel):
    data = (ROOT / rel).read_bytes()
    assert b"\r\n" not in data, (
        f"{rel} has CRLF line endings; on macOS and Linux that fails with "
        f"'bad interpreter: /usr/bin/env bash^M'. .gitattributes should be "
        f"forcing eol=lf for it."
    )


@pytest.mark.parametrize("rel", POSIX_SCRIPTS)
def test_posix_scripts_start_with_a_shebang(rel):
    first = (ROOT / rel).read_bytes().split(b"\n", 1)[0]
    assert first.startswith(b"#!"), f"{rel} has no #! line, so the shell picks its own"


def test_gitattributes_forces_unix_endings_for_shell_scripts():
    """The working tree can be right while a fresh clone is wrong.

    git's autocrlf converts on checkout, so without an explicit rule a
    developer on Windows publishes .sh files that arrive with CRLF on the Mac
    that clones them - and the file in this repository still looks fine.
    """
    text = (ROOT / ".gitattributes").read_text(encoding="utf-8")
    for pattern in ("*.sh", "*.command"):
        assert re.search(rf"^\s*\{re.escape(pattern)[1:]}\s+.*eol=lf", text, re.M), (
            f".gitattributes does not pin {pattern} to eol=lf"
        )


@pytest.mark.parametrize("rel", POSIX_SCRIPTS)
def test_posix_scripts_are_executable_in_the_index(rel):
    """Double-clicking install.command has to work on a fresh clone.

    The permission that matters is the one recorded in git, not the one on
    this filesystem - Windows checkouts have no executable bit at all.
    """
    out = subprocess.run(
        ["git", "ls-files", "--stage", "--", rel],
        cwd=ROOT, capture_output=True, text=True,
    )
    if out.returncode != 0 or not out.stdout.strip():
        pytest.skip("not a git checkout, or the file is not tracked yet")
    mode = out.stdout.split()[0]
    assert mode == "100755", (
        f"{rel} is mode {mode} in the index; it needs 100755 or it cannot be "
        f"run after a clone. Fix with: git update-index --chmod=+x {rel}"
    )


def test_both_dependency_fetchers_agree():
    """The two get_deps scripts must fetch the same things.

    They are the only place the third-party versions are written down, and a
    drift between them means Windows and macOS silently build against
    different sources.
    """
    ps = (ROOT / "scripts/get_deps.ps1").read_text(encoding="utf-8", errors="replace")
    sh = (ROOT / "scripts/get_deps.sh").read_text(encoding="utf-8", errors="replace")

    repos = re.compile(r"https://github\.com/([\w.-]+/[\w.-]+?)(?:\.git)?[\"\s]")
    files = re.compile(r"https://raw\.githubusercontent\.com/\S+?/([\w_.]+\.h(?:pp)?)")

    assert set(repos.findall(ps)) == set(repos.findall(sh)), (
        "the two dependency fetchers clone different repositories"
    )
    assert set(files.findall(ps)) == set(files.findall(sh)), (
        "the two dependency fetchers download different single-header files"
    )
    # the imgui docking branch is the one the studio needs; a default-branch
    # clone loses the docking and viewport API the whole UI is built on
    assert "docking" in ps and "docking" in sh, "imgui must be cloned on the docking branch"


def test_windows_staging_lists_directories_that_exist():
    """Every tree the Windows installer stages must be in the repository."""
    stage = (ROOT / "packaging/windows/stage.ps1").read_text(encoding="utf-8", errors="replace")
    for name in re.findall(r'^CopyTree\s+"([\w/\\.]+)"', stage, re.M):
        assert (ROOT / name).exists(), f"stage.ps1 stages '{name}', which does not exist"


def test_macos_bundle_carries_what_the_app_looks_for():
    """studio/paths.cpp finds the install tree by looking for `orchestrator`.

    If make_app.sh ever stops copying it, install_dir() silently falls back to
    the directory holding the executable and the offline renderers stop
    working, with nothing to say why.
    """
    app = (ROOT / "packaging/macos/make_app.sh").read_text(encoding="utf-8", errors="replace")
    paths = (ROOT / "studio/paths.cpp").read_text(encoding="utf-8", errors="replace")
    assert "orchestrator" in paths, "paths.cpp no longer looks for the marker directory"
    assert re.search(r"for tree in .*orchestrator", app), (
        "make_app.sh does not copy orchestrator into the bundle"
    )
    assert "Contents/Resources" in app and "Contents/MacOS" in app


def test_installer_and_project_versions_match():
    """The Inno Setup default and CMake's version must not drift."""
    cml = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8", errors="replace")
    iss = (ROOT / "packaging/windows/terraforge.iss").read_text(encoding="utf-8", errors="replace")
    m = re.search(r"project\([^)]*VERSION\s+([0-9]+\.[0-9]+\.[0-9]+)", cml)
    assert m, "cannot read the project version from CMakeLists.txt"
    fallback = re.search(r'#define AppVersion "([0-9.]+)"', iss)
    assert fallback, "terraforge.iss has no AppVersion fallback"
    assert fallback.group(1) == m.group(1), (
        f"terraforge.iss falls back to {fallback.group(1)} but the project is "
        f"{m.group(1)}; make_installer.ps1 passes the real one, so this only "
        f"bites someone compiling the .iss by hand"
    )


def test_install_documentation_covers_every_platform():
    doc = (ROOT / "docs/INSTALL.md").read_text(encoding="utf-8", errors="replace")
    for needle in ("install.bat", "install.command", "scripts/install.sh",
                   "get_deps.sh", "get_deps.ps1", "make_dmg.sh", "make_installer.ps1"):
        assert needle in doc, f"docs/INSTALL.md never mentions {needle}"
    readme = (ROOT / "README.md").read_text(encoding="utf-8", errors="replace")
    assert "docs/INSTALL.md" in readme, "the README does not link the install guide"
