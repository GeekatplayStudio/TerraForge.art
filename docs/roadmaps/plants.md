# Plants — roadmap

## Purpose

Believable vegetation for every landscape the studio makes: a library of
plants ready to place and scatter, and a plant editor where a plant is not a
mesh found somewhere but a set of growth rules - a trunk, the branches it
carries and how they are spread along it, leaves, age, season, health and
wind - evaluated into a textured, wind-ready plant with a new individual from
every seed, in its own workspace.

## Where it stands (2026-09-11)

**The Plants workspace** (`WS_PLANTS`) holds two windows: the library
(studio/panel_plants.cpp) and the Plant Editor
(studio/panel_plant_editor.cpp).

**The library** has four shelves (studio/plant_library.cpp): the seven
built-in kinds (studio/scene_plants.cpp), the free CC0 Poly Haven scans
`orchestrator/plant_fetch.py` downloads on request, the user's own recorded
models, and now the species grown from rules and saved from the editor
(`<data_dir>/library/plants/species/<id>/`). Placement (studio/plant_place.cpp)
stands any of them on the ground the viewport draws, with the node that
drives it; Scatter binds it to a `ScatterPoints` node.

**The species engine** (engine/plant/, public face engine/gpx/plant.hpp) is
the plant tool's model, built from its manual (docs_private):

- **17 node types** in the `Plant` category (engine/nodes/nodes_plant.cpp):
  PlantSpecies, PlantSegment, PlantLeaf, PlantCutoutLeaf, PlantWarpboard,
  PlantObject, PlantUrchin, PlantHydra, PlantBall, PlantFlower, PlantGrowth,
  PlantRepeat, PlantChildSelect, PlantBias, PlantMaterial, PlantVariable,
  PlantVector. Links carry `DataType::Plant` from a part to the part it grows
  on, so the root is a sink and any edit anywhere in the species reaches it.
- **Two new attribute types** (engine/gpx/attribute.hpp): `Curve` (a drawn
  function, with weighted alternatives one of which a seed picks) and
  `Random` (a value, a spread with three modes, a scope, and the two shaping
  curves). Both serialize, both have Properties rows
  (studio/attr_widgets_plant.cpp), both are reachable from `set_attr`.
- **The parameter tables** (engine/plant/plant_schema*.cpp): every parameter
  of every part, group by group as the manual lays them out, with its type,
  range, default, choice list and tooltip - about 600 keys.
- **The walk** (plant_eval.cpp, plant_eval_walk.cpp): levels of detail,
  presence by season, health and age, the selectors, the loops, the caps,
  and the parts that place their own children.
- **Attachment** (plant_attach.cpp): count modes, soft insert, ranges,
  density curves, arrangements, whorls, roll and coil, tropism cones,
  pruning and cuts, inherited density, scale and sap.
- **The segment** (plant_axis.cpp, plant_segment.cpp, plant_segment_extra.cpp,
  plant_blades.cpp): the axis walked under gravity, tropism, seven kinds of
  bias, perturbation and its children's kinks; sections, radius profiles,
  bark displacement, caps, root flares and blades.
- **The growth simulation** (plant_growth*.cpp): buds, light, shedding,
  apical dominance, phyllotaxis, the pipe model, bottom and profile cuts.
- **Materials** (plant_materials.cpp) with seasonal looks, and **pictures
  made from rules** (plant_textures*.cpp): 13 leaf shapes, a petal, 8 bark
  kinds with normal maps, and a cut-out tracer.
- **Wind** (plant_wind.cpp): weights baked per vertex and one function that
  moves them, with its GLSL twin spliced into the mesh shader.
- **Species files** (plant_species_io.cpp), **node presets**
  (plant_presets.cpp), **descriptions and the plants it knows by name**
  (plant_describe.cpp, plant_knowledge.cpp), **archetypes**
  (plant_archetypes*.cpp), and **export** (plant_export*.cpp: OBJ + MTL and
  binary glTF).

**The studio side**: the species in the scene (scene_plants_species.cpp: the
object its root drives, the wind and tint vertex streams, its pictures), the
Plant Editor with its parts tree, presets and published parameters, the
species layer (plant_species*.cpp) and eleven operations mirrored as MCP
tools and shown to the assistant in its own `AiDomain::Plant`.

Tests: `plant_tests` (tests/cpp/test_plant.cpp, test_plant_species.cpp) for
curves, random parameters, growth, determinism, seasons, wind, the knowledge
of plants, every archetype, species files, every texture and the exporters;
`test_plant_library.cpp` and `test_plant_fetch.py` for the library; the node
contract battery covers every new node.

