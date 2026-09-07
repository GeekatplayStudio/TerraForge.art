// Geekatplay TerraForge - populations for ground that has no tile.
//
// A planet's surface and an infinite terrain are functions, not pictures:
// there is no 0..1 domain to divide into a lattice and no heightmap to
// stand the copies on. An EcosystemLayer with "Populate around the camera"
// set is therefore realised here instead of in the node: the cells of a
// world-anchored lattice near the camera are generated on demand, cached,
// and dropped again as the camera leaves them.
//
// What a cell holds never depends on the camera - only which cells exist
// does (engine/gpx/scatter.hpp). A cell re-entered from the other side
// holds exactly what it held before, so nothing shimmers or re-grows.
#pragma once

namespace studio {
struct App;

// Per frame: realise the cells the camera can see, retire the rest, and
// keep every mesh bound to an unbounded layer holding the right copies.
// Cheap when no layer is unbounded (the usual case).
void app_service_population(App &a);
} // namespace studio
