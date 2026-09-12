# Volumetrics: what we do, what the field does, what to take next

Written 2026-09-08 from a survey of the mid-2026 state of the art (the Nubis
lineage that Unreal Engine 5's Volumetric Cloud system descends from, NanoVDB,
volumetric Gaussian splatting, and the WebGPU/Vulkan compute-shader
pipelines), set against what TerraForge already has. This is the record so
the question is not re-researched, and the plan for the parts worth taking.

## What TerraForge has (measured, not claimed)

One radiative-transfer model shared by the fog, the clouds and volumetric
materials — `FOG_FN` in `studio/shaders_terrain.cpp`, `march_clouds` in
`studio/shaders_sky.cpp`, and the `u_v_*` march at the top of `FS_MESH` in
`studio/shaders_scene.cpp`:

| Technique | Ours | Notes |
|---|---|---|
| Beer–Lambert extinction | yes | everywhere |
| Henyey–Greenstein phase | yes | clouds: dual lobe (0.75 / −0.25); fog and materials: one lobe, anisotropy a setting |
| Sun shadow march | yes | clouds 5 steps, fog 3, materials 4 |
| Multiple scattering (octaves) | yes | clouds, capped at 4, normalised so energy is redistributed not invented |
| Perlin–Worley base + Worley detail erosion | yes | two 3D textures, `u_cl_shape` / `u_cl_detail` (`cloud_noise.cpp`), Schneider/Guerrilla remap |
| Height gradient by cloud type | yes | stratus / cumulus / cumulonimbus with anvil |
| Blue-noise ray jitter | yes | static 64×64; not animated because there is no TAA |
| Early exit at ~1% transmittance | yes | every march |
| Empty-space skipping / adaptive step | **no → added** | the cloud march stepped uniformly; now coarse in clear air, fine on contact |
| Depth termination of the cloud march | partial | clouds draw in the sky pass behind the terrain; a cloud *in front of* a mountain is not composited against its depth |
| Multiple cloud layers | yes | every CloudLayer node is a deck of its own, marched far to near, eight beside the two the settings carry; authored as objects in the scene tree under an Atmosphere (`studio/scene_air_layers.cpp`), not as a list on a panel |
| Painted cloud shape | yes | a layer may name an image, laid flat over the land: white is cloud, black is clear, mid grey leaves the coverage to decide - the same form the procedural weather takes, so one strength dial fades between them (`studio/cloud_shape_map.cpp`). Held still while the cloud's own texture drifts through it, and read by mip level like every other lookup in the march |
| Temporal reconstruction (quarter-res + reprojection) | no | needs motion vectors and a history buffer; the governor's render scale does the resolution half without the reprojection |
| Density baked to a 3D texture per weather change | no | the shape/detail textures are baked once; density is remapped per sample (cheap) |
| Planet-wrapped layers (spherical shells) | yes | `layer_span` in shaders_sky.cpp: the band between two shells about the world's centre or a ring's axis, cut at a ring's rim; a flat world keeps the slab |
| VDB / NanoVDB import | no | see below |
| Gaussian-splat media | no | see below |
| Offline: true medium in a path tracer | yes | Mitsuba 3 `volpath`, homogeneous medium behind a null BSDF, terms converted from the material |

## What the survey says, and the decision on each

**Nubis / UE5 architecture.** Ray-march a proxy, skip empty space, Worley
erosion, multi-scatter octaves, temporal upsampling. We have all of it except
empty-space skipping (now added) and temporal upsampling. Temporal
reconstruction is the single largest remaining win for cloud cost and is
*not* taken yet: it needs per-pixel motion vectors from the camera and a
history buffer per view, and a static blue-noise dither would then have to
become an animated one. Worth doing when the clouds are the frame's cost; the
watcher (`perf_watch`) will say when they are.

**NanoVDB (Apache-2.0 since OpenVDB 10).** The right route for *authored*
volumes — a hero cloud, a smoke sim from Blender or Houdini — flattened into
a GPU buffer and marched with its HDDA. Acceptable under `docs/LICENSING.md`
§2. Not adopted now: nothing in TerraForge produces or reads `.vdb`, and a
dependency without a use is a cost without a benefit. When it is wanted, the
integration point is a `VolumeFile` scene object whose material is a Volume
(the `u_v_*` march already handles a bounded medium; NanoVDB replaces
`fog_noise3` with a grid lookup). Instancing scaled/rotated grids across the
sky, as the survey describes, is how "infinite" authored clouds would be done.

**Volumetric Gaussian splatting.** Fast, but the lighting is baked into the
splats: no dynamic sun, no shadows from the terrain, projection artefacts on
fly-through, VRAM bloat for animation. Everything here is lit by a sun that
moves and a terrain that shadows. Not taken. Revisit only for captured,
static effects.

**Procedural compute-shader density cache.** Baking density into a 3D
texture every N frames and sampling it in the march, with a seamlessly
tiling Worley (modular hash). Our shape/detail textures already tile and are
baked once; per-sample remap is cheap compared with the light march. The
gain would come only with temporal reconstruction. Deferred with it.

**Infinite fly-through.** Tiled 3D textures with scrolled UVs (we do — the
wind offsets the sample position and the textures repeat), height-gradient
layers from one texture (we do, per type), depth pre-pass termination
(partial, see above). Multiple layers at different heights is what makes a
sky read as a sky: added as a second layer; a list of N layers is the natural
generalisation and is where "multiple atmospheres" goes.

## Atmospheres: global, and wrapped to a planet

Asked for: an atmosphere component that can be global, or wrapped to the
planet's surface (radius, height), with several of them. The pieces exist
apart: the tile renderer's fog and cloud slab are "global" in tile space; the
planet renderer draws its own sky. The design that unifies them:

1. An `Atmosphere` layer = { kind (fog | cloud), height range, coverage,
   density, albedo, absorption, anisotropy, type, wind } — the fields the
   fog and cloud settings already have, as a list.
2. A layer is either **flat** (height above the tile, what we have) or
   **shell** (radius from the planet's centre, so it wraps). The march is the
   same; only the entry/exit test differs — slab vs concentric sphere pair —
   and `pl_sphere_place` already maps tile positions onto the planet.
3. The sky pass marches the layers back to front from the camera.

The second cloud layer added now is step 1 with N = 2 and flat only. The
shell case shipped in phase 36 (the home world only; planets in the sky
still get a limb-glow term - see docs/roadmaps/planets.md).

## Cost rules (AGENTS.md "Participating media")

Every march has a step ceiling and stops at 1% transmittance. A volume is
drawn after every surface, blended, without writing depth. Density on a
material is per box unit; the export converts. A material with density 0 is
a surface, exactly as before.