## Integrations

| Direction | With | Through |
|---|---|---|
| out → | Objects | the object a species root drives (`driver_node`), like every component |
| out → | Distribution | Scatter binds a species' plant to `ScatterPoints`; several individuals share one species |
| in ← | Terrain | the ground and its micro-relief a plant stands on |
| out → | Render | parts with pictures and alpha; the wind baked per vertex |
| in ← | Materials | PlantMaterial nodes, or pictures made from rules |
| in ← | Atmosphere | the wind's strength and direction |
| out → | Animation | season, age, health and wind are ordinary animatable attributes |

## What a plant looks like now (checked in the app, 2026-09-11)

Typing a plant's name into the Plants tab grows it: `plant_species_new`
with `"words": "oak"` builds the species graph, grows it, stands it on the
ground and names the object. A plain oak comes out 22 m with a buttressed
trunk, a fork into limbs, two levels of branching, textured bark and green
leaves, at ~21 fps with 4.5 M triangles in the viewport. Six things were
wrong when it was first put in front of a camera, and all six are fixed:

1. **The app ran out of memory** growing a mature tree. A plant's mesh
   carries one part per primitive - 318,000 on that oak - and the scene
   copied the whole leaf picture, and uploaded its own GL texture, for every
   one of them. The scene object now holds one part per **material**
   (`studio/scene_plants_species.cpp`): four pictures, four draws.
2. **Every wood part but the trunk wore a flat fallback brown.** An
   archetype hangs its bark on the part it is thinking about; the rest were
   left bare. `dress_bare_wood` now runs when an archetype finishes and puts
   the species' bark on any wood with nothing on its material slot.
3. **A bark made from rules came out near black**: the picture is drawn with
   the material's own colour between its cracks, and the material's colour
   was then multiplied in on top of it. A picture now carries its colour
   alone (`plant_materials.cpp`).
4. **Every branch attachment was a sail.** The blending fillet read its
   width as a share of the child's *length*, so a nine-metre branch flared
   out to the trunk's girth over four and a half of them. It is a width in
   the parent's radii now, which suits a trunk and a twig alike.
5. **A named plant grew a quarter of its size.** Maturity was `age/max_age`,
   so a 140-year oak of a 600-year species was 23% grown. A plant is
   full-grown long before it dies: `plant_maturity` measures growth against
   a little under half the span, and `plant_size_at` says what size that is.
   Both the engine and the archetype builders read the same two functions.
6. **The placement threw the species' own scale away**, overwriting it with
   the placement request's size, so a described plant was the wrong size the
   moment it was placed. The placement's size belongs to the object.

Then the wind was turned on, and the crown came apart - branches and leaves
floating free of the wood, jittering. Four more faults, all of them in what
the wind does rather than where the parts are (at rest not one leaf of 92,112
stood off its twig by more than 8 mm):

7. **Every part drew its own wind phase.** The displacement depends on the
   phase, so a leaf and the twig it is nailed to swung in opposite
   directions. The phase is now where the gust has got to - a function of
   distance out along the wood - which is continuous across every joint by
   construction, while two branches whose paths differ still sway apart.
8. **A leaf's flutter had a floor under it**, so the hook itself rippled while
   the wood stood still: about 11 cm of it on a 22 m tree. Flutter is zero
   where a part meets its parent now, and flexibility shapes how quickly it
   builds along the blade instead.
9. **The child re-derived the parent's bend** from a parameter read at a
   different point along it, and differed by about a tenth. The parent's axis
   samples carry the wind now; a socket copies them, interpolating between
   samples exactly as it interpolates position and radius, and the child takes
   the socket verbatim. Of the leaves genuinely touching wood, 99.3% hold in a
   full gale; the rest are leaves whose nearest wood is a *neighbour's* twig,
   which is supposed to move apart.
10. **Flutter was scaled by the plant's height**, not the part's own size, so
    every 11 cm leaf on a 22 m oak was smeared over 22 cm. Builders bake the
    amplitude in metres now and the finalisation converts it: a leaf is
    stretched 1.26x its own size in a gale, down from 2.12x.
11. **A leaf inherited its twig's shrink**, three levels deep, so an 11 cm oak
    leaf drew at 3.9 cm. Wood inherits its parent's scale; a leaf, flower and
    fruit do not.

