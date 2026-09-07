# Node previews

Every node in the graph carries a thumbnail of what it produces. It is the
fastest thing in the editor to read: you do not have to trace a chain back to
the terrain to find out which link went wrong, because each card already shows
its own answer.

The picture is 112 px square, redrawn after every evaluation, and drawn from
the node's **first output**, by type:

| Output | Drawn as |
| :--- | :--- |
| Texture | the image itself, resampled down |
| Heightmap | a hillshade — height as brightness, slope as shading, lit from the north-west |
| Field (number) | the function sampled over the tile at LOD 6, then hillshaded like a heightmap |
| Field (colour) | the function sampled over the tile, as colour |
| Points | the cloud splatted onto the tile |
| Points, on a port named `path` | the polyline through the cloud, in order |

A node can also hand in its own picture — `previews_set_image` — which is how
an imported mesh or a loaded material shows a real render rather than a
buffer. A custom picture is never overwritten by the automatic one.

Collapsing a node (the chevron on its header) hides the preview and leaves the
ports; collapsing again leaves only the header. See `studio/preview.cpp` and
`studio/panel_graph_draw.cpp`.

## Point clouds

Whether a scatter clumps, leaves bald patches or covers the ground evenly is a
picture, not a number, and no arrangement of sliders will tell you which one
you got. So points **accumulate** rather than overwrite: a pixel holding
twenty points is brighter than a pixel holding one, and a cluster reads as a
bright knot against a dark field.

Two decisions in `engine/gpx/points_thumb.hpp` are worth knowing about,
because both are visible:

- **The brightness ramp is a square root, not a straight line.** With a
  linear ramp, normalising against the densest pixel makes the sparse half of
  a clustered scatter disappear into the background — which is the half you
  were looking at it to see.
- **Points outside the tile are outside the picture, not clamped to its
  edge.** Clamping would paint a bright rim along the border, and a rim is
  exactly what a working transform looks like, so it would hide the bug it
  was meant to reveal. A cloud pushed entirely off the tile draws nothing,
  which is the truth.

A path is the same buffer with its points in order, so it draws as the line
between them rather than as the points. Segments are clipped to the canvas
before they are walked: a path running to 10⁶ in tile units is a segment a
hundred million pixels long, and stepping it a pixel at a time takes ten
seconds for a picture of nothing.

## What is tested

| What | Where |
| :--- | :--- |
| every node has an output the preview code can draw | `tests/cpp/test_nodes.cpp` (the contract battery, all 245 types) |
| the cloud picture itself — clustering reads, paths connect, bad numbers survive, off-tile clips | `tests/cpp/test_points_thumb.cpp` |
| the point and path nodes hand it a cloud when wired the way people wire them | `tests/cpp/test_points_preview.cpp` |
| those tests would notice if the drawing were wrong | `python scripts/mutate.py points_thumb` |
