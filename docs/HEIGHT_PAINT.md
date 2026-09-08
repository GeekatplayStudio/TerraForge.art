# Painting the terrain

The **Height Paint** window (View > Height Paint) draws the terrain as a
greyscale picture:

- **mid grey** — the layer does nothing here, and the terrain is whatever the
  nodes above it made;
- **darker** — carves down: a river, a canyon, a hollow;
- **lighter** — raises: a hill, a ridge, a bank.

It is the same idea as a height map in an image editor, except that the
picture is a node in the graph rather than a file, so everything downstream of
it keeps working on what you paint.

## The layer is a node

Strokes go into a **Terrain sculpt** node's painted field. The first stroke
creates one and splices it into the chain just before the terrain output; after
that every stroke accumulates in the same layer. That is what keeps hand work
and procedural work in one place: change the mountain generator above it and
the riverbed you carved is still there, sitting on the new shape.

Because it is an ordinary node, you can move it. Put it **before** erosion and
the water flows down the valley you drew — the canyon gets banks, deposits and
channels of its own, and the materials that read erosion follow. Put it after
erosion and your strokes are the last word.

## While you draw

Each frame of a stroke evaluates the graph at a reduced resolution and a full
pass runs when you let go. Terrain, filters and materials update as the stroke
lands rather than after it, which is what makes drawing a river's course
possible at all — you can see where it wants to go.

How live it feels depends on what is downstream. A few filters is immediate;
a heavy erosion pass is not instant even at reduced resolution, and a long
chain will lag the pointer. The layer itself is always immediate.

## Tools

| Tool | What it does |
| :--- | :--- |
| **Shade** | Paints toward the grey you pick. Going over the same ground again deepens the stroke up to that value and then stops, which is what makes a flat plateau or a level riverbed paintable. |
| **Raise** | Pushes up under the brush, and down with Invert. Unlike Shade it keeps going the longer you hold it. |
| **Smooth** | Relaxes the painted layer toward its surroundings — softening a bank, blending a stroke into what it meets. |
| **Erase** | Takes paint back out, down to the layer doing nothing. The terrain underneath is untouched. |

**Size** is the brush width as a fraction of the tile. **Edge** runs from a
soft airbrush at the left to a hard pen at the right. **Flow** is how fast the
stroke builds while the button is held.

The **Shade** ramp runs black to white; the swatch with the orange frame is mid
grey, the value that changes nothing. The number beside it takes an exact
value, so 0.5 can be typed rather than aimed at.

## Starting from a picture

**Open image...** loads a greyscale PNG, JPG, TGA or BMP into the layer,
resampled to its resolution. Mid grey stays neutral, so a height map exported
from somewhere else drops straight in and can then be painted on. **Clear**
puts the layer back to doing nothing; the terrain under it is not touched.

## The canvas

The picture is the layer's own value, black to white. **Terrain under** tints
it with the shape coming in, faintly, so a river can be drawn between the hills
it has to run through — faintly on purpose, because the grey has to stay
readable as a value.

The wheel zooms and the middle button pans; the readout under the canvas gives
the texel, its value and its grey.

## Limits

- **The layer has its own resolution** (512 x 512 by default), separate from
  the graph's. It is resampled to the terrain, so painting at 512 and
  evaluating at 2048 gives smooth strokes, not blocky ones — but it will not
  hold detail finer than its own grid.
- **One sculpt layer at a time.** The painter follows the existing one, or a
  selected Mask paint node. Several layers means several nodes; the painter
  paints whichever it finds.
- **Undo is per stroke**, not per dab.

## See also

- [TERRAIN_PERFORMANCE.md](TERRAIN_PERFORMANCE.md) — what the interactive
  evaluation does and what it costs.
- `MaskPaint` in [NODES.md](NODES.md) — the same brushes painting a 0..1 mask
  instead of relief, for splatting materials or placing scatter.
