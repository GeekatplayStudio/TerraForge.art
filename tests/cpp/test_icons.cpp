// Geekatplay TerraForge - the icon set, without a window.
//
// Every id paints at least one primitive (there is no default glyph to hide
// behind), every vertex stays inside the requested box at each step of the
// 18 / 26 / 36 px ladder, and drawing an icon twice gives the same vertex
// buffer - a glyph must not depend on anything but its inputs.
#include "icons.hpp"
#include "prefs.hpp"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <imgui.h>
#include <imgui_internal.h>
#include <vector>

using namespace studio;

static int failures = 0;
static void check(bool ok, const char *what) {
  if (!ok) {
    std::printf("FAIL: %s\n", what);
    ++failures;
  }
}

static const char *icon_name(int i) {
  static const char *names[] = {
      "Undo", "Redo", "Refresh", "Brush", "Wireframe", "Grid", "Sky", "Water",
      "Camera", "Planet", "Terrain", "Light", "Cloud", "Mesh", "Folder", "Eye",
      "EyeOff", "Plus", "Minus", "Trash", "Gear", "Search", "Chevron",
      "ChevronDown", "Link", "Unlink", "Save", "Open", "Move", "Rotate", "Scale",
      "Material", "Node", "Render", "Scene", "World", "Object", "ViewPersp",
      "ViewTop", "ViewFront", "ViewRight", "Shaded", "Textured", "Outline",
      "Detach", "Dock", "Lock", "Unlock", "Twist", "Bend", "Skew", "Taper",
      "Play", "Pause", "Stop", "ToStart", "ToEnd", "PrevKey", "NextKey",
      "KeyAdd", "KeyRemove", "Autokey", "Loop", "Marker", "Curve", "Timeline",
      "Dot", "DotRing", "Check", "Cross", "Layer", "Tag", "Filter", "Home", "Up",
      "Sun", "Atmosphere", "Group", "Null", "Expression", "Modifier", "Bake",
      "Fit", "Snap", "Magnet"};
  static_assert(sizeof(names) / sizeof(names[0]) == (size_t)Icon::Count,
                "name every icon id");
  return names[i];
}

// A fresh draw list ready to take primitives.
static void reset(ImDrawList &dl) {
  dl._ResetForNewFrame();
  dl.PushTexture(ImGui::GetIO().Fonts->TexRef);
  dl.PushClipRectFullScreen();
}

static void test_icons_all() {
  std::printf("icons at 18 / 26 / 36...\n");
  const int *ladder = icon_size_ladder();
  check(ladder[0] == 18 && ladder[1] == 26 && ladder[2] == 36, "the ladder is 18 / 26 / 36");
  ImDrawList a(ImGui::GetDrawListSharedData());
  ImDrawList b(ImGui::GetDrawListSharedData());
  char msg[256];
  for (int step = 0; step < 3; ++step) {
    float size = (float)ladder[step];
    ImVec2 c(100.f, 100.f);
    for (int i = 0; i < (int)Icon::Count; ++i) {
      Icon ic = (Icon)i;
      reset(a);
      icon_draw(&a, ic, c, size, IM_COL32_WHITE);
      std::snprintf(msg, sizeof msg, "%s at %d paints something", icon_name(i), (int)size);
      check(a.VtxBuffer.Size > 0 && a.IdxBuffer.Size > 0, msg);

      float lo = 0.f, hi = 0.f;
      for (int v = 0; v < a.VtxBuffer.Size; ++v) {
        ImVec2 p = a.VtxBuffer[v].pos;
        lo = std::fmin(lo, std::fmin(p.x - c.x, p.y - c.y));
        hi = std::fmax(hi, std::fmax(p.x - c.x, p.y - c.y));
      }
      float lim = size * 0.5f + 1.f;
      std::snprintf(msg, sizeof msg, "%s at %d stays in its box (extent %.1f..%.1f, limit %.1f)",
                    icon_name(i), (int)size, lo, hi, lim);
      check(lo >= -lim && hi <= lim, msg);

      reset(b);
      icon_draw(&b, ic, c, size, IM_COL32_WHITE);
      bool same = a.VtxBuffer.Size == b.VtxBuffer.Size && a.IdxBuffer.Size == b.IdxBuffer.Size &&
                  std::memcmp(a.VtxBuffer.Data, b.VtxBuffer.Data,
                              a.VtxBuffer.Size * sizeof(ImDrawVert)) == 0 &&
                  std::memcmp(a.IdxBuffer.Data, b.IdxBuffer.Data,
                              a.IdxBuffer.Size * sizeof(ImDrawIdx)) == 0;
      std::snprintf(msg, sizeof msg, "%s at %d is deterministic", icon_name(i), (int)size);
      check(same, msg);
    }
  }
}

static void test_toolbar_size() {
  std::printf("toolbar size...\n");
  prefs().icon_size = 0;
  check(icon_toolbar_size() == 18.f, "small is 18");
  prefs().icon_size = 1;
  check(icon_toolbar_size() == 26.f, "medium is 26");
  prefs().icon_size = 2;
  check(icon_toolbar_size() == 36.f, "large is 36");
  prefs().icon_size = 7;
  check(icon_toolbar_size() == 36.f, "an index off the ladder clamps");
  prefs().icon_size = 0;
}

int main() {
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  ImGui::CreateContext();
  ImGuiIO &io = ImGui::GetIO();
  io.DisplaySize = ImVec2(400.f, 400.f);
  io.Fonts->AddFontDefault();
  // no renderer backend: build the atlas the legacy way so NewFrame has one
  unsigned char *pixels = nullptr;
  int tw = 0, th = 0;
  io.Fonts->GetTexDataAsRGBA32(&pixels, &tw, &th);
  ImGui::NewFrame();
  test_icons_all();
  test_toolbar_size();
  ImGui::EndFrame();
  ImGui::DestroyContext();
  if (failures) {
    std::printf("%d icon check(s) failed\n", failures);
    return 1;
  }
  std::printf("icon tests passed: %d icons x 3 sizes\n", (int)Icon::Count);
  return 0;
}
