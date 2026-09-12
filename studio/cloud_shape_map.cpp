// Geekatplay TerraForge - the picture that says where the cloud is.
//
// Everything else about a cloud layer makes weather: noise opening and
// closing a cover over the whole sky. That gives a believable sky and no way
// at all to say "a cloud THERE, in that shape" - which is what anyone
// matching a plate, laying a front along a coast or standing a lenticular
// over a peak actually wants.
//
// So a layer may name an image. It is laid flat over the world, centred on
// the middle of the land (the world origin is the terrain tile's CORNER, and
// a picture laid there would miss the landscape), and read where each march
// sample stands: white is cloud, black
// is clear sky, mid grey leaves the coverage slider to decide. That is the
// same form the procedural weather already takes (cover * grey * 2), which is
// why one strength dial fades between them rather than switching.
//
// One channel is all that is read, so a painted mask, a photograph or a
// satellite picture all work. The file is loaded once and kept until the path
// changes; the texture lives for the life of the process because the sky pass
// binds it every frame and nothing else owns it.
#include "console.hpp"
#include "render_settings.hpp"
#include "stb_image.h"
#include "uniform_cache.hpp"
#include <glad/gl.h>
#include <string>

namespace studio {

namespace {

GLuint g_tex = 0;
int g_texels = 0;
std::string g_path;   // what g_tex holds
std::string g_failed; // what we already know will not load, so it is said once

// Wrap and border are texture state, and the "tile it" switch can change
// between frames, so they are set at bind time rather than at load. Beyond a
// picture laid once the sky is clear, which is what makes a single cloud
// possible at all: CLAMP_TO_EDGE would smear its edge row to the horizon.
void set_wrap(bool tiled) {
  const GLint mode = tiled ? GL_REPEAT : GL_CLAMP_TO_BORDER;
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, mode);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, mode);
  const float border[4] = {0.f, 0.f, 0.f, 0.f};
  glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, border);
}

// Load `path` if it is not already loaded. 0 when there is nothing to show.
// Context owner only.
GLuint shape_texture(const std::string &path, int &texels) {
  texels = 0;
  if (path.empty()) return 0;
  if (path == g_path) {
    texels = g_texels;
    return g_tex;
  }
  if (path == g_failed) return 0;

  int w = 0, h = 0, comp = 0;
  // One channel. A colour picture is reduced by stb's own luminance, which
  // weights green the way the eye does - right for a photograph, and harmless
  // for a painted mask, where the channels agree anyway.
  unsigned char *d = stbi_load(path.c_str(), &w, &h, &comp, 1);
  if (!d || w <= 0 || h <= 0) {
    if (d) stbi_image_free(d);
    g_failed = path;
    log_error("clouds", "cloud shape image will not load: " + path);
    return 0;
  }

  if (!g_tex) glGenTextures(1, &g_tex);
  glBindTexture(GL_TEXTURE_2D, g_tex);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, w, h, 0, GL_RED, GL_UNSIGNED_BYTE, d);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
  stbi_image_free(d);
  // Mips, because a march sample far off covers hundreds of the image's
  // pixels, and reading the finest level there is noise that crawls as the
  // camera moves - the same reason the shape volume is read by level.
  glGenerateMipmap(GL_TEXTURE_2D);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  set_wrap(false);
  glBindTexture(GL_TEXTURE_2D, 0);

  g_path = path;
  g_texels = w > h ? w : h;
  texels = g_texels;
  return g_tex;
}

} // namespace

// Bind the sky's shape image on `unit` and tell `prog` about it. It always
// binds something, so the sampler is never left reading whatever the last
// pass put there; u_cl_map_on is what decides whether it is read at all.
void cloud_shape_map_bind(GLuint prog, const RenderSettings &RS, int unit) {
  int texels = 0;
  const GLuint tex = shape_texture(RS.cloud_shape_map, texels);
  glActiveTexture((GLenum)(GL_TEXTURE0 + unit));
  glBindTexture(GL_TEXTURE_2D, tex);
  if (tex) set_wrap(RS.cloud_shape_tiled);
  glActiveTexture(GL_TEXTURE0);
  glUniform1i(uniform_location(prog, "u_cl_map"), unit);
  glUniform1i(uniform_location(prog, "u_cl_map_on"), tex ? 1 : 0);
  glUniform1f(uniform_location(prog, "u_cl_map_amt"), RS.cloud_shape_amount);
  glUniform1f(uniform_location(prog, "u_cl_map_size"), RS.cloud_shape_size);
  glUniform2fv(uniform_location(prog, "u_cl_map_center"), 1, RS.cloud_shape_center);
  glUniform1f(uniform_location(prog, "u_cl_map_texels"),
              (float)(texels > 0 ? texels : 1));
}

} // namespace studio
