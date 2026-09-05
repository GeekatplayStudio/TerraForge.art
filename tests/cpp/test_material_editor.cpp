// Geekatplay TerraForge - the material editor's engine side, tested without
// a window: the Vue property set on MaterialOutput, NaturalGrain, the
// mapped-picture controls on TextureFile, the Presence modes, alpha boost
// and highlight on MaterialLayer, and the two-material distribution with
// its influence of environment on MaterialStack.
//
// Each case names the Vue page it replicates so a mismatch can be checked
// against the manual rather than against a memory of it.
#include "gpx/material_params.hpp"
#include "gpx/node_graph.hpp"
#include "gpx/serialization.hpp"
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

static int failures = 0;
static void check(bool ok, const char *what) {
  if (!ok) {
    std::printf("FAIL: %s\n", what);
    ++failures;
  }
}

static const gpx::TextureRGBA *tex_of(gpx::Node *n, const char *port) {
  gpx::Port *p = n->port(port, gpx::PortDir::Out);
  return p && p->tex ? p->tex.get() : nullptr;
}
static const gpx::Heightmap *hm_of(gpx::Node *n, const char *port) {
  gpx::Port *p = n->port(port, gpx::PortDir::Out);
  return p && p->hmap ? p->hmap.get() : nullptr;
}
static float lum(const float *p) { return p[0] * 0.299f + p[1] * 0.587f + p[2] * 0.114f; }

// p703, p742, p744, p688-690, p735: every tab group exists on the node
static void test_tab_groups() {
  std::printf("tab groups...\n");
  gpx::Graph g;
  gpx::Node *m = g.add_node("MaterialOutput");
  const char *tabs[] = {"Color",        "Alpha",     "Bump",     "Highlights",
                        "Transparency", "Reflection", "Translucency", "Clearcoat",
                        "Effects",      "Options",   "Transform"};
  for (const char *t : tabs) {
    int n = 0;
    for (const gpx::Attribute &at : m->attrs.items)
      if (at.group == t) ++n;
    check(n >= 2, (std::string("group ") + t).c_str());
  }
  gpx::MaterialParams p = gpx::material_params_from(m->attrs);
  check(p.cc_intensity == 0.f && p.cc_ior == 1.5f, "clearcoat off by default, coat IOR 1.5");
  check(p.receive_shadows && !p.one_sided && !p.turbulence, "options and transform defaults");
  m->attrs.find("turbulence")->b = true;
  m->attrs.find("turb_complexity")->i = 99;
  m->attrs.find("cc_intensity")->f = 0.7f;
  p = gpx::material_params_from(m->attrs);
  check(p.turbulence && p.turb_complexity == 8, "turbulence on, complexity clamped to 8");
  check(std::fabs(p.cc_intensity - 0.7f) < 1e-6f, "coat intensity reads back");
}

// p712-713: natural grain is two colours through a noise, deterministic,
// and balance moves which colour dominates
static void test_natural_grain() {
  std::printf("natural grain...\n");
  gpx::Graph g;
  g.resolution = 64;
  gpx::Node *n = g.add_node("NaturalGrain");
  check(n != nullptr, "NaturalGrain exists");
  gpx::Attribute *c1 = n->attrs.find("color1"), *c2 = n->attrs.find("color2");
  c1->col[0] = 0.f; c1->col[1] = 0.f; c1->col[2] = 0.f;
  c2->col[0] = 1.f; c2->col[1] = 1.f; c2->col[2] = 1.f;
  check(g.evaluate(), "evaluates");
  const gpx::TextureRGBA *t = tex_of(n, "texture");
  check(t && !t->empty(), "produces a texture");
  if (!t) return;
  double mean = 0; float lo = 1, hi = 0;
  for (int y = 0; y < t->h; ++y)
    for (int x = 0; x < t->w; ++x) {
      float l = lum(t->px(x, y));
      mean += l; lo = std::min(lo, l); hi = std::max(hi, l);
    }
  mean /= (double)t->w * t->h;
  check(hi - lo > 0.3f, "the grain varies between the two colours");
  check(mean > 0.25 && mean < 0.75, "at balance 0.5 neither colour dominates");
  std::vector<float> first = t->v;
  g.mark_dirty(n->id);
  g.evaluate();
  check(t->v == first, "the same settings give the same grain");
  n->attrs.find("balance")->f = 0.95f;
  g.mark_dirty(n->id);
  g.evaluate();
  double mean2 = 0;
  for (int y = 0; y < t->h; ++y)
    for (int x = 0; x < t->w; ++x) mean2 += lum(t->px(x, y));
  mean2 /= (double)t->w * t->h;
  check(mean2 > mean + 0.15, "balance toward the second colour brightens the grain");
  const gpx::Heightmap *grain = hm_of(n, "grain");
  check(grain && !grain->empty(), "the grain is also a mask");
}

