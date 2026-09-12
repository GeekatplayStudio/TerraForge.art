// Geekatplay TerraForge - writing OBJ text quickly, for the render export.
//
// A render hands the engines every scene mesh and the terrain as OBJ files,
// and the stream's own number formatting runs, under MinGW, through its printf
// one number at a time: the plants of one small scene cost the export seven
// seconds on the main thread, and the terrain grids more on top. This formats
// with std::to_chars into a buffer that is written in large pieces. The text
// is the same OBJ - numbers to the same significant digits - with plain line
// ends.
#pragma once
#include <charconv>
#include <fstream>
#include <string>

namespace studio {

class ObjText {
public:
  explicit ObjText(const std::string &path, int precision = 6)
      : file_(path, std::ios::binary), precision_(precision) {
    buf_.reserve(1 << 20);
  }
  ~ObjText() { flush(); }
  ObjText(const ObjText &) = delete;
  ObjText &operator=(const ObjText &) = delete;
  bool ok() const { return (bool)file_; }

  ObjText &text(const char *t) {
    buf_ += t;
    return spill();
  }
  ObjText &ch(char c) {
    buf_ += c;
    return spill();
  }
  ObjText &num(float v) {
    char tmp[32];
    const auto r = std::to_chars(tmp, tmp + sizeof tmp, v, std::chars_format::general, precision_);
    buf_.append(tmp, r.ptr);
    return spill();
  }
  ObjText &num(long long v) {
    char tmp[24];
    const auto r = std::to_chars(tmp, tmp + sizeof tmp, v);
    buf_.append(tmp, r.ptr);
    return spill();
  }
  ObjText &num(int v) { return num((long long)v); }
  // "v x y z\n" and its kin
  ObjText &line3(const char *tag, float a, float b, float c) {
    text(tag).ch(' ').num(a).ch(' ').num(b).ch(' ').num(c);
    return ch('\n');
  }
  void flush() {
    if (!buf_.empty()) {
      file_.write(buf_.data(), (std::streamsize)buf_.size());
      buf_.clear();
    }
  }

private:
  ObjText &spill() {
    if (buf_.size() > (1u << 20)) flush();
    return *this;
  }
  std::ofstream file_;
  std::string buf_;
  int precision_;
};

} // namespace studio
