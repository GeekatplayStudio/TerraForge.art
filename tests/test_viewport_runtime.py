"""Opt-in real-context viewport checks; point TERRAFORGE_TEST_API at a test instance."""
import os
import time

import pytest


def test_viewport_render_pipeline(tmp_path):
    directory = os.environ.get("TERRAFORGE_TEST_API")
    if not directory:
        pytest.skip("requires an isolated running studio with GPX_FREEZE_TIME=1")
    from mcp_server.studio_api import Studio
    from PIL import Image, ImageStat, ImageChops

    studio = Studio(directory)
    deadline = time.monotonic() + 60
    while time.monotonic() < deadline:
        ev = studio.state().get("eval", {})
        if ev and not ev.get("running") and ev.get("uploaded_serial", ev.get("serial")) == ev.get("serial"):
            break
        time.sleep(0.1)
    else:
        pytest.fail("terrain upload did not complete")
    target = tmp_path / "viewport.png"
    studio.send({"op": "capture", "path": str(target), "width": 960, "height": 540})
    deadline = time.monotonic() + 60
    while time.monotonic() < deadline:
        if target.exists() and studio.state().get("status", "").startswith("captured "):
            break
        time.sleep(0.1)
    assert target.exists(), studio.state().get("status")
    with Image.open(target) as im:
        assert im.size == (960, 540)
        assert max(ImageStat.Stat(im).stddev) > 5, "render is blank or flat"
        baseline = os.environ.get("TERRAFORGE_TEST_BASELINE")
        if baseline:
            with Image.open(baseline) as before:
                assert ImageChops.difference(before.convert("RGB"), im.convert("RGB")).getbbox() is None
    perf = studio.state().get("perf", {})
    assert 0 < perf.get("work_ms", 0) < 1000
    assert perf.get("gpu_ms", -1) >= 0


def test_viewport_latest_terrain(tmp_path):
    directory = os.environ.get("TERRAFORGE_TEST_API")
    if not directory:
        pytest.skip("requires an isolated running studio")
    from mcp_server.studio_api import Studio
    from pathlib import Path
    from PIL import Image, ImageChops
    studio = Studio(directory)
    for resolution in (1024, 64, 256):
        studio.send({"op": "set_resolution", "resolution": resolution})
        deadline = time.monotonic() + 60
        while Path(studio.inbox_path).exists() and time.monotonic() < deadline:
            time.sleep(0.02)
        assert not Path(studio.inbox_path).exists(), "API inbox stopped responding"
    deadline = time.monotonic() + 60
    while time.monotonic() < deadline:
        state = studio.state()
        ev = state.get("eval", {})
        if (state.get("terrain", {}).get("resolution") == 256 and
                not ev.get("running", True) and ev.get("uploaded_serial") == ev.get("serial")):
            break
        time.sleep(0.05)
    else:
        pytest.fail("latest terrain was not uploaded")
    paths = [tmp_path / "first.png", tmp_path / "settled.png"]
    for target in paths:
        studio.send({"op": "capture", "path": str(target), "width": 960, "height": 540})
        deadline = time.monotonic() + 60
        while time.monotonic() < deadline:
            if target.exists() and str(target) in studio.state().get("status", ""):
                break
            time.sleep(0.05)
        assert target.exists()
        time.sleep(0.5)
    with Image.open(paths[0]) as first, Image.open(paths[1]) as settled:
        assert ImageChops.difference(first.convert("RGB"), settled.convert("RGB")).getbbox() is None, "stale terrain replaced the latest result"
