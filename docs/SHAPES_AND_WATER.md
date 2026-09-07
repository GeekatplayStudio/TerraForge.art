# The shape of the ground, and the water on it

Two nodes: `TerrainShape`, which decides what the terrain tile *is*, and
`Lake`, which puts a body of water somewhere in particular.

## Why one slider was not enough

The only thing that could be done about a tile's border was
`post_zero_edges` on the standard output block: one number that faded the
whole rectangle to its lowest value over a fixed smoothstep. It can make a
square island. It cannot make a round one, a wandering coast, or a plateau
that stays flat and then drops - and it runs two different questions
together, because "how far in does the fade reach" and "how hard does it
pull" are the same number.

`TerrainShape` separates them. The outline is a named shape or your own
mask, its rim can wander, and the edge is **three** controls:

| control | question it answers |
| :--- | :--- |
| **Blend extent** | How far in from the rim the blend reaches, as a fraction of the shape's own radius - so it means the same on a big island and a small one. |
| **Blend gradient** | The curve it takes on the way. Below 1 the ground stays high and drops away near the rim: a plateau with a cliff. Above 1 it starts falling from well inside: a beach. 1 is the plain S-curve, which is what the old slider always did. |
| **Blend intensity** | How far down it goes by the time it gets there. 1 takes the rim all the way to the base level; less leaves it standing proud. |

They are independent, and the tests say so: at a fixed extent, changing the
gradient still moves the profile, and at the rim the height is exactly
`1 - intensity` whatever the other two are set to.

## Outlines

`Rectangle`, `Rounded rectangle`, `Round` (an ellipse when width and height
differ), `Diamond`, and `From mask` - which takes the region from the mask
input, so a coastline traced from a real map gets the same edge treatment as
a generated one.

**The rim wanders inward only.** `Edge wander` perturbs the outline with a
fractal, and the perturbation only ever eats *into* the shape. That is what
makes the size a maximum you can reason about: a lake given a 400 m radius
occupies less than that circle and never crosses it. A symmetric wobble
would push the rim out as often as in, and a lake would climb its own bank.

## Lake

Terragen's Lake object is a water disc with a **Water level**, a **Centre**
and a **Max radius** ([Lake - Terragen
documentation](https://docs.planetside.co.uk/wiki/Lake), Transform tab),
attached to a planet and shaded by a water shader. It is always round,
always flat, and the shore is wherever the disc happens to cut the ground.

Ours keeps those three under their own names and goes past them where it
matters:

- **the rim wanders**, so a shore has bays and spits rather than being a
  circle;
- **Follow the ground** settles the lake into the valley it is in - water
  appears only where the ground is already below the level. Turn it off and
  you get Terragen's literal flat disc, which ignores the terrain and lets
  rock poke through it;
- **Carve the bed** pulls the ground under the lake down, deepest in the
  middle, so the water is a body and not a film laid over a hillside;
- **Bank the shore** levels the ground just outside the waterline toward the
  water. This is the control that reads as a lake rather than as a flooded
  hole;
- **Shore width** and **Shore gradient** are the same two questions as the
  terrain's blend: how far in the water shallows, and on what curve. Below 1
  it stays deep and shallows abruptly - a tarn in a rock basin; above 1 it
  shallows from far out - a wide beach.

Five outputs: `output` (the terrain with its bed carved and its shore
banked), `water` (the water surface), `depth`, `mask` (where the water
surface is) and `shore` (the waterline band, for a beach material).

**`mask` is where the water surface is, not where it is deep.** A flat disc
still has a surface over the rock that pokes through it - that is what makes
it a disc - so keying the mask on depth would have made "follow the ground"
and "ignore it" produce the same picture. They do not.

## Where it is verified

`tests/cpp/test_shape_lake.cpp`, 35 checks: that every outline is solid at
its centre and gone outside its half-width, that the three edge controls do
three different jobs, that a wandering rim never reaches past the radius it
was given, that a lake's radius is in metres and follows the terrain's size,
that a conforming lake does not climb a hillside and a disc does, and that
carving actually lowers the bed.

Three real defects came out of writing it: a symmetric rim wobble that let
the shape escape its own radius, a mask keyed on depth that made "follow the
ground" indistinguishable from ignoring it, and a shore band multiplied by
the coverage that fades it - which capped it at a quarter and put its peak
in the wrong place.
