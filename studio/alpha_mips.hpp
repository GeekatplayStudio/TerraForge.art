// Geekatplay TerraForge - mipmaps for a cut-out picture that keep its coverage.
//
// A leaf card is drawn where its picture's alpha passes a cut (0.5, FS_MESH).
// Averaging a level down blends the leaf's opaque texels with the clear ones
// round them, so each smaller level has fewer texels over the cut than the one
// before: a tree thinned to bare twigs with distance, and a hedge went see-
// through. Each level's alpha is scaled until the share of texels past the cut
// is the base level's again (Castano's coverage-preserving mipmaps).
// GL-free, so the render tests check it headless.
#pragma once
#include <cstdint>
#include <vector>

namespace studio {

// true when some texels are over the cut and some under it: the only
// pictures whose levels need this
bool alpha_has_cut(const std::vector<uint8_t> &rgba, int w, int h);

// the share of texels whose alpha is at or past the cut (128)
float alpha_coverage(const std::vector<uint8_t> &rgba, int w, int h, float scale = 1.f);

// Every level below the base, each half the one before (rounded down, at
// least 1), box-filtered, with its alpha scaled to the base level's coverage.
// Returns them smallest last; `sizes` gets their widths and heights.
std::vector<std::vector<uint8_t>> alpha_coverage_mips(const std::vector<uint8_t> &rgba, int w,
                                                      int h, std::vector<int> &sizes);

} // namespace studio