// p705-707: invert, rotate and mirror on a mapped picture
static void test_texture_file_controls() {
  std::printf("texture file controls...\n");
  namespace fs = std::filesystem;
  fs::path f = fs::temp_directory_path() / "terraforge_tf_test.ppm";
  {
    // a 2x2 picture: red, green / blue, white (binary PPM, which stb reads)
    std::ofstream o(f, std::ios::binary);
    o << "P6\n2 2\n255\n";
    unsigned char px[12] = {255, 0, 0, 0, 255, 0, 0, 0, 255, 255, 255, 255};
    o.write((const char *)px, 12);
  }
  gpx::Graph g;
  g.resolution = 8;
  gpx::Node *n = g.add_node("TextureFile");
  n->attrs.find("path")->s = f.string();
  n->attrs.find("mapping")->i = 0; // stretch
  n->attrs.find("interpolation")->i = 1; // nearest, so texels stay pure
  check(g.evaluate(), "reads the picture");
  const gpx::TextureRGBA *t = tex_of(n, "texture");
  check(t && t->w == 8, "output at graph resolution");
  if (!t) return;
  auto is = [&](int x, int y, float r, float gg, float b) {
    const float *p = t->px(x, y);
    return std::fabs(p[0] - r) < 0.02f && std::fabs(p[1] - gg) < 0.02f && std::fabs(p[2] - b) < 0.02f;
  };
  check(is(0, 0, 1, 0, 0) && is(7, 0, 0, 1, 0) && is(0, 7, 0, 0, 1), "the picture reads as stored");
  n->attrs.find("invert")->b = true;
  g.mark_dirty(n->id); g.evaluate();
  check(is(0, 0, 0, 1, 1) && is(7, 7, 0, 0, 0), "invert turns red to cyan and white to black");
  n->attrs.find("invert")->b = false;
  n->attrs.find("rotate")->i = 1; // 90
  g.mark_dirty(n->id); g.evaluate();
  check(!is(0, 0, 1, 0, 0), "a quarter turn moves the red texel");
  n->attrs.find("rotate")->i = 0;
  n->attrs.find("mirror_x")->b = true;
  g.mark_dirty(n->id); g.evaluate();
  check(is(0, 0, 0, 1, 0) && is(7, 0, 1, 0, 0), "mirror X swaps red and green");
  n->attrs.find("mirror_x")->b = false;
  n->attrs.find("gamma")->f = 2.f;
  g.mark_dirty(n->id); g.evaluate();
  check(is(0, 0, 1, 0, 0) && is(7, 7, 1, 1, 1), "gamma leaves pure black, white and primaries alone");
  std::error_code ec;
  fs::remove(f, ec);
}

