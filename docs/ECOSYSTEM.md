# Ecosystems: intelligent distribution of objects

How Vue's EcoSystem material places a forest, what TerraForge does about
it, and how to build a meadow with trees where the grass keeps out from
under the canopy and the pebbles gather at the boulders' feet.

## What Vue does

An EcoSystem is a material whose presence also decides where objects
stand (Vue Reference Manual p1087-1109). Read as mechanism it is five
ideas:

1. **Density is a rate over an area**, per unit of surface, thinned by
   functions of the ground - slope (steeper, sparser), altitude, the
   direction a slope faces - and by the objects placed on the terrain
   ("decay near foreign objects": a void around each house with an
   influence and a falloff).
2. **Which species stands where is a separate decision** from whether
   anything stands there: a population list with a relative presence per
   item, or a driving function whose value picks the item by interval.
3. **Every per-instance property is a distribution** - size (half to
   twice), per-axis or proportional, direction from the surface (vertical
   for a tree, perpendicular for a stone), rotation about the up axis, an
   offset from the surface, a colour, an animation phase - and shrink and
   lean where the population is thin.
4. **Layers interact.** An EcoSystem stacked above another has *affinity*
   with it (positive: cling to its instances, negative: everywhere but) and
   *repulsion* from it (positive: a sudden void around each, negative: only
   inside that void). "By using affinity and repulsion simultaneously, you
   could have the grass appear near the trees, but not underneath them."
5. **Populations are stable under editing**: a small change in the
   settings moves only the instances it concerns.

## What TerraForge does

The same five, on the node graph, in metres:

- **`EcosystemLayer`** is a member of the material stack like a
  `MaterialLayer`: it passes the colour of the layers below through and
  adds a population. Its tabs in the Material Studio are Vue's: General
  (species list, affinity and repulsion with the layer below), Density
  (per hectare, spacing, placement, clumping, decay near objects),
  Scaling & Orientation, Color, Presence (mask, altitude, slope,
  orientation). *Add ecosystem* in the hierarchy puts one on top of the
  stack and wires its `below` input to the nearest population beneath
  it - an `EcosystemLayer` or a `DistributionLayer` - so the interaction
  dials work the moment the layer exists; Up, Down and Remove keep that
  wiring right.
- **The engine** (`engine/gpx/scatter.hpp`) runs five stages, and the
  order is the stability guarantee:
  1. *candidates* - a lattice of cells fixed by the spacing, hashed
     candidates per cell with a stable id `hash(seed, cell, index)`; no
     mask reaches this stage, so editing a mask cannot move a survivor;
  2. *filter* - presence (mask, altitude, slope, orientation, slope
     influence, decay near objects); keep when `u(id) < presence * rate`,
     so raising the density adds instances and never reshuffles;
  3. *species and transform* - weights or a driver map; size, per-axis
     variation, direction from surface, rotation, offset, tint, phase;
     shrink and lean where the population is thin;
  4. *interact* - affinity and repulsion against the layer below, measured
     to the edge of each instance's footprint, then overlap avoidance in
     id order.
  Nothing reads the clock, the camera or the thread count. The same
  settings give the same forest on every machine.
- **Points nodes for a graph built from pieces**: `ScatterArea` (stages 1
  and 2), `PointsTransform` (3), `PointsInteract` (4). Chained with the
  same settings they produce exactly the layer's cloud (tested).
- **The point cloud carries the population**: `gpx::PointCloud` gained
  optional channels - id, species, per-axis scale, yaw, tilt, tint, phase,
  footprint radius, offset - empty for older nodes, defaults on read.
- **Meshes stand for species.** A mesh object bound to the layer
  (Properties > Scatter, or `set_scatter`) picks which species it is;
  bind one mesh per species. Copies stand on the displayed ground, turned,
  sized per axis, leaned towards the surface normal by *direction*, tinted,
  and swaying on their own phase.
- **Decay near objects** reads `TerrainImprint`'s new `objects` output: the
  distance to the nearest object standing on the terrain, so the grass
  thins around the house that the ground moulds to.
- **Level of detail** for the copies - thinned by distance, drawn from
  reduced meshes far away, culled beyond a distance - is in
  [LOD.md](LOD.md).

`examples/macros/ecosystem_layers.json` builds a meadow: trees in two
species, clumped, off the steep ground; grass on top that gathers near the
trees (affinity 0.6 within 60 m) but keeps out from under them (repulsion 1
within 12 m of the canopy).

## The dials

Density tab: **Density** (instances per hectare at full presence),
**Minimum spacing** (the lattice - changing it reseeds), **Placement**
(jittered / random / regular), **Clumping** and **Clump size**, **Seed**,
**Presence threshold**, **Slope influence**, **Decay near objects** with
**Reach** and **Falloff**.

Interaction: **Affinity with layer below** (-1..1) and its radius,
**Repulsion from layer below** (-1..1) and its radius, **Avoid overlapping
instances**. Distances are measured from the edge of the instance below
(its footprint radius times its size), so a repulsion of 12 m keeps the
grass 12 m clear of the canopy, not of the trunk.

Scaling: **Overall scaling**, **Size variation** (1 = half to twice),
**Keep proportions**, **Direction from surface** (0 vertical, 1
perpendicular), **Rotation** (up axis / none / driven) and **Maximum
angle**, **Offset from surface**, **Footprint radius**, **Shrink at low
density** with its radius, **Lean out at low density**.

Species: **Species** (1..8), per species a **presence** (relative) and a
**scale**. A **driver** map picks the species by interval instead.

## Gates (tests/cpp/test_ecosystem.cpp, `ecosystem_tests`)

- identical instances across evaluations and thread counts;
- raising the density is a superset; a mask edit moves nothing outside
  the edited area; nothing where presence is zero or below the threshold;
- a 26 degree ramp is outside a 0..20 slope band; an altitude band keeps
  the upper half; decay is empty at the object and untouched far away;
- affinity 1 gathers the population within its radius and thins the rest;
  repulsion 1 opens a void of the full radius; repulsion -1 keeps only the
  void; both together give a ring; overlap avoidance leaves no pair closer
  than their footprints and does not depend on arrival order;
- species follow their weights or a driver's intervals; variation spans
  half to twice; a lone instance shrinks;
- 50 per hectare over 100 ha places about 5000; grass keeps out from under
  the canopy of a tree layer; the Points nodes equal the layer;
- the Material Studio's stack surgery keeps an ecosystem reacting to the
  population beneath it through a colour layer, and unlinks it when that
  population is removed (undo_tests).
