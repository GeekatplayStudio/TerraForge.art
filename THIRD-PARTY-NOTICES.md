# Third-party notices

TerraForge itself is licensed under the [PolyForm Noncommercial License
1.0.0](LICENSE). The components below are **not** — each keeps its own
licence, and each of those licences is permissive, so they may be used in
TerraForge whether your copy is the free noncommercial one or a purchased
commercial one.

Every licence here requires that its copyright notice travels with the
software. This file is how that requirement is met; it ships in the
installer and the packaged build.

| Component | Used for | Licence | Where the text lives |
| :--- | :--- | :--- | :--- |
| [Dear ImGui](https://github.com/ocornut/imgui) | the entire user interface | MIT | `external/imgui/LICENSE.txt` |
| [imgui-node-editor](https://github.com/thedmd/imgui-node-editor) | the node graph canvas | MIT | `external/imgui-node-editor/LICENSE` |
| [GLFW](https://www.glfw.org/) | windows, input, OpenGL contexts | zlib/libpng | `external/glfw/LICENSE.md` |
| [GLM](https://github.com/g-truc/glm) | vector and matrix maths | The Happy Bunny License or MIT | `external/glm/copying.txt` |
| [glad](https://github.com/Dav1dde/glad) | OpenGL function loading | (WTFPL OR CC0-1.0) AND Apache-2.0 | SPDX headers in `external/glad/` |
| [nlohmann/json](https://github.com/nlohmann/json) | every JSON document we read or write | MIT | SPDX header in `external/json.hpp` |
| [stb_image / stb_image_write](https://github.com/nothings/stb) | image loading and saving | public domain (or MIT) | headers in `external/` |
| [miniz](https://github.com/richgel999/miniz) | zip and PNG compression | MIT / public domain | header in `external/miniz/` |

## Downloaded content

Material libraries fetched from [ambientCG](https://ambientcg.com) are
**CC0 1.0** (public domain dedication) and carry no obligations. They are
downloaded at the user's request and are not redistributed with TerraForge.

## What is deliberately absent

No GPL or LGPL code is present anywhere in TerraForge, and none may be
added. A copyleft licence requires that downstream users receive the same
freedoms, including the freedom to use the software commercially —
TerraForge's noncommercial licence cannot grant that, so the two are
incompatible. This rules out, among others, Blender and any of its addons
as *sources of code*, however freely they may be read as design references.

Anything added in future must be MIT, BSD, Apache-2.0, zlib, MPL-2.0,
CC0 or public domain, and must be listed in this file in the same commit
that adds it.
