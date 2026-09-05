# Licensing: what TerraForge is, and what it may take in

This file exists so a licence question is answered from evidence rather than
memory. Every claim below was read from the licence file itself, on the date
given, at the version given.

**Last verified: 2026-09-04.**

---

## 1. TerraForge itself

| | |
| :--- | :--- |
| Licence | **PolyForm Noncommercial License 1.0.0** ([`LICENSE`](../LICENSE)) |
| SPDX | `PolyForm-Noncommercial-1.0.0` |
| Decided | 2026-09-04, commit `111deec` |
| Text | Reproduced verbatim from `polyformproject/polyform-licenses` tag `1.0.0` |
| Commercial use | Separate paid licence — see [`COMMERCIAL.md`](../COMMERCIAL.md) |

Free to use, modify, fork and share for noncommercial purposes; commercial use
requires a licence from Geekatplay Studio. Charities, educational
institutions, public research bodies, public safety and health organisations,
environmental organisations and government institutions are permitted by name
in the licence text, whatever their funding.

**It is not open source, and we do not call it that.** The Open Source
Definition forbids discriminating against a field of endeavour, and a
noncommercial restriction does exactly that. The correct word is
*source-available*.

---

## 2. The rule that follows

> **Permissive licences only. No GPL, no LGPL, no AGPL — ever.**

The reason, in one paragraph, so it never has to be re-derived:

Copyleft requires that everyone who receives the program also receives the
freedom to use it for **any purpose, including commercially**, along with the
source. Our licence sells that particular freedom. A work combining the two
cannot be distributed under our terms without breaking the copyleft licence,
and cannot be distributed under copyleft terms without destroying the
commercial licence. The incompatibility is structural — it is not a matter of
attribution, notices or paperwork, and no amount of care in how the code is
arranged inside the repository changes it.

**Acceptable:** MIT, BSD (2- and 3-clause), Apache-2.0, zlib/libpng, MPL-2.0,
CC0, public domain, Boost Software License, Unlicense.

**Not acceptable:** GPL (any version), LGPL (any version), AGPL, SSPL,
Elastic License, and any licence with a field-of-use restriction of its own
that conflicts with ours.

Anything added must be listed in
[`THIRD-PARTY-NOTICES.md`](../THIRD-PARTY-NOTICES.md) **in the same commit**
that adds it.

---

## 3. What is in the build today

| Component | Version checked | Licence | Verified from |
| :--- | :--- | :--- | :--- |
| Dear ImGui | 1.93.0 WIP (docking) | MIT | `external/imgui/LICENSE.txt` |
| imgui-node-editor | master | MIT | `external/imgui-node-editor/LICENSE` |
| GLFW | 3.6.0 | zlib/libpng | `external/glfw/LICENSE.md` |
| GLM | master | Happy Bunny **or** MIT | `external/glm/copying.txt` |
| glad | 2.x generated | (WTFPL OR CC0-1.0) AND Apache-2.0 | SPDX headers in `external/glad/` |
| nlohmann/json | 3.12.0 | MIT | SPDX header in `external/json.hpp` |
| stb_image / stb_image_write | v2.30 | public domain or MIT | headers in `external/` |
| miniz | 3.0.0 | MIT / public domain | `external/miniz/miniz.h` |
| Manifold | v3.5.2 | Apache-2.0 | `external/manifold/LICENSE` |
| QuadriFlow | commit 810b7a09 | BSD-3-Clause | `external/quadriflow/LICENSE.txt` |
| Eigen (QuadriFlow only) | 3.4.0 | MPL-2.0, built `EIGEN_MPL2_ONLY` | `external/eigen/COPYING.MPL2` |
| pcg32 (QuadriFlow only) | master | Apache-2.0 | header comment |
| ambientCG material sets | downloaded on request | CC0 1.0 | ambientcg.com terms |

All permissive. None impose obligations beyond carrying their notice, which
`THIRD-PARTY-NOTICES.md` does.

---

