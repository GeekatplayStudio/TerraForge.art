// Geekatplay TerraForge — exact Euclidean distance on a grid, shared.
//
// Felzenszwalb & Huttenlocher (2004), "Distance transforms of sampled
// functions": squared distance along a line is the lower envelope of
// parabolas, computable in one pass, and a 2D transform is the row pass
// followed by the column pass. Exact, linear-time, and deterministic — the
// row passes are independent of each other and so are the column passes, so
// parallelism cannot change the result.
//
// Shared because more than one thing needs it: the DistanceField mask node,
// and path carving, which stamps a polyline onto a grid and then needs the
// distance to it everywhere.
#pragma once
#include <vector>

namespace gpx {

// In place: cells > 0.5 are "the shape" (distance 0); on return every cell
// holds the SQUARED distance in cells to the nearest shape cell.
void edt_squared(std::vector<float> &grid, int w, int h);

} // namespace gpx
