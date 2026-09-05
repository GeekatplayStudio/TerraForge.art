#pragma once
#include <glad/gl.h>
#include <map>
#include <string>

namespace studio {
// Context-owner thread only. Program deletion must invalidate its locations:
// OpenGL may reuse the numeric name for a completely different program.
inline auto &uniform_cache() {
  static std::map<GLuint, std::map<std::string, GLint, std::less<>>> cache;
  return cache;
}
inline GLint uniform_location(GLuint program, const char *name) {
  auto &locations = uniform_cache()[program];
  auto it = locations.find(name);
  if (it != locations.end()) return it->second;
  GLint loc = glGetUniformLocation(program, name);
  locations.emplace(name, loc);
  return loc;
}
inline void delete_program(GLuint program) {
  uniform_cache().erase(program);
  glDeleteProgram(program);
}
} // namespace studio
