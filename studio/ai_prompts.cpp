// Geekatplay TerraForge - what the studio adds to a prompt before a provider
// sees it. A texture must tile and be lit flat; a skydome must be an
// equirectangular 360 with the horizon at the middle; a 3D reference image
// must show one object on nothing. Image Express had no templates for these
// (it made posters); these are written for terrain work.
#include "ai_services.hpp"
#include "ai_describe.hpp"

namespace studio {

std::string ai_prompt_for(int kind, const std::string &user) {
  switch (kind) {
    case IMG_TEXTURE:
      return "Seamless tileable PBR texture, top-down orthographic view, flat even "
             "lighting, no shadows, no vignette, no borders, no text, photographic "
             "detail, repeats without visible seams. Subject: " + user;
    case IMG_SKYDOME:
      return "360 degree equirectangular panorama, 2:1, full sphere, horizon exactly "
             "at the vertical middle, seamless left-right edge, no watermark, no "
             "text, consistent lighting across the whole sky, photographic. Scene: " +
             user;
    default:
      return user;
  }
}

std::string ai_negative_for(int kind, const std::string &user) {
  std::string base;
  switch (kind) {
    case IMG_TEXTURE:
      base = "seams, borders, frame, text, watermark, logo, vignette, shadows, "
             "perspective, objects, people, blur, low quality";
      break;
    case IMG_SKYDOME:
      base = "seam, border, frame, text, watermark, logo, fisheye, cube, split, "
             "duplicated sun, people, blur, low quality";
      break;
    default:
      base = "text, watermark, logo, low quality, blurry";
  }
  return user.empty() ? base : base + ", " + user;
}

void ai_size_for(int kind, int &w, int &h) {
  if (kind == IMG_SKYDOME) {
    // 2:1, and wide enough that the horizon has detail
    if (w < 2048) w = 2048;
    h = w / 2;
  } else if (kind == IMG_TEXTURE) {
    if (w <= 0) w = 1024;
    h = w; // square tiles
  } else {
    if (w <= 0) w = 1024;
    if (h <= 0) h = 1024;
  }
}

int poll_next_delay_ms(int current_ms, bool progressed) {
  const int min_ms = 2000, max_ms = 15000;
  if (progressed || current_ms <= 0) return min_ms;
  int next = (int)(current_ms * 1.5);
  return next > max_ms ? max_ms : next;
}

std::string ai_describe_extract_actions(const std::string &reply) {
  // A model answers {"actions":[...]} when it listens and a bare [...] when
  // it does not; either becomes the object ai_apply_actions reads. Fences
  // and sentences around it are dropped.
  size_t obj = reply.find("{\"actions\"");
  if (obj == std::string::npos) obj = reply.find("{ \"actions\"");
  if (obj != std::string::npos) {
    size_t e = reply.rfind('}');
    if (e != std::string::npos && e > obj) return reply.substr(obj, e - obj + 1);
  }
  size_t a = reply.find('[');
  size_t b = reply.rfind(']');
  if (a == std::string::npos || b == std::string::npos || b < a) return "";
  return "{\"actions\":" + reply.substr(a, b - a + 1) + "}";
}

} // namespace studio
