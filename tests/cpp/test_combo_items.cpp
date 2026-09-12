// Geekatplay TerraForge - the list a dropdown is built from
// (studio/combo_items.hpp).
//
// This exists because it shipped broken. A panel separated the labels with
// '\\0' - a multi-character constant, a backslash and the digit zero, which
// narrows to the letter '0' - so ImGui read the whole list as one malformed
// item: the control showed flickering text and offered a single choice. The
// compiler said so twice (-Wmultichar, -Woverflow) and it went past in a
// build log.
//
// The promises:
//   1. a list of n labels is n NUL-terminated strings and nothing else;
//   2. reading it back the way ImGui does gives the labels, in order;
//   3. no empty item at the end - an extra NUL would make one, and ImGui
//      counts it as a choice;
//   4. an empty list is an empty buffer, not a buffer holding one blank.
#include "combo_items.hpp"
#include <cassert>
#include <cstdio>
#include <string>
#include <vector>

using namespace studio;

static int failures = 0;
static void check(bool ok, const char *what) {
  if (!ok) {
    std::printf("FAIL: %s\n", what);
    ++failures;
  }
}

// ImGui walks the buffer the way it walks any run of C strings: take the
// string at the cursor, step over it and its terminator, stop at an empty one.
static std::vector<std::string> as_imgui_reads(const std::string &items) {
  std::vector<std::string> out;
  const char *p = items.c_str();
  const char *end = items.c_str() + items.size();
  while (p < end && *p) {
    out.emplace_back(p);
    p += out.back().size() + 1;
  }
  return out;
}

int main() {
  {
    const std::vector<std::string> labels = {"Stratus", "Cumulus", "Cumulonimbus"};
    const std::string items = combo_items(labels);
    check(as_imgui_reads(items) == labels, "the labels come back, in order");
    // "Stratus\0Cumulus\0Cumulonimbus\0"
    check(items.size() == 7 + 1 + 7 + 1 + 12 + 1, "one NUL after each label, no more");
    check(items.find('0') == std::string::npos,
          "no digit zero anywhere - that was the bug");
    int nuls = 0;
    for (char c : items)
      if (c == '\0') ++nuls;
    check(nuls == (int)labels.size(), "exactly one separator per label");
  }
  {
    const std::string items = combo_items({"Draft", "Normal", "High"});
    const std::vector<std::string> read = as_imgui_reads(items);
    check(read.size() == 3, "three choices, not one");
    check(read[2] == "High", "the last label is whole");
  }
  {
    check(combo_items({}).empty(), "no labels is an empty buffer");
    check(as_imgui_reads(combo_items({})).empty(), "and reads as no choices");
  }
  {
    // a label may be empty; it must still occupy its place, and it ends the
    // list as far as ImGui is concerned - which is exactly why the builder
    // must not add one of its own
    const std::string items = combo_items({"On", "", "Off"});
    check(items.size() == 2 + 1 + 0 + 1 + 3 + 1, "an empty label still takes a NUL");
  }

  if (failures == 0) std::printf("combo items: all checks passed\n");
  return failures == 0 ? 0 : 1;
}
