// Geekatplay TerraForge — the one place a shader's #version line is decided.
//
// Every shader in this repo is written as `#version 430 core`, which is what
// Windows and Linux drivers give us. macOS caps OpenGL at 4.1 core and never
// will ship 4.2 or 4.3, so on Apple the leading version line is rewritten to
// `#version 410 core` on its way to glShaderSource.
//
// That rewrite is only sound because none of our shaders uses a 4.2/4.3-only
// feature: no compute shaders, no shader storage buffers, no
// `layout(binding = ...)`, no explicit uniform locations, no immutable texture
// storage (glTexStorage*), no debug callback. Tessellation, which we do use,
// has been core since 4.0.
//
// If that ever stops being true, the shader fails at compile with a GLSL error
// naming the construct, and log_error prints it. That is the failure mode we
// want: loud, local, and pointing at the line — not a silently wrong picture
// (see AGENTS.md "Generated shaders").
//
// Every glShaderSource call site in the app goes through this: compile() in
// renderer_programs.cpp, pl_compile() in planet_renderer.cpp, and
// compile_or_report() in field_gpu_check.cpp. It lives in its own header
// rather than in renderer_internal.hpp because that header is private to the
// renderer_*.cpp family and field_gpu_check.cpp is not part of it.
#pragma once
#include <string>

namespace studio {

inline std::string glsl_for_platform(const char *src) {
  std::string s(src ? src : "");
#ifdef __APPLE__
  // Replace the first line, and only when it really is a version directive —
  // a source without one is left exactly as written.
  if (s.compare(0, 8, "#version") == 0) {
    size_t eol = s.find('\n');
    s.replace(0, eol == std::string::npos ? s.size() : eol,
              "#version 410 core");
  }
#endif
  return s;
}

} // namespace studio
