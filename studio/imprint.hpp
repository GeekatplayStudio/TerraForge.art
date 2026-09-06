// Geekatplay TerraForge - objects standing on the terrain.
//
// A mesh made a child of the terrain object is "grounded": it stands on
// the surface (its base follows the ground as it is moved across it, unless
// unlocked), and the ground answers it through a TerrainImprint node in the
// graph - flat under the object, blended around it, a hollow when it is
// pushed down and a mound when it is lifted. The studio's part is here:
// gathering the footprints every frame, keeping the node in the chain, and
// the lock that holds the base on the surface.
#pragma once

namespace studio {
struct App;
struct SceneObject;

// Index of the terrain object a mesh stands on, -1 if it is not grounded.
int imprint_ground_of(const SceneObject &o);

// Make the object a child of the terrain, so it is grounded. Returns the
// object's new index (the tree may have been reordered), or -1.
int imprint_place_on_terrain(App &a, int object);

// Once a frame: footprints to the node, the base to the surface.
void app_service_imprint(App &a);

// The TerrainImprint node's id, 0 if there is none yet.
unsigned long long imprint_node(App &a);
} // namespace studio
