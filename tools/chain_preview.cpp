// Geekatplay TerraForge — headless chain preview.
//
//   chain_preview "TerrainFractal2:octaves=9,roughness=0.9|StreamPower:iterations=60" out.png [res]
//
// Builds the chain (each node's heightmap output into the next node's
// "input"), evaluates it at `res`, and writes a hillshaded PNG of the last
// node's output plus a plain height PNG beside it. For tuning presets and
// erosion parameters without launching the studio: a run is a second, not
// a minute.
#include "gpx/node_graph.hpp"
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "stb_image_write.h"

using namespace gpx;

static void set_attr(Node *n, const std::string &key, const std::string &val) {
  Attribute *at = n->attrs.find(key);
  if (!at) {
    std::printf("  (no attribute '%s' on %s)\n", key.c_str(), n->type.c_str());
    return;
  }
  switch (at->type) {
    case AttrType::Float: at->f = std::strtof(val.c_str(), nullptr); break;
    case AttrType::Int: at->i = std::atoi(val.c_str()); break;
    case AttrType::Bool: at->b = val == "1" || val == "true"; break;
    case AttrType::Choice: at->i = std::atoi(val.c_str()); break;
    case AttrType::Seed: at->seed = (uint32_t)std::strtoul(val.c_str(), nullptr, 10); break;
    default: std::printf("  (attribute '%s' type not settable here)\n", key.c_str());
  }
}

int main(int argc, char **argv) {
  if (argc < 3) {
    std::printf("usage: chain_preview \"Type:k=v,k=v|Type:k=v\" out.png [res]\n");
    return 1;
  }
  std::string spec = argv[1], out_png = argv[2];
  int res = argc > 3 ? std::atoi(argv[3]) : 256;
  Graph g;
  g.resolution = res;
  Node *prev = nullptr, *last = nullptr;
  size_t pos = 0;
  while (pos <= spec.size()) {
    size_t bar = spec.find('|', pos);
    std::string part = spec.substr(pos, bar == std::string::npos ? std::string::npos : bar - pos);
    pos = bar == std::string::npos ? spec.size() + 1 : bar + 1;
    if (part.empty()) continue;
    size_t colon = part.find(':');
    std::string type = part.substr(0, colon);
    Node *n = g.add_node(type, 0, 0);
    if (!n) {
      std::printf("unknown node type '%s'\n", type.c_str());
      return 1;
    }
    if (colon != std::string::npos) {
      std::string kv = part.substr(colon + 1);
      size_t p = 0;
      while (p < kv.size()) {
        size_t comma = kv.find(',', p);
        std::string one = kv.substr(p, comma == std::string::npos ? std::string::npos : comma - p);
        p = comma == std::string::npos ? kv.size() : comma + 1;
        size_t eq = one.find('=');
        if (eq != std::string::npos) set_attr(n, one.substr(0, eq), one.substr(eq + 1));
      }
    }
    if (prev) {
      Port *po = prev->first_out(DataType::Heightmap);
      if (po) g.add_link(prev->id, po->name, n->id, "input");
    }
    prev = n;
    last = n;
  }
  if (!last) return 1;
  auto t0 = std::chrono::steady_clock::now();
  g.evaluate();
  double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
  for (auto &n : g.nodes)
    std::printf("%-18s %8.1f ms%s%s\n", n->type.c_str(), n->last_compute_ms,
                n->error.empty() ? "" : "  ERROR: ", n->error.c_str());
  std::printf("total %.1f ms\n", ms);
  Port *po = last->first_out(DataType::Heightmap);
  if (!po || !po->hmap || po->hmap->empty()) {
    std::printf("no heightmap output\n");
    return 1;
  }
  Heightmap h = *po->hmap;
  h.remap(0.f, 1.f);
  const int W = h.w, H = h.h;
  std::vector<unsigned char> shade((size_t)W * H * 3), height((size_t)W * H);
  // sun from the north-west, 35 degrees up, plus a little altitude tint
  const float lx = -0.5f, ly = -0.5f, lz = 0.7071f;
  for (int y = 0; y < H; ++y)
    for (int x = 0; x < W; ++x) {
      float dx, dy;
      h.gradient_at(x, y, dx, dy);
      float nx = -dx * W * 0.25f, ny = -dy * H * 0.25f, nz = 1.f;
      float len = std::sqrt(nx * nx + ny * ny + nz * nz);
      float s = std::fmax((nx * lx + ny * ly + nz * lz) / len, 0.f);
      float v = h.at(x, y);
      float g = (0.2f + 0.8f * s) * (0.55f + 0.45f * v);
      size_t i = (size_t)y * W + x;
      shade[i * 3] = (unsigned char)std::fmin(g * 255.f, 255.f);
      shade[i * 3 + 1] = (unsigned char)std::fmin(g * 245.f, 255.f);
      shade[i * 3 + 2] = (unsigned char)std::fmin(g * 225.f, 255.f);
      height[i] = (unsigned char)std::fmin(v * 255.f, 255.f);
    }
  stbi_write_png(out_png.c_str(), W, H, 3, shade.data(), W * 3);
  std::string hp = out_png.substr(0, out_png.rfind('.')) + "_height.png";
  stbi_write_png(hp.c_str(), W, H, 1, height.data(), W);
  std::printf("wrote %s and %s\n", out_png.c_str(), hp.c_str());
  return 0;
}