// p736-738, p704, p691: presence modes, alpha boost, highlight
static void test_layer_presence() {
  std::printf("layer presence...\n");
  gpx::Graph g;
  g.resolution = 32;
  gpx::Node *ter = g.add_node("Noise");
  gpx::Node *below = g.add_node("FlatColor");
  gpx::Node *top = g.add_node("FlatColor");
  top->attrs.find("r")->f = 1.f; top->attrs.find("g")->f = 0.f; top->attrs.find("b")->f = 0.f;
  below->attrs.find("r")->f = 0.f; below->attrs.find("g")->f = 0.f; below->attrs.find("b")->f = 1.f;
  gpx::Node *layer = g.add_node("MaterialLayer");
  g.add_link(below->id, "texture", layer->id, "below albedo");
  g.add_link(top->id, "texture", layer->id, "albedo");
  g.add_link(ter->id, "output", layer->id, "terrain");
  layer->attrs.find("use_altitude")->b = true;
  gpx::Attribute *band = layer->attrs.find("altitude");
  band->v2[0] = 0.5f; band->v2[1] = 1.f; // upper half of the terrain's range
  check(g.evaluate(), "evaluates");
  const gpx::Heightmap *pres = hm_of(layer, "presence");
  check(pres && !pres->empty(), "presence is produced");
  if (!pres) return;
  double cover = 0;
  for (float v : pres->v) cover += v;
  cover /= (double)pres->v.size();
  check(cover > 0.2 && cover < 0.8, "by terrain: about half the ground");
  // absolute: the band is in height units, so 0.5..1 on a 0..1 gradient is
  // still the upper half; on a terrain scaled to 0..0.4 it would be nothing
  layer->attrs.find("altitude_mode")->i = 1;
  g.mark_dirty(layer->id); g.evaluate();
  double cover_abs = 0;
  for (float v : pres->v) cover_abs += v;
  cover_abs /= (double)pres->v.size();
  check(std::fabs(cover_abs - cover) < 0.3, "absolute on a unit terrain agrees roughly");
  // relative to sea: raise the sea and the band moves up
  layer->attrs.find("altitude_mode")->i = 2;
  layer->attrs.find("sea_level")->f = 0.4f;
  g.mark_dirty(layer->id); g.evaluate();
  double cover_sea = 0;
  for (float v : pres->v) cover_sea += v;
  cover_sea /= (double)pres->v.size();
  check(cover_sea < cover_abs, "measured from a higher sea, less ground is in the band");
  // alpha boost strengthens, never invents
  layer->attrs.find("altitude_mode")->i = 0;
  layer->attrs.find("opacity")->f = 0.5f;
  g.mark_dirty(layer->id); g.evaluate();
  float mx = 0; for (float v : pres->v) mx = std::max(mx, v);
  check(mx < 0.6f, "opacity halves the presence");
  layer->attrs.find("alpha_boost")->f = 1.f;
  g.mark_dirty(layer->id); g.evaluate();
  float mx2 = 0; int zeros_after = 0;
  for (float v : pres->v) { mx2 = std::max(mx2, v); if (v == 0.f) ++zeros_after; }
  check(mx2 > mx, "alpha boost strengthens the layer");
  check(zeros_after > 0, "but it puts nothing where the constraint excluded it");
  // highlight: the layer shows as its flat colour
  layer->attrs.find("highlight")->b = true;
  gpx::Attribute *hc = layer->attrs.find("highlight_color");
  hc->col[0] = 0.f; hc->col[1] = 1.f; hc->col[2] = 0.f;
  g.mark_dirty(layer->id); g.evaluate();
  const gpx::TextureRGBA *alb = tex_of(layer, "albedo");
  bool green_somewhere = false;
  if (alb)
    for (int y = 0; y < alb->h && !green_somewhere; ++y)
      for (int x = 0; x < alb->w; ++x)
        if (alb->px(x, y)[1] > 0.9f && alb->px(x, y)[0] < 0.1f) { green_somewhere = true; break; }
  check(green_somewhere, "highlighted, the layer reads as its solid colour");
}

