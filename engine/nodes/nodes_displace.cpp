// Geekatplay TerraForge — displacement, redirection and surface analysis (P1).
//
// This is the family Terragen builds its terrain from and the one the Vue
// manual spends p717-719 on, and it is the reason the field domain had to exist
// first: every node here answers a question about a *point*, and three of them
// answer it about a point other than the one they were asked about.
//
// That last property is the whole trick.
//
//   Redirect   evaluates its input at a position moved by a vector field. Warp,
//              flow, swirl and domain distortion are all this one node, and it
//              works on anything — noise, a sampled heightfield, another warp —
//              because it does not know or care what it is redirecting.
//   Displace   evaluates its input with a raised detail budget and turns the
//              answer into an offset along a chosen direction.
//   ComputeNormal
//              evaluates its input four times around the point to recover the
//              surface direction *after* displacement, which is what makes
//              stacking order work: shaders downstream of it see the displaced
//              surface rather than the flat one underneath.
//
// On the CPU that is just calling in_field with a modified FieldContext. On the
// GPU it needs the transpiler to re-emit a subtree under a second evaluation
// point, which is what EmitCtx's scoped cache is for — see field_glsl.cpp.
#include "gpx/node_graph.hpp"
#include "gpx/node_helpers.hpp"
#include <algorithm>
#include <cmath>

