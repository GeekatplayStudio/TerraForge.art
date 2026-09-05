// Geekatplay TerraForge - the animation engine, without a window: key
// evaluation for every interpolation, auto-clamped tangents that never
// overshoot, extrapolation modes, modifiers, the expression language, the
// text form (old and new) and the curve operations. docs/ANIMATION.md
// parts A, C, E and F name what each group covers.
//
// Mutation half: a wrong expression must fail with a reason and never
// crash; a corrupt track string must load to an empty track; an operation
// on an empty selection must be a no-op; a baked curve must sample the
// same as the curve it replaced.
#include "gpx/animation.hpp"
#include <cmath>
#include <cstdio>
#include <string>

using namespace gpx;

static int failures = 0;
static void check(bool ok, const char *what) {
  if (!ok) {
    std::printf("FAIL: %s\n", what);
    ++failures;
  }
}
static bool near(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) <= eps; }

static void test_interpolation() {
  std::printf("interpolation...\n");
  Track t;
  t.interp = Interp::Bezier;
  t.set_key(0.f, 0.f);
  t.set_key(1.f, 10.f);
  check(near(t.sample(0.f), 0.f) && near(t.sample(1.f), 10.f), "keys hit exactly");
  float q = t.sample(0.25f), h = t.sample(0.5f);
  check(h > 4.9f && h < 5.1f, "two-key bezier is symmetric at the middle");
  check(q > 0.f && q < 5.f, "and monotonic");
  t.set_key(2.f, 10.f);
  // a key between two equal values: auto-clamped tangent, no overshoot
  for (float x = 1.f; x <= 2.f; x += 0.05f) check(t.sample(x) <= 10.f + 1e-4f, "auto-clamped: no overshoot between equal keys");
  // linear
  std::vector<int> all{0, 1, 2};
  anim::set_interp(t, all, Interp::Linear);
  check(near(t.sample(0.5f), 5.f), "linear halfway");
  anim::set_interp(t, all, Interp::Constant);
  check(near(t.sample(0.9f), 0.f) && near(t.sample(1.f), 10.f), "hold steps at the key");
  // per-key: only the first segment linear, second smooth
  anim::set_interp(t, {0}, Interp::Linear);
  anim::set_interp(t, {1}, Interp::Smooth);
  check(near(t.sample(0.5f), 5.f), "first segment linear");
  check(near(t.sample(1.5f), 10.f), "smooth between equal values stays put");
  // user tangents
  Track u;
  u.interp = Interp::Bezier;
  u.set_key(0.f, 0.f);
  u.set_key(1.f, 0.f);
  anim::flatten(u, {0, 1});
  check(near(u.sample(0.5f), 0.f), "flat tangents on equal keys: flat curve");
  u.keys[0].tangent = TangentMode::User;
  u.keys[0].tan_out = u.keys[0].tan_in = 4.f; // rises then must return
  check(u.sample(0.3f) > 0.f, "a user out-tangent bends the curve");
  u.keys[0].tangent = TangentMode::Broken;
  u.keys[0].tan_in = -4.f;
  check(u.sample(0.3f) > 0.f, "broken: in-tangent does not affect the outgoing segment");
  // ease presets
  Track e;
  e.interp = Interp::Bezier;
  e.set_key(0.f, 0.f);
  e.set_key(1.f, 1.f);
  anim::set_ease(e, {0, 1}, anim::Ease::Easy);
  float d0 = e.sample(0.05f), d1 = e.sample(0.95f);
  check(d0 < 0.05f && 1.f - d1 < 0.05f, "easy ease: slow at both ends");
  anim::set_ease(e, {0, 1}, anim::Ease::Linear);
  check(near(e.sample(0.25f), 0.25f), "linear preset");
}

