# Reusing a material

A material in TerraForge is a **Material output** node with channels wired into
it, and an object points at one. Two objects can share a material by pointing
at the same node — but then they are the same material, and changing it for one
changes it for both.

The **Material source** node is the other way of reusing one. It brings another
object's material into the graph as eight ordinary outputs, so it can be
filtered, tinted, blended and sent into a Material output of your own. The
original is untouched.

> The terrain's rock, but wetter and darker where the water sits.
> The cliff's material on the boulders, at half the tiling.
> The same ground, with a different gradient.

## Using it

1. Open the **Materials** workspace.
2. Add a **Material source** node (Material category).
3. In **From object**, pick the object whose material you want. The list is the
   scene's objects, built fresh each time you open it.
4. Wire its channels into whatever you want to do to them, and on into a
   **Material output**.
5. Assign that Material output to the object you are dressing.

The channels arrive exactly as the source built them, so the mapping, tiling
and projection that went into the original come with them. That is the point:
it is a copy that has been modified, not a re-creation that has to be matched
by eye.

## What comes out

The same eight channels a Material output takes in:

| Channel | Type |
| :--- | :--- |
| base color | texture |
| normal | texture |
| roughness | texture |
| metallic | texture |
| height | texture |
| ambient occlusion | texture |
| alpha | texture |
| displacement | heightmap |

A channel nothing was wired into on the source comes out empty, which reads the
same as an unconnected input anywhere else in the graph. An object that has no
material at all leaves every channel empty and the node says so.

## It follows the original

Change the source material — any node feeding it — and the import changes with
it, in the same evaluation. There is no copy to refresh and no button to press.
Reassign the object to a different material and the import follows that too,
because what the node names is the object, not the material node.

That is worth knowing when you want the opposite. If you want a snapshot that
stops tracking, put the channels you care about through a node that ends the
chain — save the texture and read it back, or bake it — rather than expecting
the importer to hold still.

## How it finds the material

The node stores two things: **From object**, the name you picked, and
**Material node**, the id it resolved to. The studio fills the second in from
the first, once a frame:

- the object's own material, if it has one;
- for the terrain, and only the terrain, the render settings' terrain material,
  which is where projects made before objects carried a material recorded it;
- otherwise nothing, and the node reports that the object has no material.

You do not normally touch **Material node**. It is shown because it is saved
with the project and because a script can set it directly, which is how a graph
can import a material that no object is wearing.

Ids are renumbered when a project loads, and the reference is renumbered with
them, so an import survives a save and reload pointing at the same material —
not at whichever node inherited the number.

## Limits

- **The source must be a material.** Pointing the node at a noise or a texture
  node is an error rather than eight empty channels to puzzle over.
- **Circular imports are refused.** A material that imports the material it
  feeds is a cycle, and the graph reports it as one.
- **Deleting the object clears the import.** The node goes unbound rather than
  quietly serving the material of something that no longer exists.
- **Renaming the object breaks the link**, because the name is what the node
  stores. Pick the object again from the dropdown.

## See also

- [MATERIAL_LAYERS.md](MATERIAL_LAYERS.md) — stacking materials by slope,
  height and erosion, which is the other way to build one out of others.
- [NODES.md](NODES.md#materialsource) — the generated reference entry.
