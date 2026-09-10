# Objects and modelling — roadmap

## Purpose

Create and edit object shapes: primitives, imported meshes, booleans and
metaballs, deformers, displacement, sculpting; repair, reduce and export
them; ground them on the terrain; give each a material and, where wanted, a
population.

## Where it stands (2026-09-10)

- **Primitives**: cube, sphere, plane, cylinder, cone with `primitive_detail`
  (`studio/scene_primitives.cpp`); every component made through
  `component_new.cpp` (object + driver node + material).
- **Meshwright port** (`engine/mesh_*.cpp`): OBJ, STL, PLY, OFF, glTF/GLB,
  binary FBX import/export; analysis with a readiness score; repair; quadric
  reduce; split shells; retopo (Manifold); solidify; the Mesh tools window.
- **CSG and metaballs** (`studio/scene_csg.cpp`, `engine/mesh_csg.cpp`).
- **Deformers** (`engine/gpx/deform.hpp`): twist, bend, skew, taper —
  identical in the vertex shader and the exports; gizmos.
- **Displacement**: material `displacement` channel per layer; field graphs
  on planets; `surface_features.cpp` applies material displacement and the
  ground mould after placement.
- **Sculpt** (`studio/sculpt.cpp`): nine brushes into a `TerrainSculpt`
  delta layer; height painting (`paint_canvas*.cpp`).
- **Grounding**: `place_on_terrain`, `set_ground` (lock, offset, margin,
  blend, sink, lift, dig), `TerrainImprint`.
- Tests: `test_mesh.cpp` (16), `test_mesh_csg.cpp`, `test_imprint.cpp`,
  `test_scene_tree.cpp`.

## Integrations

| Direction | With | Through |
|---|---|---|
| out → | Terrain | `TerrainImprint` moulds the ground; the `objects` mask |
| in ← | Materials | `material_node`; `MaterialSource` reuse |
| in ← | Distribution | instances of a mesh; LOD chains; billboards |
| in ← | Planets | placed against the planet's surface (`scene_planet_of`) |
| in ← | Atmosphere | fog on meshes; volumetric materials march the object's box |
| out → | Render | OBJ + matrices + instances to every engine |
| in ← | Animation | transform, deformers, colour, visibility tracks |

## Scripting surface

Ops: `add_primitive`, `import_object`/`import_mesh`, `mesh_*` (eight),
`combine_objects`, `metaball`, `place_object` (transform + deformers),
`place_on_terrain`, `set_ground`, `set_locked`, `delete_object`,
`assign_material`, `set_scatter`. MCP: one tool each (two added in this
audit). Guide: recipes 4, 5.

## Gaps (verified)

1. `ObjectGroup`, `BooleanObject`, `TerrainObject` nodes are `[Planned]`;
   booleans exist only as a destructive op on the selection.
2. No object-level sculpting or displacement painting (terrain only).
3. No parametric modelling: no extrude/loft/lathe/sweep, no splines to
   meshes beyond paths on the terrain.
4. No procedural buildings/roads/walls (paths exist as terrain carvers).
5. Metallic/AO not consumed; no per-object material override of layers.
6. Viewport point lights cast no shadows on objects (lighting.md).

## Roadmap

### Phase 1 — booleans and groups as nodes
- `BooleanObject` node: live union/intersect/difference between driven
  objects (Manifold), re-evaluated on edit, exported as one mesh.
  `ObjectGroup` node with a shared transform. Accept: a house from a cube
  minus a cube, moved as a group, exports as one manifold mesh.
- `mesh_retopo`/`mesh_solidify` in the Mesh tools window with their dials
  (they are ops only today).

### Phase 2 — surfaces you can shape
- Object displacement painting (a `delta` field on the mesh's UV/triplanar
  space, brushes as on the terrain); subdivision (Loop/Catmull–Clark) for
  displacement to have somewhere to go.
- Splines → meshes: extrude/loft/sweep along `PathSpline`; walls, fences,
  roads with profiles.
- Contact blend on bases (materials.md).

### Phase 3 — procedural structures
- Rule-based buildings (footprint → floors → roof → materials), placed by
  the distribution module along paths and in zones; bridges over rivers.
- Physics settle (rocks roll into place) as a one-shot on scatter clouds.

Owner files: `studio/scene_primitives.cpp`, `studio/scene_csg.cpp`,
`engine/mesh_*.cpp`, `studio/panel_mesh.cpp`, `studio/sculpt.cpp`,
`studio/imprint.cpp`, `engine/nodes/nodes_scene.cpp`.