namespace gpx {
namespace {

// Directions a displacement can act along. "Along normal" is the one that makes
// displacement behave like real relief on a curved surface (and on a planet);
// the others exist because they are predictable, which matters when you are
// stacking several.
enum DirMode { DirNormal = 0, DirUp, DirVector, DirFixed };

// Resolve the displacement direction at a point. Returns a unit-ish vector.
void displace_direction(const Node &self, const FieldContext &ctx, float *out) {
  switch (self.attrs.get_choice("dir_mode")) {
    case DirUp:
      out[0] = 0.f; out[1] = 1.f; out[2] = 0.f;
      return;
    case DirVector: {
      self.in_field("direction", ctx, FieldValue::vector(0, 1, 0)).as_vector(out);
      float l = std::sqrt(out[0]*out[0] + out[1]*out[1] + out[2]*out[2]);
      if (l > 1e-9f) { out[0] /= l; out[1] /= l; out[2] /= l; }
      else { out[0] = 0.f; out[1] = 1.f; out[2] = 0.f; }
      return;
    }
    case DirFixed: {
      out[0] = self.attrs.get_f("dir_x", 0.f);
      out[1] = self.attrs.get_f("dir_y", 1.f);
      out[2] = self.attrs.get_f("dir_z", 0.f);
      float l = std::sqrt(out[0]*out[0] + out[1]*out[1] + out[2]*out[2]);
      if (l > 1e-9f) { out[0] /= l; out[1] /= l; out[2] /= l; }
      else { out[0] = 0.f; out[1] = 1.f; out[2] = 0.f; }
      return;
    }
    default:
      out[0] = ctx.normal[0]; out[1] = ctx.normal[1]; out[2] = ctx.normal[2];
      return;
  }
}

// The scalar being displaced by, with smoothing and the quality boost applied.
float displace_amount(const Node &self, const FieldContext &ctx) {
  FieldContext c = ctx;
  // Quality boost: let this displacement resolve finer than the caller asked
  // for. Terragen and Vue both expose this because a displacement often wants
  // more detail than the geometry carrying it (Vue p718).
  c.lod = std::clamp(ctx.lod + (float)self.attrs.get_i("quality", 0), 1.f, 16.f);

  float a = self.in_number("amount", c, 0.f);
  float sm = std::clamp(self.attrs.get_f("smoothing", 0.f), 0.f, 1.f);
  if (sm > 1e-6f) {
    // Average the four cardinal neighbours. Smoothing a displacement is not a
    // post-blur: it has to happen where the field is sampled, because there is
    // no buffer to blur afterwards.
    float r = std::max(self.attrs.get_f("smooth_radius", 0.01f), 1e-6f);
    float sum = 0.f;
    const float off[4][2] = {{r, 0}, {-r, 0}, {0, r}, {0, -r}};
    for (const auto &o : off) {
      FieldContext n = c;
      n.pos[0] = c.pos[0] + o[0];
      n.pos[2] = c.pos[2] + o[1];
      sum += self.in_number("amount", n, 0.f);
    }
    a = a + (sum * 0.25f - a) * sm;
  }
  return a;
}

// Depth in real units, or as a fraction of a reference size. Vue keeps both
// because "10 metres" and "a tenth of this object" are different intentions
// (p717) and one of them survives rescaling the scene.
float displace_depth(const Node &self) {
  float d = self.attrs.get_f("depth", 1.f);
  if (self.attrs.get_choice("depth_mode") == 1)
    d *= self.attrs.get_f("relative_size", 1.f);
  return d;
}

} // namespace

// --------------------------------------------------------------- redirect
// Terragen's Redirect shader and Vue's warp, generalised: any vector field can
// move where any other field is evaluated. Our existing WarpNoise and
// WarpDirectional are the raster-domain special cases of exactly this.
REGISTER_NODE(
    FieldRedirect, "Field Displace",
    "Moves where another field is evaluated — warp, flow and distortion, on anything",
    [](Node &n) {
      n.add_field_in("input", FieldType::Number);
      n.add_field_in("redirect", FieldType::Vector, true);
      add_choice(n.attrs, "mode", "Mode",
                 {"Offset the position", "Replace the position"}, 0)
          .tooltip = "Offset moves the evaluation point by the vector.\n"
                     "Replace evaluates at the vector itself, which is how you\n"
                     "project one space onto another.";
      add_float(n.attrs, "strength", "Strength", 1.f, -32.f, 32.f);
      add_float(n.attrs, "scale_x", "Scale X", 1.f, -8.f, 8.f, "Per axis");
      add_float(n.attrs, "scale_y", "Scale Y", 1.f, -8.f, 8.f, "Per axis");
      add_float(n.attrs, "scale_z", "Scale Z", 1.f, -8.f, 8.f, "Per axis");
      n.add_field_out("out", FieldType::Number, [](const Node &self,
                                                   const FieldContext &ctx) {
        float off[3];
        self.in_field("redirect", ctx, FieldValue::vector(0, 0, 0)).as_vector(off);
        float s = self.attrs.get_f("strength", 1.f);
        float sx = self.attrs.get_f("scale_x", 1.f);
        float sy = self.attrs.get_f("scale_y", 1.f);
        float sz = self.attrs.get_f("scale_z", 1.f);
        FieldContext c = ctx;
        if (self.attrs.get_choice("mode") == 1) {
          c.pos[0] = off[0] * s * sx;
          c.pos[1] = off[1] * s * sy;
          c.pos[2] = off[2] * s * sz;
        } else {
          c.pos[0] = ctx.pos[0] + off[0] * s * sx;
          c.pos[1] = ctx.pos[1] + off[1] * s * sy;
          c.pos[2] = ctx.pos[2] + off[2] * s * sz;
        }
        // altitude travels with the point, or an altitude-driven graph would
        // read the height of somewhere it is no longer looking
        c.altitude = ctx.altitude + (c.pos[1] - ctx.pos[1]);
        return self.in_field("input", c, FieldValue(0.f));
      });
    },
    [](Node &) {})

// -------------------------------------------------------------- displace
REGISTER_NODE(
    FieldDisplace, "Field Displace",
    "Turns a value into relief: displaces along the normal, up, or any direction",
    [](Node &n) {
      n.add_field_in("amount", FieldType::Number);
      n.add_field_in("direction", FieldType::Vector, true);
      add_choice(n.attrs, "dir_mode", "Direction",
                 {"Along the surface normal", "Straight up", "Along the vector input",
                  "Along a fixed direction"},
                 0)
          .tooltip = "Along the normal gives relief that follows the surface,\n"
                     "which is what you want on a curved world. Straight up is\n"
                     "predictable and stacks cleanly.";
      add_choice(n.attrs, "depth_mode", "Depth is in",
                 {"Real units", "Relative to a size"}, 0)
          .tooltip = "Real units keep the displacement fixed when the scene is\n"
                     "rescaled; relative keeps its proportion.";
      add_float(n.attrs, "depth", "Depth", 1.f, -1000.f, 1000.f);
      add_float(n.attrs, "relative_size", "Reference size", 1.f, 0.001f, 1000.f)
          .tooltip = "The size 'relative' depth is a fraction of.";
      add_float(n.attrs, "smoothing", "Smoothing", 0.f, 0.f, 1.f, "Quality")
          .tooltip = "Softens the displacement by sampling around each point.\n"
                     "Costs four extra evaluations when above zero.";
      add_float(n.attrs, "smooth_radius", "Smoothing radius", 0.01f, 0.0001f, 1.f,
                "Quality");
      add_int(n.attrs, "quality", "Quality boost", 0, 0, 6, "Quality")
          .tooltip = "Extra octaves of detail for this displacement only, beyond\n"
                     "the caller's budget. Use when relief needs to be finer\n"
                     "than the geometry carrying it.";
      add_bool(n.attrs, "outwards_only", "Displace outwards only", false)
          .tooltip = "Discards negative displacement, so the surface can only\n"
                     "be pushed out and never dented inward.";
      add_float(n.attrs, "dir_x", "Direction X", 0.f, -1.f, 1.f, "Fixed direction");
      add_float(n.attrs, "dir_y", "Direction Y", 1.f, -1.f, 1.f, "Fixed direction");
      add_float(n.attrs, "dir_z", "Direction Z", 0.f, -1.f, 1.f, "Fixed direction");

      // The displaced height — what a terrain graph reads.
      n.add_field_out("out", FieldType::Number, [](const Node &self,
                                                   const FieldContext &ctx) {
        float d = displace_amount(self, ctx) * displace_depth(self);
        if (self.attrs.get_b("outwards_only")) d = std::max(d, 0.f);
        float dir[3];
        displace_direction(self, ctx, dir);
        return FieldValue(ctx.altitude + dir[1] * d);
      });
      // The full 3D offset — what true displacement of a surface needs.
      n.add_field_out("offset", FieldType::Vector, [](const Node &self,
                                                      const FieldContext &ctx) {
        float d = displace_amount(self, ctx) * displace_depth(self);
        if (self.attrs.get_b("outwards_only")) d = std::max(d, 0.f);
        float dir[3];
        displace_direction(self, ctx, dir);
        return FieldValue::vector(dir[0] * d, dir[1] * d, dir[2] * d);
      });
    },
    [](Node &) {})

// --------------------------------------------------------- compute normal
// Terragen's Compute Terrain / Compute Normal. Everything downstream of this
// node sees the displaced surface: its slope, its orientation, its normal. That
// is what makes "snow above this altitude, on slopes below this angle" mean the
// displaced terrain rather than the flat plane it started as.
REGISTER_NODE(
    FieldComputeNormal, "Field Displace",
    "Recovers the surface normal after displacement, so later nodes see the real shape",
    [](Node &n) {
      n.add_field_in("height", FieldType::Number);
      add_float(n.attrs, "epsilon", "Sample distance", 0.01f, 1e-5f, 1.f)
          .tooltip = "How far apart the samples are taken. Too small and the\n"
                     "normal is noise; too large and it smooths real detail\n"
                     "away. Roughly one pixel of the scale you care about.";
      add_float(n.attrs, "strength", "Strength", 1.f, 0.f, 64.f);
      add_bool(n.attrs, "flip", "Flip", false);
      n.add_field_out("normal", FieldType::Vector, [](const Node &self,
                                                      const FieldContext &ctx) {
        float e = std::max(self.attrs.get_f("epsilon", 0.01f), 1e-6f);
        FieldContext c = ctx;
        auto h = [&](float dx, float dz) {
          c.pos[0] = ctx.pos[0] + dx;
          c.pos[2] = ctx.pos[2] + dz;
          return self.in_number("height", c, 0.f);
        };
        // central differences: symmetric, so a constant slope gives an exact
        // normal rather than one biased in the sampling direction
        float gx = (h(e, 0) - h(-e, 0)) * self.attrs.get_f("strength", 1.f);
        float gz = (h(0, e) - h(0, -e)) * self.attrs.get_f("strength", 1.f);
        float nx = -gx, ny = 2.f * e, nz = -gz;
        float l = std::sqrt(nx * nx + ny * ny + nz * nz);
        if (l > 1e-9f) { nx /= l; ny /= l; nz /= l; }
        else { nx = 0.f; ny = 1.f; nz = 0.f; }
        if (self.attrs.get_b("flip")) { nx = -nx; ny = -ny; nz = -nz; }
        return FieldValue::vector(nx, ny, nz);
      });
      // Slope falls straight out of the normal, and asking for it directly is
      // far commoner than asking for the vector.
      n.add_field_out("slope", FieldType::Number, [](const Node &self,
                                                     const FieldContext &ctx) {
        float e = std::max(self.attrs.get_f("epsilon", 0.01f), 1e-6f);
        FieldContext c = ctx;
        auto h = [&](float dx, float dz) {
          c.pos[0] = ctx.pos[0] + dx;
          c.pos[2] = ctx.pos[2] + dz;
          return self.in_number("height", c, 0.f);
        };
        float s = self.attrs.get_f("strength", 1.f);
        float gx = (h(e, 0) - h(-e, 0)) * s;
        float gz = (h(0, e) - h(0, -e)) * s;
        float ny = 2.f * e;
        float l = std::sqrt(gx * gx + ny * ny + gz * gz);
        return FieldValue(l > 1e-9f ? ny / l : 1.f);
      });
    },
    [](Node &) {})

// -------------------------------------------------------------- texcoord
// The one standard field type with no producer until now. Texture placement in
// Vue (p738) is exactly this: choose a plane, scale it, rotate it, offset it.
REGISTER_NODE(
    FieldTexCoord, "Field Input",
    "Texture coordinates for this point — the input to any mapped texture",
    [](Node &n) {
      add_choice(n.attrs, "plane", "Projection",
                 {"Top down (XZ)", "Front (XY)", "Side (ZY)"}, 0);
      add_vec2(n.attrs, "scale", "Scale", 1.f, 1.f, -64.f, 64.f);
      add_vec2(n.attrs, "offset", "Offset", 0.f, 0.f, -64.f, 64.f);
      add_float(n.attrs, "angle", "Rotation °", 0.f, -180.f, 180.f);
      n.add_field_out("out", FieldType::TexCoord, [](const Node &self,
                                                     const FieldContext &ctx) {
        float u, v;
        switch (self.attrs.get_choice("plane")) {
          case 1: u = ctx.pos[0]; v = ctx.pos[1]; break;
          case 2: u = ctx.pos[2]; v = ctx.pos[1]; break;
          default: u = ctx.pos[0]; v = ctx.pos[2]; break;
        }
        float a = self.attrs.get_f("angle", 0.f) * 0.017453293f;
        float ca = std::cos(a), sa = std::sin(a);
        float ru = u * ca - v * sa;
        float rv = u * sa + v * ca;
        float sx, sy, ox, oy;
        self.attrs.get_vec2("scale", sx, sy);
        self.attrs.get_vec2("offset", ox, oy);
        return FieldValue::texcoord(ru * sx + ox, rv * sy + oy);
      });
    },
    [](Node &) {})

// ------------------------------------------------------------------ zone
// Vue's Zones (p536): confine detail to where it is needed. In the field domain
// this is a soft spatial mask between two fields, which also gives the Extract
// behaviour for free — take the mask output on its own.
REGISTER_NODE(
    FieldZone, "Field Displace",
    "Confines one field to a region, fading into another outside it",
    [](Node &n) {
      n.add_field_in("inside", FieldType::Number, true);
      n.add_field_in("outside", FieldType::Number, true);
      add_choice(n.attrs, "shape", "Shape", {"Sphere", "Box"}, 0);
      add_vec2(n.attrs, "center", "Centre (X,Z)", 0.f, 0.f, -1000.f, 1000.f,
               "Region");
      add_float(n.attrs, "center_y", "Centre Y", 0.f, -1000.f, 1000.f, "Region");
      add_float(n.attrs, "size", "Size", 1.f, 0.001f, 1000.f, "Region");
      add_float(n.attrs, "fade", "Fade", 0.25f, 0.f, 1.f, "Region")
          .tooltip = "Width of the transition, as a fraction of the size.\n"
                     "Zero gives a hard edge, which will show.";
      add_bool(n.attrs, "flat", "Ignore height", true)
          .tooltip = "On: the region is a column, so altitude does not matter.\n"
                     "Off: a true sphere or box in 3D.";

      // shared by both outputs: 1 inside, 0 outside, smooth between
      auto mask = [](const Node &self, const FieldContext &ctx) {
        float cx, cz;
        self.attrs.get_vec2("center", cx, cz);
        float cy = self.attrs.get_f("center_y", 0.f);
        float size = std::max(self.attrs.get_f("size", 1.f), 1e-6f);
        float dx = ctx.pos[0] - cx;
        float dy = self.attrs.get_b("flat", true) ? 0.f : ctx.pos[1] - cy;
        float dz = ctx.pos[2] - cz;
        float d;
        if (self.attrs.get_choice("shape") == 1)
          d = std::max(std::fabs(dx), std::max(std::fabs(dy), std::fabs(dz)));
        else
          d = std::sqrt(dx * dx + dy * dy + dz * dz);
        float fade = std::clamp(self.attrs.get_f("fade", 0.25f), 0.f, 1.f) * size;
        float inner = size - fade;
        if (d <= inner) return 1.f;
        if (d >= size) return 0.f;
        float t = fade > 1e-9f ? (size - d) / fade : 0.f;
        t = std::clamp(t, 0.f, 1.f);
        return t * t * (3.f - 2.f * t); // smoothstep, so the seam is invisible
      };
      n.add_field_out("out", FieldType::Number,
                      [mask](const Node &self, const FieldContext &ctx) {
                        float m = mask(self, ctx);
                        float a = self.in_number("inside", ctx, 1.f);
                        float b = self.in_number("outside", ctx, 0.f);
                        return FieldValue(b + (a - b) * m);
                      });
      n.add_field_out("mask", FieldType::Number,
                      [mask](const Node &self, const FieldContext &ctx) {
                        return FieldValue(mask(self, ctx));
                      });
    },
    [](Node &) {})

// ------------------------------------------------- the terrain displacement
// The sink that says "this field displaces the viewport terrain". It mirrors
// TerrainOutput: the graph declares the intent rather than the application
// guessing which node was meant, which is also what makes it discoverable in
// the node library.
//
// It computes nothing. The studio finds it, transpiles whatever feeds it to
// GLSL and hands that to the renderer, so the displacement is evaluated per
// vertex on the GPU rather than baked into a buffer. That is the entire point:
// a field has no resolution, so it keeps resolving as the camera closes in.
REGISTER_NODE(
    TerrainDisplacement, "Export",
    "Displaces the viewport terrain on the GPU from a field graph",
    [](Node &n) {
      n.add_field_in("field", FieldType::Number);
      add_float(n.attrs, "strength", "Strength", 0.05f, -2.f, 2.f)
          .tooltip = "How far the field moves the surface, in world units\n"
                     "(the terrain tile is 1 unit across).";
      add_bool(n.attrs, "live", "Update the viewport", true)
          .tooltip = "Off: keep the graph but stop displacing, without having\n"
                     "to disconnect it.";
    },
    [](Node &n) {
      // Nothing to compute — but say so clearly if it is not wired up, since
      // an unconnected sink silently doing nothing is hard to diagnose.
      if (!n.field_connected("field")) n.error = "input 'field' not connected";
    })

// ------------------------------------------- the infinite-surface displacement
// The same idea as TerrainDisplacement, one domain out. TerrainDisplacement
// shapes the finite terrain tile; this shapes everything that is *not* the
// tile - the endless ground plane that runs to the horizon, and the surface of
// every planet.
//
// Those surfaces have no heightmap and never will: they are evaluated on the
// GPU from parameters at whatever detail the camera needs, which is why a
// planet costs no memory. So the only way to author their shape is to author a
// function, and that is exactly what the field domain is. Wire a field here
// and it is transpiled to GLSL and added to the built-in layer stack, at every
// scale, everywhere, for free.
//
// The field is evaluated at a point on the unit sphere for a planet, and at
// (x, 0.37, z) in tile coordinates for the ground plane - the same coordinates
// the built-in layers use, so a field and a layer stack agree about where
// things are. `lod` carries the octave budget the camera has earned, so a
// graph that respects it stays sharp on approach and cheap at a distance.
REGISTER_NODE(
    SurfaceDisplacement, "Export",
    "Shapes planets and the infinite ground plane from a field graph",
    [](Node &n) {
      n.add_field_in("field", FieldType::Number);
      add_float(n.attrs, "strength", "Strength", 1.f, -4.f, 4.f)
          .tooltip = "Weight of this field against the built-in layers.\n"
                     "They span roughly -0.5..0.5 of the relief budget, so\n"
                     "1.0 makes the field as strong as a full layer.";
      add_bool(n.attrs, "live", "Update the viewport", true)
          .tooltip = "Off: keep the graph but stop shaping the surfaces,\n"
                     "without having to disconnect it.";
    },
    [](Node &n) {
      // Nothing to compute here - the studio transpiles what feeds it. But an
      // unwired sink that silently does nothing is hard to diagnose, so say so.
      if (!n.field_connected("field")) n.error = "input 'field' not connected";
    })

// ----------------------------------------------------------- the surface
// The colour counterpart of TerrainDisplacement, and the point of the
// distribution nodes: wire a colour field here and the viewport shades the
// terrain with it per pixel instead of with a baked texture or the built-in
// blend. Because it is evaluated per pixel from the shaded normal, a
// distribution keyed on steepness follows the displaced surface — including
// detail finer than the heightmap that carries it.
REGISTER_NODE(
    TerrainSurface, "Export",
    "Shades the viewport terrain from a field graph, per pixel on the GPU",
    [](Node &n) {
      // Vue's point about a function graph is that one graph produces several
      // channels at once (p770): the same distribution that decides where the
      // grass goes should decide that the grass is rougher than the rock.
      // Each channel is its own input, compiled to its own shader function.
      n.add_field_in("color", FieldType::Color);
      n.add_field_in("roughness", FieldType::Number, true);
      n.add_field_in("bump", FieldType::Number, true);
      add_float(n.attrs, "bump_strength", "Bump strength", 1.f, 0.f, 16.f)
          .tooltip = "How strongly the bump field tilts the surface normal.\n"
                     "This is shading only — it does not move the geometry,\n"
                     "which is what TerrainDisplacement is for.";
      add_float(n.attrs, "bump_scale", "Bump sample distance", 0.004f, 1e-4f,
                0.1f)
          .tooltip = "How far apart the bump is sampled. Too small and it is\n"
                     "noise; too large and it flattens.";
      add_bool(n.attrs, "live", "Update the viewport", true)
          .tooltip = "Off: keep the graph but go back to the usual shading,\n"
                     "without having to disconnect it.";
    },
    [](Node &n) {
      if (!n.field_connected("color")) n.error = "input 'color' not connected";
    })

} // namespace gpx
