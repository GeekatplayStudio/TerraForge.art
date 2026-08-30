"""Geekatplay TerraForge — AI texture generation bridge.

Generates tileable terrain material textures through a locally running
ComfyUI server (SDXL or any checkpoint the user has installed), then the
result can be pulled into the app with the TextureFile node and converted
to PBR maps with the AlbedoToPBR node.

RTXNTC was evaluated and rejected: it is a texture *compressor* (DX12/
Vulkan + CUDA + MSVC only), not a generator. ComfyUI + SDXL is the
practical local pipeline.

Usage:
    python -m orchestrator.texture_ai "mossy alpine rock, wet" out_rock.png
Requires: a ComfyUI instance on http://127.0.0.1:8188 (default install).
"""
from __future__ import annotations

import json
import sys
import time
import urllib.request
import urllib.error

COMFY_URL = "http://127.0.0.1:8188"


def _post(path: str, payload: dict) -> dict:
    req = urllib.request.Request(
        COMFY_URL + path,
        data=json.dumps(payload).encode(),
        headers={"Content-Type": "application/json"},
    )
    with urllib.request.urlopen(req, timeout=30) as r:
        return json.loads(r.read())


def _get(path: str) -> bytes:
    with urllib.request.urlopen(COMFY_URL + path, timeout=30) as r:
        return r.read()


def comfy_available() -> bool:
    try:
        _get("/system_stats")
        return True
    except Exception:
        return False


def build_workflow(prompt: str, seed: int = 0, size: int = 1024,
                   checkpoint: str = "sd_xl_base_1.0.safetensors") -> dict:
    """Minimal txt2img graph. Tileability comes from the prompt plus the
    app-side Transform(mirror) node; for true circular-padding tiling install
    a seamless-tile custom node in ComfyUI and extend this workflow."""
    full_prompt = (
        f"top-down texture of {prompt}, seamless tileable material, "
        "photorealistic, 4k detail, even lighting, no shadows of objects"
    )
    return {
        "1": {"class_type": "CheckpointLoaderSimple",
              "inputs": {"ckpt_name": checkpoint}},
        "2": {"class_type": "CLIPTextEncode",
              "inputs": {"text": full_prompt, "clip": ["1", 1]}},
        "3": {"class_type": "CLIPTextEncode",
              "inputs": {"text": "blurry, seams, border, watermark, text",
                         "clip": ["1", 1]}},
        "4": {"class_type": "EmptyLatentImage",
              "inputs": {"width": size, "height": size, "batch_size": 1}},
        "5": {"class_type": "KSampler",
              "inputs": {"model": ["1", 0], "positive": ["2", 0],
                         "negative": ["3", 0], "latent_image": ["4", 0],
                         "seed": seed, "steps": 28, "cfg": 7.0,
                         "sampler_name": "euler", "scheduler": "normal",
                         "denoise": 1.0}},
        "6": {"class_type": "VAEDecode",
              "inputs": {"samples": ["5", 0], "vae": ["1", 2]}},
        "7": {"class_type": "SaveImage",
              "inputs": {"images": ["6", 0], "filename_prefix": "terraforge"}},
    }


def generate_texture(prompt: str, out_path: str, seed: int = 0,
                     size: int = 1024, timeout_s: float = 600.0) -> bool:
    if not comfy_available():
        print("ComfyUI is not running on", COMFY_URL)
        print("Install/start ComfyUI, then re-run. The app works without it —")
        print("procedural TerrainTexture and TextureFile nodes stay available.")
        return False
    wf = build_workflow(prompt, seed=seed, size=size)
    resp = _post("/prompt", {"prompt": wf})
    pid = resp["prompt_id"]
    deadline = time.time() + timeout_s
    while time.time() < deadline:
        hist = json.loads(_get(f"/history/{pid}"))
        if pid in hist and hist[pid].get("outputs"):
            images = hist[pid]["outputs"].get("7", {}).get("images", [])
            if images:
                img = images[0]
                data = _get(f"/view?filename={img['filename']}"
                            f"&subfolder={img.get('subfolder', '')}&type=output")
                with open(out_path, "wb") as f:
                    f.write(data)
                print("saved", out_path)
                return True
        time.sleep(2.0)
    print("timed out waiting for ComfyUI")
    return False


if __name__ == "__main__":
    if len(sys.argv) < 3:
        print(__doc__)
        sys.exit(2)
    ok = generate_texture(sys.argv[1], sys.argv[2],
                          seed=int(sys.argv[3]) if len(sys.argv) > 3 else 0)
    sys.exit(0 if ok else 1)