static void test_extrapolation() {
  std::printf("extrapolation...\n");
  Track t;
  t.interp = Interp::Linear;
  t.set_key(0.f, 0.f);
  t.set_key(1.f, 2.f);
  check(near(t.sample(-1.f), 0.f) && near(t.sample(3.f), 2.f), "constant holds");
  t.post = Extrapolate::Linear;
  check(near(t.sample(2.f), 4.f), "linear continues the slope");
  t.post = Extrapolate::Cycle;
  check(near(t.sample(1.5f), 1.f) && near(t.sample(2.5f), 1.f), "cycle repeats");
  t.post = Extrapolate::CycleOffset;
  check(near(t.sample(1.5f), 3.f) && near(t.sample(2.5f), 5.f), "cycle with offset climbs");
  t.post = Extrapolate::PingPong;
  check(near(t.sample(1.5f), 1.f) && near(t.sample(1.f), 2.f) && near(t.sample(2.f), 0.f), "ping-pong reverses");
  t.pre = Extrapolate::Cycle;
  check(near(t.sample(-0.5f), 1.f), "pre-cycle");
}

static void test_modifiers() {
  std::printf("modifiers...\n");
  Track t;
  t.set_key(0.f, 1.f);
  Modifier m;
  m.type = ModType::Oscillator;
  m.a = 0.5f; m.b = 1.f; m.c = 0.f;
  t.modifiers.push_back(m);
  check(near(t.sample(0.25f), 1.5f, 1e-3f), "sine oscillator at a quarter phase is +amplitude");
  t.modifiers[0].enabled = false;
  check(near(t.sample(0.25f), 1.f), "a disabled modifier does nothing");
  t.modifiers.clear();
  m.type = ModType::Noise; m.a = 1.f; m.b = 2.f; m.octaves = 3; m.seed = 7;
  t.modifiers.push_back(m);
  float n1 = t.sample(0.37f), n2 = t.sample(0.37f);
  check(n1 == n2, "noise is deterministic");
  t.modifiers[0].seed = 8;
  check(t.sample(0.37f) != n1, "a different seed is a different noise");
  bool bounded = true;
  for (float x = 0; x < 10.f; x += 0.01f) bounded = bounded && std::fabs(t.sample(x) - 1.f) <= 1.f + 1e-4f;
  check(bounded, "noise stays within its amplitude");
  t.modifiers.clear();
  m.type = ModType::Limit; m.a = 0.f; m.b = 0.5f;
  t.modifiers.push_back(m);
  check(near(t.sample(3.f), 0.5f), "limit clamps");
  t.modifiers.clear();
  Track s;
  s.interp = Interp::Constant;
  s.set_key(0.f, 0.f);
  s.set_key(1.f, 1.f);
  m.type = ModType::Smooth; m.a = 0.5f;
  s.modifiers.push_back(m);
  float sm = s.sample(1.f);
  check(sm > 0.3f && sm < 1.f, "smooth averages across the step");
  m.type = ModType::Offset; m.a = 0.f; m.b = -0.5f;
  s.modifiers.clear();
  s.modifiers.push_back(m);
  check(near(s.sample(1.2f), 0.f), "time offset reads the curve earlier");
}

static void test_expressions() {
  std::printf("expressions...\n");
  ExprContext c;
  c.t = 2.f;
  c.fps = 24.f;
  c.value = 3.f;
  c.lookup = [](const std::string &n, float &v) { if (n == "Rock.pos.x") { v = 0.75f; return true; } return false; };
  float v;
  std::string err;
  check(expr_eval("1 + 2 * 3", c, v) && near(v, 7.f), "precedence");
  check(expr_eval("(1 + 2) * 3", c, v) && near(v, 9.f), "parentheses");
  check(expr_eval("-t ^ 2", c, v) && near(v, -4.f), "unary minus and power");
  check(expr_eval("frame / fps", c, v) && near(v, 2.f), "frame and fps");
  check(expr_eval("value * 2 + sin(0)", c, v) && near(v, 6.f), "value and a function");
  check(expr_eval("clamp(t, 0, 1) + lerp(0, 10, 0.5) + max(1, 2)", c, v) && near(v, 8.f), "clamp lerp max");
  check(expr_eval("Rock.pos.x * 4", c, v) && near(v, 3.f), "a property path through the lookup");
  check(expr_eval("\"Rock\".pos.x", c, v) && near(v, 0.75f), "a quoted path");
  check(expr_eval("noise(1.5) == noise(1.5)" , c, v) == false, "== is not an operator: fails cleanly");
  check(!expr_eval("1 +", c, v, &err) && !err.empty(), "a dangling operator is an error with a message");
  check(!expr_eval("foo(1)", c, v, &err) && err.find("unknown function") != std::string::npos, "unknown function names itself");
  check(!expr_eval("nothing", c, v, &err) && err.find("unknown name") != std::string::npos, "unknown name names itself");
  check(expr_eval("1 / 0", c, v) && v == 0.f, "division by zero is zero, not infinity");
  check(expr_eval("smoothstep(0, 1, 0.5)", c, v) && near(v, 0.5f), "smoothstep");
  Track t;
  t.set_key(0.f, 1.f);
  t.expr = "value + t";
  check(near(t.sample(2.f, c), 3.f), "an expression sees the curve as value");
  t.expr = "1 +";
  check(near(t.sample(2.f, c), 1.f), "a broken expression falls back to the curve");
}

