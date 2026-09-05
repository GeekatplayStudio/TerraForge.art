// Geekatplay TerraForge — the expression language a track can be driven by.
//
// Small on purpose: the things After Effects' expressions and Blender's
// drivers are actually used for are arithmetic on time and on other
// properties. Recursive descent, no allocation beyond the token walk, and
// every function is deterministic — noise(x) is a hashed value noise, not a
// random number, so a frame always evaluates the same.
#include "gpx/animation.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <string>

namespace gpx {

namespace {

struct Parser {
  const std::string &s;
  const ExprContext &ctx;
  size_t i = 0;
  std::string err;

  Parser(const std::string &src, const ExprContext &c) : s(src), ctx(c) {}

  void ws() { while (i < s.size() && std::isspace((unsigned char)s[i])) ++i; }
  bool peek(char c) { ws(); return i < s.size() && s[i] == c; }
  bool eat(char c) { if (peek(c)) { ++i; return true; } return false; }

  static float hash01(uint32_t x) {
    x ^= x >> 16; x *= 0x7feb352dU; x ^= x >> 15; x *= 0x846ca68bU; x ^= x >> 16;
    return (float)(x & 0xffffff) / (float)0xffffff;
  }
  static float vnoise(float x) {
    float fl = std::floor(x), f = x - fl;
    f = f * f * (3.f - 2.f * f);
    uint32_t a = (uint32_t)(int)fl * 2654435761U, b = ((uint32_t)(int)fl + 1) * 2654435761U;
    return (hash01(a) + (hash01(b) - hash01(a)) * f) * 2.f - 1.f;
  }

  float call(const std::string &name, const std::vector<float> &a) {
    auto need = [&](size_t n) { if (a.size() < n) { err = name + " needs " + std::to_string(n) + " argument(s)"; return false; } return true; };
    if (name == "sin") return need(1) ? std::sin(a[0]) : 0.f;
    if (name == "cos") return need(1) ? std::cos(a[0]) : 0.f;
    if (name == "tan") return need(1) ? std::tan(a[0]) : 0.f;
    if (name == "abs") return need(1) ? std::fabs(a[0]) : 0.f;
    if (name == "sqrt") return need(1) ? std::sqrt(std::max(a[0], 0.f)) : 0.f;
    if (name == "floor") return need(1) ? std::floor(a[0]) : 0.f;
    if (name == "ceil") return need(1) ? std::ceil(a[0]) : 0.f;
    if (name == "round") return need(1) ? std::round(a[0]) : 0.f;
    if (name == "exp") return need(1) ? std::exp(a[0]) : 0.f;
    if (name == "log") return need(1) ? std::log(std::max(a[0], 1e-30f)) : 0.f;
    if (name == "noise") return need(1) ? vnoise(a[0]) : 0.f;
    if (name == "min") return need(2) ? std::min(a[0], a[1]) : 0.f;
    if (name == "max") return need(2) ? std::max(a[0], a[1]) : 0.f;
    if (name == "pow") return need(2) ? std::pow(a[0], a[1]) : 0.f;
    if (name == "clamp") return need(3) ? std::clamp(a[0], a[1], a[2]) : 0.f;
    if (name == "lerp") return need(3) ? a[0] + (a[1] - a[0]) * a[2] : 0.f;
    if (name == "smoothstep") {
      if (!need(3)) return 0.f;
      float u = std::clamp((a[2] - a[0]) / std::max(a[1] - a[0], 1e-9f), 0.f, 1.f);
      return u * u * (3.f - 2.f * u);
    }
    err = "unknown function " + name;
    return 0.f;
  }

  float variable(const std::string &name) {
    if (name == "t" || name == "time") return ctx.t;
    if (name == "frame") return ctx.t * ctx.fps;
    if (name == "fps") return ctx.fps;
    if (name == "value") return ctx.value;
    if (name == "pi") return 3.14159265358979f;
    float v = 0.f;
    if (ctx.lookup && ctx.lookup(name, v)) return v;
    if (err.empty()) err = "unknown name " + name;
    return 0.f;
  }

  float primary() {
    ws();
    if (i >= s.size()) { err = "unexpected end"; return 0.f; }
    char c = s[i];
    if (c == '(') {
      ++i;
      float v = expr();
      if (!eat(')')) err = "missing )";
      return v;
    }
    if (std::isdigit((unsigned char)c) || c == '.') {
      char *end = nullptr;
      float v = std::strtof(s.c_str() + i, &end);
      i = (size_t)(end - s.c_str());
      return v;
    }
    if (std::isalpha((unsigned char)c) || c == '_') {
      // a name, possibly dotted with spaces inside a quoted segment:
      // Camera 1.focal_mm is written "Camera 1".focal_mm or Camera_1.focal_mm
      std::string name;
      while (i < s.size()) {
        char d = s[i];
        if (std::isalnum((unsigned char)d) || d == '_' || d == '.' || d == ':' || d == '/') name += d, ++i;
        else break;
      }
      if (peek('(')) {
        ++i;
        std::vector<float> args;
        if (!peek(')')) {
          do args.push_back(expr()); while (eat(','));
        }
        if (!eat(')')) err = "missing ) after " + name;
        return call(name, args);
      }
      return variable(name);
    }
    if (c == '"') {
      // a quoted property path, with an optional .suffix after the quote
      ++i;
      std::string name;
      while (i < s.size() && s[i] != '"') name += s[i++];
      if (i < s.size()) ++i;
      while (i < s.size() && (std::isalnum((unsigned char)s[i]) || s[i] == '_' || s[i] == '.')) name += s[i++];
      return variable(name);
    }
    err = std::string("unexpected '") + c + "'";
    ++i;
    return 0.f;
  }

  float power() {
    float b = primary();
    if (eat('^')) return std::pow(b, unary());
    return b;
  }
  // -t^2 is -(t^2): the sign binds looser than the power
  float unary() {
    if (eat('-')) return -unary();
    if (eat('+')) return unary();
    return power();
  }
  float term() {
    float v = unary();
    while (true) {
      if (eat('*')) v *= unary();
      else if (eat('/')) { float d = unary(); v = std::fabs(d) < 1e-30f ? 0.f : v / d; }
      else if (eat('%')) { float d = unary(); v = std::fabs(d) < 1e-30f ? 0.f : std::fmod(v, d); }
      else return v;
    }
  }
  float expr() {
    float v = term();
    while (true) {
      if (eat('+')) v += term();
      else if (eat('-')) v -= term();
      else return v;
    }
  }
};

} // namespace

bool expr_eval(const std::string &expr, const ExprContext &ctx, float &out, std::string *err) {
  Parser p(expr, ctx);
  float v = p.expr();
  p.ws();
  if (p.err.empty() && p.i < expr.size()) p.err = "trailing characters";
  if (err) *err = p.err;
  if (!p.err.empty()) return false;
  if (!std::isfinite(v)) v = 0.f;
  out = v;
  return true;
}

} // namespace gpx
