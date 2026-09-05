# AI services in TerraForge

TerraForge can describe a scene, paint a texture or a sky, and build a 3D
model with AI — through a local ComfyUI or Ollama, or through cloud providers
you hold keys for. Every one of these is a tool: it produces a file in your
library, indexed by the asset manager, and only then touches the scene when
you asked it to.

## Where things are configured

**Settings** (menu *File > Settings…*, or `Ctrl+,`):

| Tab | What |
|---|---|
| General | interface, performance, generated-asset folders, notifications |
| Shortcuts | every command with its chord; click to rebind, Reset for the default; conflicts are flagged |
| AI services | default providers for text, images and 3D; a row per provider with key, endpoint override and model |
| ComfyUI | local / cloud address, mode, installation folder, extra workflow folders, poll interval and timeout, *Check connection* |
| Applications | named paths to external programs |
| Asset folders | the folders the asset index watches |

Everything lives in `%LOCALAPPDATA%/GeekatplayTerraForge/config.json`. API
keys are stored protected for the current Windows user (DPAPI); on other
platforms they are marked `plain:` so the file says what it holds. An
endpoint left empty uses the provider's own.

## Providers

| Provider | Purpose | Auth | Notes |
|---|---|---|---|
| Ollama (local) | text, vision | none | `http://127.0.0.1:11434`; vision model from Preferences |
| OpenAI | text, vision; images | Bearer key | chat completions; `gpt-image-1` or `dall-e-3` |
| Anthropic | text, vision | `x-api-key` | messages API |
| Google Gemini | text, vision; images | key in query | `gemini-2.0-flash`; `gemini-2.5-flash-image` for pictures |
| Stability AI | images | Bearer key | v2beta `stable-image/generate/core` (multipart) |
| ComfyUI (local or cloud) | images | none / `X-API-Key` | any API-format workflow; parameters injected by node id + input name |
| Meshy | 3D | Bearer key | text-to-3D (preview, then refine), image-to-3D; GLB |
| Tripo | 3D | Bearer key | text- and image-to-model; GLB |
| Hitem3D | 3D | `ak:sk` or token | image-to-3D, OBJ requested |
| Replicate, fal.ai | upscale (reserved) | Bearer / Key | configured, used by later passes |

## What you can generate

- **Tileable texture** — the prompt is wrapped for a seamless, top-down,
  flat-lit tile; the result can be connected straight to the open material's
  colour channel (or any channel, from a script).
- **360 skydome** — wrapped for a 2:1 equirectangular panorama with the
  horizon at the middle; can be set as the sky backdrop on arrival.
- **Image** — the prompt as written.
- **3D model** — from a prompt and/or a reference picture; imported as a scene
  object on arrival. GLB and OBJ are read natively.
- **A scene, a terrain, an atmosphere from words** — *AI > Describe…*: the
  text model receives every action schema the studio has and answers with an
  ordered list of actions, applied through the same path as the assistant
  bars, the Python API and MCP.

Jobs run in the background (*AI > Jobs…*): state, progress, cancel, and the
result with *Use on material*, *Use as sky* or *Import*.

## ComfyUI workflows

A workflow is any API-format JSON (*Save (API format)* in ComfyUI). TerraForge
finds the inputs to fill by node class and input name: `CLIPTextEncode.text`
(negative if the node's title contains "negative"), the sampler's
`seed`/`noise_seed`, `steps`, `cfg`/`guidance`, `denoise`, the first node with
`width` and `height`, `LoadImage.image`. Empty values leave the workflow's
own defaults. Workflows are looked up by name in `<install>/user/default/
workflows`, in the extra folders from Settings, or the bundled SDXL
`text_to_image`.

## Over the API and MCP

Ops: `ai_generate_texture`, `ai_generate_skydome`, `ai_generate_image`,
`ai_generate_model`, `ai_ask`, `ai_jobs`, `ai_job_cancel`,
`config_set_service`, `config_set_defaults`, `config_status`,
`config_check_comfy`. Every one is an MCP tool with the `studio_` prefix.

```json
{"op":"ai_generate_texture","prompt":"wet mossy granite","apply":true,"material":"Mossy rock"}
{"op":"config_set_service","service":"openai","key":"sk-...","model":"gpt-4o-mini"}
```

## Tests

`ai_services_tests` runs every request builder and response parser on the
providers' documented JSON, the ComfyUI injection and inference on a real
workflow, the prompt templates and the poll backoff — with deliberate
mutations (error payloads, empty replies, a missing node id, truncated JSON)
that must fail with a message. `config_tests` covers the configuration file,
protected secrets and the shortcut table. Nothing in the test suites touches
the network.