## 4. The mesh module, and the question that produced this file

The mesh module (`engine/mesh_*.cpp`) is a port of **Meshwright** — Geekatplay
Studio's own Python application, MIT licensed — so its own code raises no
question. Meshwright's *optional engines* did.

On 2026-09-04 the claim "MeshLab, MeshFix and QuadriFlow are all GPL" was
recorded in this repository. **It was wrong**, and it was corrected in commit
`35c3133`. Here is what each one actually is, read from the licence file:

| Library | Version / ref | Licence, as the file states it | Verdict |
| :--- | :--- | :--- | :--- |
| [**Manifold**](https://github.com/elalish/manifold) (`manifold3d`) | v3.5.2, released 2026-06-27 | **Apache License 2.0** (`LICENSE`, verified 2026-09-04) | **Usable** — adopted, see §5 |
| [**QuadriFlow**](https://github.com/hjwdzh/QuadriFlow) | commit `810b7a0967c3`, 2019-12-07 (no tagged releases) | **BSD 3-Clause** in `LICENSE.txt`; the README calls it "MIT". Both are permissive, so the discrepancy does not change the verdict — treat the file as controlling and reproduce the BSD-3 notice | **Usable** — adopted, see §5 |
| [**fast-simplification**](https://github.com/pyvista/fast-simplification) | 0.1.7+ | **MIT** (`LICENSE`, verified 2026-09-04) | Usable; not needed, we have our own quadric collapse |
| [**PyMeshLab / MeshLab**](https://github.com/cnr-isti-vclab/PyMeshLab) | 2023.12+ | **GPL-3.0** (`LICENSE`, verified 2026-09-04) | **Excluded** |
| [**MeshFix**](https://github.com/MarcoAttene/MeshFix-V2.1) (`pymeshfix`) | 0.17+ | **GPL-3.0**, *and* dual-licensed: the author states commercial use requires an agreement with the authors and IMATI-GE/CNR | **Excluded** unless a commercial licence is bought — see §6 |

### QuadriFlow's own dependencies

QuadriFlow builds against Boost (Boykov max-flow), Eigen and Lemon. Eigen is
MPL-2.0 **except** its Sparse Cholesky code, which is LGPL; QuadriFlow ships
a CMake flag, `-DBUILD_FREE_LICENSE=ON`, that swaps it for an MPL-2.0 Sparse
LU solver (slightly slower). **That flag is mandatory for us.** Boost and
Lemon are both under the Boost Software License, which is permissive.

---

## 5. What was decided, and when

**2026-09-04.** Keep PolyForm Noncommercial. Add the two permissive engines
behind an adapter; do not take the GPL ones in any form.

- **Manifold (Apache-2.0)** — guaranteed-manifold solid reconstruction. This
  is the strongest single repair stage: it rebuilds a surface as a solid that
  is manifold by construction, which also resolves most of the
  self-intersection cases MeshFix existed for. It has **no required
  dependencies** (its own README, verified 2026-09-04), which is why it went
  first.
- **QuadriFlow (BSD-3)** — quad retopology. Our quadric edge collapse thins
  triangles; it does not rebuild a surface as clean, curvature-aligned quads,
  and nothing else we have does either.

Both sit behind `engine/gpx/mesh_engines.hpp`, compiled in when available and
reported honestly when not: a missing engine disables its stage and says so,
it never silently degrades to something weaker.

### What actually shipped, 2026-09-04

**Manifold v3.5.2 — done.** Cloned by `scripts/get_deps` at that exact tag,
built static with `MANIFOLD_CROSS_SECTION=OFF` (which is what would otherwise
pull in Clipper2), `MANIFOLD_PAR=OFF`, `MANIFOLD_TEST=OFF`,
`MANIFOLD_PYBIND=OFF`. It compiled against our own toolchain (mingw-w64 GCC
16.1, CMake 4.3) in 7 seconds and produced a 2.1 MB static library. It is a
real capability gain, not a formality: overlapping shells — two closed pieces
running through each other, which nothing in our own stages can even detect
as a defect — are unioned into one solid, verified by a test that checks the
result encloses the union's volume (1.875 for two unit cubes overlapping by
half) rather than the sum of both.

**QuadriFlow — done, and the Boost problem turned out to be smaller than it
looked.** `flow.hpp` carries four max-flow solvers: Boykov (Boost.Graph),
network simplex (Lemon), Gurobi (commercial) and `ECMaxFlowHelper`, which is
QuadriFlow's own and needs nothing at all — and which the code already
selects for small problems. `scripts/patch_quadriflow.py` removes the first
two and aliases their names to the third, so the rest of QuadriFlow needs no
edit. That leaves **Eigen** as the only dependency, header-only, built with
`EIGEN_MPL2_ONLY` so its LGPL Sparse Cholesky corner never enters. Two
further things the checkout needs: `3rd/pcg32` is an empty submodule
directory (cloned separately, Apache-2.0), and `dedge.cpp` reaches for an
MSVC intrinsic on any `_WIN32` build, which MinGW is not.

**What the patch costs, stated plainly:** EC and Boykov both compute an exact
maximum flow, and the maximum-flow *value* is unique, but a different optimal
flow can give a slightly different quad layout on the same input, and EC is
slower on large problems. Nothing is approximated; the layout is one valid
solution rather than another.

Measured on our own toolchain: a 1,600-triangle sphere rebuilt to 810
triangles (405 quads, 407 vertices) with a worst radius error of 0.0067 —
a rebuilt surface, not a thinned one, and still the same sphere.

The upstream sources are **not** committed to this repository (`external/` is
ignored); `scripts/get_deps` fetches and patches them, and the patch script
is idempotent and readable.

### Options considered and rejected — so they are not re-litigated

| Option | Why not |
| :--- | :--- |
| Relicense TerraForge as GPL-3 | Gains MeshLab and MeshFix, loses the paid commercial tier entirely: anyone could use it commercially for free provided they pass on source. Also close to irreversible — once other people contribute under GPL, reverting needs every contributor's permission. |
| Dual-license GPL + commercial (the Qt model) | Only works for code we own. We cannot sell a commercial licence covering MeshLab's copyright, so the paid edition would ship without those stages anyway — where we already are, plus two editions to maintain. |
| Ship GPL tools as a separate executable we shell out to | The "ffmpeg arrangement". Defensible — separate process, separate address space, data over files — but the FSF reads "one program" more broadly than most counsel does, and it would mean distributing GPL binaries with their own source obligations. Not worth the ambiguity for a capability we can get permissively. |
| Vendor GPL code quietly, without notices | Not an option. It is a licence violation and it would poison the whole codebase. |

---

## 6. Open items

- **MeshFix commercial licence.** MeshFix is explicitly dual-licensed and its
  authors offer commercial terms through IMATI-GE/CNR. If self-intersection
  repair beyond what Manifold gives us ever becomes worth paying for, that is
  a legitimate route that keeps our licence untouched. Nobody has been
  contacted; this is a note, not a negotiation.
- **MeshLab** offers no commercial route. It stays out.

---

## 7. How to check a new dependency

1. Read the licence **file in the repository**, not the README, not a badge,
   and not a package index page. Where they disagree, the file in the source
   tree is what was actually granted — QuadriFlow above is exactly that case.
2. Check the dependency's *own* dependencies. A permissive library that
   requires a GPL one at build time is a GPL problem wearing a hat, and Eigen
   above shows that this can come down to a single CMake flag.
3. Record it here with the version, the licence, where you read it and the
   date.
4. Add it to `THIRD-PARTY-NOTICES.md` in the same commit.
5. If the licence is anything other than the accepted list in §2, the answer
   is no — take it to the licence holder for other terms, or write the
   capability ourselves.

*This is an engineering record, not legal advice. If real money or a
distribution deal depends on one of these questions, have a lawyer read it.*
