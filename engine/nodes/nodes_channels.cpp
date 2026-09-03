// Geekatplay TerraForge — raster-domain channel converters. The buffer
// graph's counterpart to FieldColorSplit: one texture in, its red, green,
// blue, alpha and luminance out as separate masks, so any channel of a
// photoscanned or painted image can drive a heightmap filter directly.
// ChannelMix (nodes_material_graph.cpp) is the inverse.
#include "gpx/color_math.hpp"
#include "gpx/node_graph.hpp"
#include "gpx/node_helpers.hpp"
#include "gpx/parallel.hpp"
#include <algorithm>

namespace gpx {

REGISTER_NODE(
    ChannelSplit, "Material",
    "Split a texture into red, green, blue, alpha and luminance masks",
    [](Node &n) {
      n.add_in("texture", DataType::Texture);
      n.add_out("r");
      n.add_out("g");
      n.add_out("b");
      n.add_out("a");
      n.add_out("luminance");
    },
    [](Node &n) {
      const TextureRGBA *in = n.in_tex("texture");
      if (!in || in->empty()) {
        n.error = "input 'texture' not connected";
        return;
      }
      Heightmap *out[5] = {&n.out_hmap("r"), &n.out_hmap("g"), &n.out_hmap("b"),
                           &n.out_hmap("a"), &n.out_hmap("luminance")};
      const int w = out[0]->w, h = out[0]->h;
      parallel_rows(h, [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y)
          for (int x = 0; x < w; ++x) {
            int sx = std::min(x * in->w / w, in->w - 1);
            int sy = std::min(y * in->h / h, in->h - 1);
            const float *p = in->px(sx, sy);
            for (int c = 0; c < 4; ++c) out[c]->at(x, y) = p[c];
            out[4]->at(x, y) = luminance_rgb(p);
          }
      });
    })

} // namespace gpx
