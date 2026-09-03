// Geekatplay TerraForge — the converter contract: every field node produces
// the type its port promises, every field output (not only the first) has a
// GLSL twin that transpiles, and the explicit conversions agree with the
// implicit FieldValue ones they claim to expose. Linked into node_tests.
#include "gpx/color_math.hpp"
#include "gpx/field_glsl.hpp"
#include "gpx/node_graph.hpp"
#include <cmath>
#include <cstdio>
#include <string>

namespace convert_tests {

static int g_fail = 0, g_checks = 0;
#define CHECK(cond, msg)                                                       \
  do {                                                                         \
    ++g_checks;                                                                \
    if (!(cond)) {                                                             \
      std::printf("  [FAIL] %s (line %d)\n", std::string(msg).c_str(), __LINE__); \
      ++g_fail;                                                                \
    }                                                                          \
  } while (0)

static bool near(float a, float b, float eps = 1e-5f) { return std::fabs(a - b) <= eps; }

// A probe point with a normal that is not straight up, so orientation and
// slope carry information.
static gpx::FieldContext probe() {
  gpx::FieldContext c = gpx::FieldContext::at(0.37f, 0.21f, -1.8f);
  c.normal[0] = 0.3f; c.normal[1] = 0.9f; c.normal[2] = -0.31f;
  c.derive_from_normal();
  c.time = 2.25f;
  return c;
}

// The FieldValue conversions the converters are documented against.
static void test_fieldvalue_conversions() {
  std::printf("FieldValue conversions...\n");
  gpx::FieldValue col = gpx::FieldValue::color(0.2f, 0.4f, 0.8f, 0.5f);
  gpx::FieldValue vec = gpx::FieldValue::vector(3.f, 0.f, 4.f);
  gpx::FieldValue tc = gpx::FieldValue::texcoord(0.25f, 0.75f);
  gpx::FieldValue num(0.6f);
  CHECK(near(col.number(), 0.299f * 0.2f + 0.587f * 0.4f + 0.114f * 0.8f),
        "a colour reads as its luminance");
  CHECK(near(vec.number(), 5.f), "a vector reads as its length");
  CHECK(near(tc.number(), 0.25f), "texture coordinates read as u");
  float uv[2];
  vec.as_texcoord(uv);
  CHECK(near(uv[0], 3.f) && near(uv[1], 4.f), "a vector lies on the ground plane (x, z)");
  col.as_texcoord(uv);
  CHECK(near(uv[0], 0.2f) && near(uv[1], 0.4f), "a colour gives (r, g)");
  num.as_texcoord(uv);
  CHECK(near(uv[0], 0.6f) && near(uv[1], 0.6f), "a number fills both coordinates");
  float v3[3];
  num.as_vector(v3);
  CHECK(near(v3[0], 0.6f) && near(v3[2], 0.6f), "a number broadcasts to a vector");
  float c4[4];
  vec.as_color(c4);
  CHECK(near(c4[0], 5.f) && near(c4[3], 1.f), "a vector as a colour is grey of its length");
  // HSV round trip, the shared maths behind FieldColorHSV / FromHSV / Adjust
  const float rgb[3] = {0.7f, 0.2f, 0.45f};
  float hsv[3], back[3];
  gpx::rgb_to_hsv(rgb, hsv);
  gpx::hsv_to_rgb(hsv, back);
  CHECK(near(back[0], rgb[0], 1e-4f) && near(back[1], rgb[1], 1e-4f) &&
            near(back[2], rgb[2], 1e-4f),
        "rgb -> hsv -> rgb round-trips");
  const float grey[3] = {0.5f, 0.5f, 0.5f};
  gpx::rgb_to_hsv(grey, hsv);
  CHECK(near(hsv[0], 0.f) && near(hsv[1], 0.f) && near(hsv[2], 0.5f),
        "grey has hue 0 and no saturation");
}

// Wire one source into a converter and read one output.
static gpx::FieldValue run(const char *src_type, const char *conv_type,
                           const char *in_port, const char *out_port,
                           const char *attr = nullptr, int choice = 0) {
  gpx::Graph g;
  gpx::Node *s = g.add_node(src_type);
  gpx::Node *c = g.add_node(conv_type);
  if (!s || !c) return gpx::FieldValue(-999.f);
  if (attr)
    if (gpx::Attribute *a = c->attrs.find(attr)) a->i = choice;
  g.add_link(s->id, "out", c->id, in_port);
  return c->eval_field(out_port, probe());
}

static void test_converters_follow_the_rules() {
  std::printf("converter nodes follow the FieldValue rules...\n");
  gpx::FieldContext ctx = probe();
  // colour split: lanes and luminance of the constant colour (0.6, 0.55, 0.5)
  gpx::FieldValue r = run("FieldColorConstant", "FieldColorSplit", "color", "r");
  gpx::FieldValue l = run("FieldColorConstant", "FieldColorSplit", "color", "luminance");
  CHECK(near(r.number(), 0.6f), "ColorSplit.r is the red lane");
  CHECK(near(l.number(), 0.299f * 0.6f + 0.587f * 0.55f + 0.114f * 0.5f),
        "ColorSplit.luminance matches FieldValue::number()");
  // vector split of the position
  gpx::FieldValue z = run("FieldPosition", "FieldVectorSplit", "vector", "z");
  gpx::FieldValue len = run("FieldPosition", "FieldVectorSplit", "vector", "length");
  CHECK(near(z.number(), ctx.pos[2]), "VectorSplit.z is the position's z");
  CHECK(near(len.number(), std::sqrt(ctx.pos[0] * ctx.pos[0] + ctx.pos[1] * ctx.pos[1] +
                                     ctx.pos[2] * ctx.pos[2])),
        "VectorSplit.length is the vector's length");
  // texcoord split: FieldTexCoord defaults to the ground plane (x, z)
  gpx::FieldValue u = run("FieldTexCoord", "FieldTexCoordSplit", "uv", "u");
  gpx::FieldValue v = run("FieldTexCoord", "FieldTexCoordSplit", "uv", "v");
  CHECK(near(u.number(), ctx.pos[0]) && near(v.number(), ctx.pos[2]),
        "TexCoordSplit reads u = x, v = z");
  // FieldToNumber: Auto on a vector is its length; lane 3 of a colour is blue
  gpx::FieldValue n0 = run("FieldPosition", "FieldToNumber", "in", "out", "mode", 0);
  CHECK(near(n0.number(), len.number()), "ToNumber(Auto) of a vector is its length");
  gpx::FieldValue n3 = run("FieldColorConstant", "FieldToNumber", "in", "out", "mode", 3);
  CHECK(near(n3.number(), 0.5f), "ToNumber(third lane) of a colour is blue");
  gpx::FieldValue n4 = run("FieldPosition", "FieldToNumber", "in", "out", "mode", 4);
  CHECK(near(n4.number(), 1.f), "ToNumber(alpha) of a non-colour is 1");
  gpx::FieldValue n5 = run("FieldTexCoord", "FieldToNumber", "in", "out", "mode", 5);
  CHECK(near(n5.number(), std::max(ctx.pos[0], ctx.pos[2])),
        "ToNumber(largest) of texcoords looks at two lanes only");
  // FieldToColor of a vector remaps -1..1 to 0..1 by default
  gpx::FieldValue tcol = run("FieldNormal", "FieldToColor", "in", "out");
  CHECK(tcol.type == gpx::FieldType::Color, "ToColor produces a colour");
  CHECK(near(tcol.v[0], ctx.normal[0] * 0.5f + 0.5f), "ToColor remaps a normal like a normal map");
  // FieldToVector of texcoords lands on the chosen plane
  gpx::FieldValue tv = run("FieldTexCoord", "FieldToVector", "in", "out", "plane", 1);
  CHECK(tv.type == gpx::FieldType::Vector && near(tv.v[0], ctx.pos[0]) &&
            near(tv.v[1], ctx.pos[2]) && near(tv.v[2], 0.f),
        "ToVector(XY) puts (u, v) on x and y");
  // FieldToTexCoord of a vector on the side plane
  gpx::FieldValue tt = run("FieldPosition", "FieldToTexCoord", "in", "out", "plane", 2);
  CHECK(tt.type == gpx::FieldType::TexCoord && near(tt.v[0], ctx.pos[2]) &&
            near(tt.v[1], ctx.pos[1]),
        "ToTexCoord(ZY) reads (z, y)");
  // the vector output of FieldVectorOp agrees with its scalar output's X lane
  {
    gpx::Graph g;
    gpx::Node *p = g.add_node("FieldPosition");
    gpx::Node *op = g.add_node("FieldVectorOp");
    op->attrs.find("op")->i = 3; // normalize
    g.add_link(p->id, "out", op->id, "a");
    gpx::FieldValue sx = op->eval_field("out", ctx);
    gpx::FieldValue vv = op->eval_field("vec", ctx);
    CHECK(vv.type == gpx::FieldType::Vector, "VectorOp.vec is a vector");
    CHECK(near(sx.number(), vv.v[0]), "VectorOp.out is the X lane of VectorOp.vec");
    float L = std::sqrt(vv.v[0] * vv.v[0] + vv.v[1] * vv.v[1] + vv.v[2] * vv.v[2]);
    CHECK(near(L, 1.f, 1e-4f), "normalize yields a unit vector");
  }
  // animation: an oscillator at phase 0.25 of a sine is its amplitude
  {
    gpx::Graph g;
    gpx::Node *o = g.add_node("Oscillator");
    o->attrs.find("frequency")->f = 1.f;
    o->attrs.find("phase")->f = 0.25f;
    gpx::FieldContext c0 = probe();
    c0.time = 0.f;
    CHECK(near(o->eval_field("out", c0).number(), 1.f, 1e-5f),
          "sine at a quarter period peaks");
    gpx::Node *t = g.add_node("TimeRemap");
    t->attrs.find("loop")->f = 1.f;
    gpx::FieldContext c1 = probe();
    c1.time = 2.75f;
    CHECK(near(t->eval_field("out", c1).number(), 0.75f), "loop wraps time");
    t->attrs.find("pingpong")->b = true;
    c1.time = 1.25f;
    CHECK(near(t->eval_field("out", c1).number(), 0.75f), "ping-pong runs back");
  }
}

// Every field output port, not only the first, must have a GLSL emitter that
// produces a value: a secondary output with no emitter would fall back to the
// primary and silently return the wrong quantity on the GPU.
static void test_every_field_output_transpiles() {
  std::printf("every field output transpiles...\n");
  int ports = 0;
  for (const gpx::NodeDef *d : gpx::NodeRegistry::instance().all()) {
    gpx::Graph g;
    gpx::Node *n = g.add_node(d->type);
    if (!n) continue;
    for (const gpx::Port &p : n->ports) {
      if (p.dir != gpx::PortDir::Out || p.type != gpx::DataType::Field) continue;
      gpx::GlslProgram prog = gpx::field_to_glsl(*n, p.name, "gpx_t");
      CHECK(prog.ok, d->type + "." + p.name + " transpiles: " + prog.error);
      ++ports;
    }
  }
  CHECK(ports > 40, "the sweep saw the field outputs");
  // a converter's secondary outputs are different expressions, not copies of
  // the primary: r and b of the same colour must not transpile identically
  gpx::Graph g;
  gpx::Node *c = g.add_node("FieldColorConstant");
  gpx::Node *s = g.add_node("FieldColorSplit");
  g.add_link(c->id, "out", s->id, "color");
  std::string r = gpx::field_to_glsl(*s, "r", "f").code;
  std::string b = gpx::field_to_glsl(*s, "b", "f").code;
  CHECK(r != b, "ColorSplit.r and ColorSplit.b emit different code");
}

int run_all() {
  test_fieldvalue_conversions();
  test_converters_follow_the_rules();
  test_every_field_output_transpiles();
  std::printf("  converter contract: %d checks, %d failures\n", g_checks, g_fail);
  return g_fail;
}

} // namespace convert_tests

int test_nodes_convert_run() { return convert_tests::run_all(); }