static void test_serialization() {
  std::printf("text form...\n");
  Track t;
  check(track_from_string(t, "1;0,5;2,7") && t.keys.size() == 2 && t.interp == Interp::Smooth, "the old form loads");
  check(near(t.sample(1.f), 6.f), "and samples as it did (smoothstep midpoint)");
  Track full;
  full.interp = Interp::Bezier;
  full.pre = Extrapolate::Linear;
  full.post = Extrapolate::PingPong;
  full.set_key(0.f, 1.f);
  full.set_key(1.f, 4.f);
  full.set_key(2.5f, -2.f);
  full.keys[1].interp = Interp::Constant;
  full.keys[2].tangent = TangentMode::Broken;
  full.keys[2].tan_in = 3.f;
  full.keys[2].tan_out = -1.f;
  Modifier m; m.type = ModType::Oscillator; m.a = 0.25f; m.b = 2.f; m.c = 0.1f; m.shape = 1; m.seed = 42;
  full.modifiers.push_back(m);
  full.expr = "value + sin(t) | 1; 2, 3"; // separators inside the expression
  std::string s = track_to_string(full);
  check(s.rfind("K2;", 0) == 0, "the new form is marked");
  Track back;
  check(track_from_string(back, s), "the new form loads");
  check(back.keys.size() == 3 && back.pre == Extrapolate::Linear && back.post == Extrapolate::PingPong, "header kept");
  check(back.keys[1].interp == Interp::Constant && back.keys[0].interp == Interp::Default, "per-key interpolation kept");
  check(back.keys[2].tangent == TangentMode::Broken && near(back.keys[2].tan_in, 3.f) && near(back.keys[2].tan_out, -1.f), "tangents kept");
  check(back.modifiers.size() == 1 && back.modifiers[0].shape == 1 && back.modifiers[0].seed == 42, "modifiers kept");
  check(back.expr == full.expr, "the expression survives its own separators");
  for (float x = -1.f; x < 4.f; x += 0.1f) if (!near(back.sample(x), full.sample(x), 1e-4f)) { check(false, "round trip samples identically"); break; }
  check(track_to_string(back) == s, "writing again is byte-identical");
  Track junk;
  check(track_from_string(junk, "K2;garbage") == false || junk.keys.empty(), "garbage after the marker does not crash");
  check(track_from_string(junk, "") && junk.empty(), "empty is empty");
}