// p747-752: two materials by one distribution, proportions, strip, method,
// and the influence of altitude and slope
static void test_mixed_distribution() {
  std::printf("mixed distribution...\n");
  gpx::Graph g;
  g.resolution = 32;
  gpx::Node *ter = g.add_node("Noise");
  gpx::Node *a = g.add_node("FlatColor"), *b = g.add_node("FlatColor");
  a->attrs.find("r")->f = 0.f; a->attrs.find("g")->f = 0.f; a->attrs.find("b")->f = 0.f;
  b->attrs.find("r")->f = 1.f; b->attrs.find("g")->f = 1.f; b->attrs.find("b")->f = 1.f;
  gpx::Node *mix = g.add_node("MaterialStack");
  g.add_link(a->id, "texture", mix->id, "albedo 1");
  g.add_link(b->id, "texture", mix->id, "albedo 2");
  const char *tport = "output";
  g.add_link(ter->id, tport, mix->id, "terrain");
  mix->attrs.find("mix_mode")->i = 1;
  mix->attrs.find("proportion")->f = 0.5f;
  mix->attrs.find("strip")->f = 0.2f;
  check(g.evaluate(), "evaluates");
  const gpx::TextureRGBA *out = tex_of(mix, "albedo");
  check(out && !out->empty(), "produces albedo");
  if (!out) return;
  auto mean_lum = [&]() {
    double m = 0;
    for (int y = 0; y < out->h; ++y)
      for (int x = 0; x < out->w; ++x) m += lum(out->px(x, y));
    return m / ((double)out->w * out->h);
  };
  double even = mean_lum();
  check(std::fabs(even - 0.5) < 0.05, "no distribution: an even blend at proportions 0.5");
  mix->attrs.find("proportion")->f = 0.9f;
  g.mark_dirty(mix->id); g.evaluate();
  check(mean_lum() < 0.1, "proportions toward material 1 show material 1");
  mix->attrs.find("proportion")->f = 0.1f;
  g.mark_dirty(mix->id); g.evaluate();
  check(mean_lum() > 0.9, "proportions toward material 2 show material 2");
  // altitude pushes material 2 uphill
  mix->attrs.find("proportion")->f = 0.5f;
  mix->attrs.find("env_on")->b = true;
  mix->attrs.find("alt_influence")->f = 1.f;
  g.mark_dirty(mix->id); g.evaluate();
  // find where the terrain is high and where low
  const gpx::Heightmap *th = hm_of(ter, tport);
  int hx = 0, hy = 0, lx = 0, ly = 0;
  float hv = -1e9f, lv = 1e9f;
  for (int y = 0; y < th->h; ++y)
    for (int x = 0; x < th->w; ++x) {
      float v = th->v[(size_t)y * th->w + x];
      if (v > hv) { hv = v; hx = x; hy = y; }
      if (v < lv) { lv = v; lx = x; ly = y; }
    }
  int ox = hx * out->w / th->w, oy = hy * out->h / th->h;
  int px = lx * out->w / th->w, py = ly * out->h / th->h;
  check(lum(out->px(ox, oy)) > lum(out->px(px, py)), "with altitude influence, material 2 sits higher");
  mix->attrs.find("alt_influence")->f = -1.f;
  g.mark_dirty(mix->id); g.evaluate();
  check(lum(out->px(ox, oy)) < lum(out->px(px, py)), "negative influence puts material 2 lower");
  // cover: no half-tones in colour
  mix->attrs.find("blend_method")->i = 3;
  g.mark_dirty(mix->id); g.evaluate();
  int half = 0;
  for (int y = 0; y < out->h; ++y)
    for (int x = 0; x < out->w; ++x) {
      float l = lum(out->px(x, y));
      if (l > 0.05f && l < 0.95f) ++half;
    }
  check(half == 0, "cover blends nothing: material 2 covers material 1");
}

int main() {
  test_tab_groups();
  test_natural_grain();
  test_texture_file_controls();
  test_layer_presence();
  test_mixed_distribution();
  if (failures) {
    std::printf("%d material editor check(s) failed\n", failures);
    return 1;
  }
  std::printf("material editor tests passed\n");
  return 0;
}