`fit_to_height` closes the loop: the species is grown a few times at the
coarsest meshing and the root's scale corrected by what it actually made, so
the height in the description is the height that stands up (within 3%).
Height does not follow scale in step - a part counted per metre carries more
children when it is longer - so the step is damped and it takes two or three
passes.

## A wood (2026-09-12)

`plant_forest` plants one: `{"op":"plant_forest","words":"scots pine",
"individuals":5}` grows the species five times, each with its own seed so no
two trees are alike, and binds all five to one `EcosystemLayer` that splits
its points among them. Every copy is instanced, so a wood of four thousand
trees costs what those five cost plus a transform each - 21 fps with 4,212
scots pines around the camera. The rules come with it: an altitude band keeps
them out of the water, a slope band off the cliffs, clumping gathers them
into stands with clearings.

Scattering one grown tree instead - which is what the Plants tab did before -
gives a plantation of visible clones, and `individuals` alone gave up to 16
separate full meshes, which a mature oak cannot afford.

Five more faults, each found by looking at a wood rather than at one tree:

12. **The far half of every wood was pale pink.** A billboard card is a
    picture of a shaded tree, so its pixels are sRGB; the card shader encoded
    them a second time without decoding first, lifting every dark value.
13. **Cones were half the tree.** A conifer hung two on every needle shoot -
    467,000 triangles, 47% of a pine - and, being the one part with no
    picture on it, they painted the whole crown flat brown. They go on the
    branches now, and a fruit is meshed like a fruit.
14. **A needle card was 94% hole.** The needle picture was one needle drawn
    across the whole card, 6.5% opaque, so a pine read as bare wood at every
    distance. A new `Needle spray` leaf picture carries a whole shoot: 37%.
15. **Every twig was as thick as its foliage was wide.** Each level took a
    fixed share (about half) of its parent whatever it carried, so a 28 m
    pine had 16 cm twigs. `pipe_ratio` sizes a branch by what it feeds -
    n branches of ratio r satisfy n*r^2 = 1 - which is how a tree is built.
16. **The picture of a tree was a picture of its wood.** The rasteriser that
    bakes a billboard claimed the depth buffer *before* testing the cut-out,
    so every transparent gap in a leaf card punched a hole through everything
    behind it. Foliage is also lit from both sides now, being thin.

## Gaps (verified)

1. The foliage is still a little thin for high summer: a 22 m oak grows
   92,000 leaves where a real one carries nearer 250,000. The counts are
   botanical per twig; it is the twig count that is low.
2. A plant has no reduced level of detail: `mesh_build_lods` refuses a mesh
   with parts, because the pictures would tear, so a plant is drawn whole or
   as a billboard with nothing in between.
3. No painting or touch-up in the viewport: the manual's Free Paint, Touch
   Up, the bias gizmos and interactive pruning are not implemented; a species
   is edited through its nodes and its parameters.
4. No per-species level-of-detail generator or billboard baking: `lod_min` /
   `lod_max` gate parts and the meshing halves per level, but the studio
   builds level 0 only - which is why a mature tree costs 4.5 M triangles.
5. Individual edition (making one primitive instance's parameter unique) is
   not implemented; variations are per plant, through the seed.
6. The growth simulation is one node and cannot be nested inside a loop.
7. A species' pictures made from rules are regenerated on load rather than
   written beside the species file.
8. Cut-out leaves trace their outline from pictures made from rules; an
   outline from a file's alpha is traced by the studio only.
9. Imported objects as plant parts (`PlantObject`) load through the mesh
   readers but do not carry their own materials into the plant's parts.
10. A species saved with field links loses them: the links are saved, the
    nodes they come from are not part of the species.

## Next

- Foliage and slenderness (gaps 1 and 2): more twigs per branch, and a
  radius from the pipe model rather than a fixed share of the parent.
- Viewport interaction: draw a branch, touch up an axis, prune by clicking,
  the bias gizmos.
- Level of detail: build levels 1..n per species, a billboard for the last,
  and choose per instance the way populations already do.
- A species browser with previews in the library, and thumbnails rendered at
  save time for every preset.
- Seasonal material looks on imported pictures (the manual's texture tag
  conventions) so a downloaded plant can change with the year too.

Owner files: `engine/gpx/plant.hpp`, `engine/plant/*`, `engine/nodes/nodes_plant.cpp`,
`studio/plant_species*.cpp`, `studio/panel_plant_editor*.cpp`,
`studio/scene_plants_species.cpp`, `studio/plant_library.*`,
`studio/plant_place.*`, `studio/panel_plants.cpp`, `orchestrator/plant_fetch.py`.
