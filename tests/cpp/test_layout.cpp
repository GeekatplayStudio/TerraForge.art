// Geekatplay TerraForge - saved window layouts (studio/layout_record.cpp).
//
// The promises, each asserted directly:
//   1. a layout round-trips: what is written is what comes back, including
//      the ImGui ini blob, which is the part that actually moves the windows;
//   2. an older or hand-edited file still loads - every field has a default,
//      because a layout that refuses to load leaves the user with no way back
//      to their arrangement;
//   3. a layout can never come back with no viewports open, whatever the file
//      says, since an application with no viewport is unusable;
//   4. names are safe as file names and still recognisable.
#include "layout_record.hpp"
#include <cassert>
#include <cstdio>
#include <string>

using namespace studio;

static int failures = 0;
static void check(bool ok, const char *what) {
  if (!ok) {
    std::printf("FAIL: %s\n", what);
    ++failures;
  }
}

static void test_round_trip() {
  LayoutRecord r;
  r.name = "Modelling";
  // A stand-in for ImGui's ini: multi-line, with the bracket syntax and the
  // blank lines it really contains.
  r.ini = "[Window][View 1]\nPos=0,40\nSize=960,540\nDockId=0x00000003,0\n\n";
  r.view_mask = 0b1011;
  for (int i = 0; i < 4; ++i) {
    LayoutRecord::View v;
    v.camera = i;
    v.display = i % 3;
    v.scene_camera = i - 2;
    v.atmosphere = (i % 2) == 0;
    v.water = i != 1;
    v.grid = i == 3;
    v.outlines = i != 2;
    r.views.push_back(v);
  }
  r.editor_domains = {1, 4};
  r.library = false;
  r.nodelist = true;
  r.properties = false;
  r.viewport = true;
  r.console = false;
  r.timeline = true;
  r.preview = false;
  r.material_editor = true;
  r.workspace = 3;

  std::string err;
  LayoutRecord back;
  check(layout_from_json(layout_to_json(r), back, err), "round trip parses");
  check(back.name == r.name, "name survives");
  check(back.ini == r.ini, "the ini blob survives byte for byte");
  check(back.view_mask == r.view_mask, "which viewports are open survives");
  check(back.views.size() == r.views.size(), "every view survives");
  for (size_t i = 0; i < back.views.size() && i < r.views.size(); ++i) {
    check(back.views[i].camera == r.views[i].camera, "view camera survives");
    check(back.views[i].display == r.views[i].display, "view display survives");
    check(back.views[i].scene_camera == r.views[i].scene_camera,
          "view scene camera survives");
    check(back.views[i].atmosphere == r.views[i].atmosphere,
          "view atmosphere survives");
    check(back.views[i].water == r.views[i].water, "view water survives");
    check(back.views[i].grid == r.views[i].grid, "view grid survives");
    check(back.views[i].outlines == r.views[i].outlines,
          "view outlines survives");
  }
  check(back.editor_domains == r.editor_domains, "extra node editors survive");
  check(back.library == r.library && back.nodelist == r.nodelist &&
            back.properties == r.properties && back.viewport == r.viewport &&
            back.console == r.console && back.timeline == r.timeline &&
            back.preview == r.preview &&
            back.material_editor == r.material_editor,
        "every panel flag survives");
  check(back.workspace == r.workspace, "the workspace survives");
}

static void test_partial_file_loads() {
  // A file from an older build: no panels, no views, no editors.
  LayoutRecord r;
  std::string err;
  check(layout_from_json("{\"name\":\"Old\",\"ini\":\"x\"}", r, err),
        "a file with only a name and an ini still loads");
  check(r.name == "Old", "the name is read");
  check(r.ini == "x", "the ini is read");
  check(r.library && r.properties && r.viewport,
        "absent panels keep their defaults rather than closing everything");
  check(r.views.empty(), "absent views stay empty rather than inventing any");
}

static void test_never_zero_viewports() {
  LayoutRecord r;
  std::string err;
  check(layout_from_json("{\"view_mask\":0}", r, err), "a zero mask loads");
  check(r.view_mask == 1,
        "a layout with no viewports opens View 1 instead of nothing");
}

static void test_bad_file_is_refused() {
  LayoutRecord r;
  std::string err;
  check(!layout_from_json("not json at all", r, err), "junk is refused");
  check(!err.empty(), "and says why");
  err.clear();
  check(!layout_from_json("[1,2,3]", r, err), "a JSON array is not a layout");
}

static void test_safe_names() {
  check(layout_safe_name("Modelling") == "Modelling", "a plain name is kept");
  check(layout_safe_name("Terrain / Materials") == "Terrain  Materials",
        "a path separator cannot survive in a layout name");
  check(layout_safe_name("../../etc/passwd") == "etcpasswd",
        "a traversal attempt cannot escape the layouts folder");
  check(layout_safe_name("  padded  ") == "padded",
        "leading and trailing spaces are trimmed");
  check(layout_safe_name("***") == "Layout",
        "a name with nothing usable in it still gets a file");
  check(layout_safe_name(std::string(200, 'x')).size() == 48,
        "an absurd name is cut to something a file system accepts");
  check(layout_safe_name("Wide 2x2 - v2_final") == "Wide 2x2 - v2_final",
        "digits, spaces, dashes and underscores all survive");
}

int main() {
  test_round_trip();
  test_partial_file_loads();
  test_never_zero_viewports();
  test_bad_file_is_refused();
  test_safe_names();
  if (failures) {
    std::printf("%d layout check(s) failed\n", failures);
    return 1;
  }
  std::printf("layout tests passed\n");
  return 0;
}