static void test_curve_ops() {
  std::printf("curve operations...\n");
  Track t;
  t.interp = Interp::Linear;
  for (int i = 0; i < 5; ++i) t.set_key((float)i, (float)(i * i));
  std::vector<int> sel{1, 2};
  anim::move_keys(t, sel, 0.5f, 1.f);
  check(t.keys.size() == 5 && near(t.keys[1].time, 1.5f) && near(t.keys[1].value, 2.f), "moved keys land where asked");
  check(sel.size() == 2 && near(t.keys[(size_t)sel[0]].time, 1.5f), "the selection follows");
  // moving onto another key merges
  std::vector<int> one{1};
  anim::move_keys(t, one, 1.5f, 0.f); // 1.5 -> 3.0, where a key sits
  check(t.keys.size() == 4, "a key moved onto another replaces it");
  Track r;
  r.interp = Interp::Linear;
  r.set_key(0.f, 0.f); r.set_key(1.f, 1.f); r.set_key(2.f, 0.f);
  std::vector<int> all{0, 1, 2};
  anim::retime(r, all, 0.f, 2.f);
  check(near(r.last_time(), 4.f) && near(r.keys[1].time, 2.f), "retime scales about the pivot");
  anim::mirror(r, all);
  check(near(r.keys[0].time, 0.f) && near(r.keys[2].time, 4.f) && near(r.keys[1].value, 1.f), "mirror keeps the span");
  Track b;
  b.interp = Interp::Bezier;
  b.set_key(0.f, 0.f); b.set_key(1.f, 1.f);
  Modifier m; m.type = ModType::Oscillator; m.a = 0.2f; m.b = 3.f;
  b.modifiers.push_back(m);
  Track ref = b;
  anim::bake(b, 30.f);
  check(b.keys.size() == 31 && b.modifiers.empty(), "bake: one key per frame, modifiers folded in");
  bool same = true;
  for (int i = 0; i <= 30; ++i) same = same && near(b.sample(i / 30.f), ref.sample(i / 30.f), 1e-4f);
  check(same, "baked samples equal the source at every frame");
  Track sm = b;
  anim::simplify(sm, 0.02f, 30.f);
  check(sm.keys.size() < b.keys.size() && sm.keys.size() >= 2, "simplify removes keys");
  bool close = true;
  for (int i = 0; i <= 30; ++i) close = close && std::fabs(sm.sample(i / 30.f) - ref.sample(i / 30.f)) < 0.1f;
  check(close, "and stays near the curve");
  Track sn;
  sn.set_key(0.013f, 1.f); sn.set_key(0.01f, 2.f); sn.set_key(1.f, 3.f); // both under half a frame at 30 fps
  anim::snap_to_frames(sn, 30.f);
  check(sn.keys.size() == 2 && near(sn.keys[0].time, 0.f), "snap merges keys that land on the same frame");
  std::vector<Key> cp = anim::copy_keys(r, {0, 1});
  check(cp.size() == 2 && near(cp[0].time, 0.f), "copy is relative to the first key");
  Track dst;
  anim::paste_keys(dst, cp, 10.f);
  check(dst.keys.size() == 2 && near(dst.keys[0].time, 10.f), "paste lands at the frame");
  std::vector<int> none;
  anim::move_keys(dst, none, 1.f, 1.f);
  check(dst.keys.size() == 2 && near(dst.keys[0].time, 10.f), "an empty selection is a no-op");
}

static void test_timeline() {
  std::printf("timeline...\n");
  Timeline tl;
  tl.fps = 24.f;
  check(tl.format(1.f) == "24", "frames");
  tl.display = TimeDisplay::Timecode;
  check(tl.format(3661.f + 5.f / 24.f) == "01:01:01:05", "timecode");
  tl.display = TimeDisplay::Seconds;
  check(tl.format(1.5f) == "1.500 s", "seconds");
  float t;
  tl.display = TimeDisplay::Frames;
  check(tl.parse("48", t) && near(t, 2.f), "parse frames");
  check(tl.parse("00:00:02:12", t) && near(t, 2.5f), "parse timecode");
  check(tl.parse("1.5s", t) && near(t, 1.5f), "parse seconds");
  check(!tl.parse("", t) && !tl.parse("abc", t), "junk fails");
  check(near(tl.snap_time(0.03f), 1.f / 24.f) && near(tl.snap_time(0.0208f), 0.f), "snap to the nearest frame, either way");
  tl.snap = false;
  check(near(tl.snap_time(0.03f), 0.03f), "snap off leaves time alone");
  tl.preview = true; tl.preview_start = 1.f; tl.preview_end = 2.f;
  check(near(tl.play_start(), 1.f) && near(tl.play_end(), 2.f), "the preview range is the play range");
}

int main() {
  test_interpolation();
  test_extrapolation();
  test_modifiers();
  test_expressions();
  test_serialization();
  test_curve_ops();
  test_timeline();
  if (failures) {
    std::printf("%d animation check(s) failed\n", failures);
    return 1;
  }
  std::printf("animation tests passed\n");
  return 0;
}
