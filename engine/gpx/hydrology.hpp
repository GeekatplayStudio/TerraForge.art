// Geekatplay TerraForge — hydrology shared by more than one node.
//
// Depression filling is used twice for two apparently different reasons, and
// keeping one implementation is the point of this header: the FillBasins node
// wants the filled surface (and the lake standing in the difference), and flow
// accumulation wants it so routing does not dead-end in every hollow.
#pragma once
#include "heightmap.hpp"
#include <vector>

namespace gpx {

// Priority-Flood depression filling. Barnes, Lehman and Mulla (2014),
// "Priority-flood: An optimal depression-filling and watershed-labeling
// algorithm for digital elevation models", Computers & Geosciences 62:117-127.
//
// Water leaves the map only at its edge, so the flood starts from the border
// and grows inwards, always from the lowest cell reached so far. The height it
// returns for a cell is the lowest ridge any path from that cell to the border
// has to cross — which is exactly the level a basin fills to before it spills.
//
// `epsilon` tilts each filled flat by a hair so water still crosses it toward
// the outlet. Zero gives true level lakes, which is what you want to look at;
// a small positive value is what flow routing needs, or a filled basin has
// nowhere to send its water.
//
// Deterministic and single-threaded: the queue orders by height and breaks
// ties by cell index — a total order — so the traversal, and with a non-zero
// epsilon the result, is bit-identical every run and on every core count
// (AGENTS.md engine rule 1).
std::vector<float> fill_depressions(const Heightmap &in, float epsilon);

} // namespace gpx
