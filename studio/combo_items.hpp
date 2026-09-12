// Geekatplay TerraForge - a choice's labels, in the one shape ImGui reads.
//
// ImGui::Combo takes its items as a single buffer with the labels separated
// by NUL bytes, terminated by the string's own. Every panel that draws a
// Choice attribute has to build that, and one of them built it with '\\0' -
// which is not a NUL but a multi-character constant, a backslash and the
// digit zero, narrowed to the letter '0'. The whole list then read as one
// malformed item: the control showed flickering text and offered a single
// choice. The compiler warned (-Wmultichar, -Woverflow) and the warning went
// past in a build log.
//
// So there is one function, and it is tested. A panel that wants a combo
// calls this; there is nothing left to get wrong per panel.
#pragma once
#include <string>
#include <vector>

namespace studio {

// The labels as ImGui wants them. The final terminator is the std::string's
// own, which c_str() supplies - appending another would make an empty item
// that ImGui counts.
inline std::string combo_items(const std::vector<std::string> &labels) {
  std::string items;
  for (const std::string &l : labels) {
    items += l;
    items.push_back('\0');
  }
  return items;
}

} // namespace studio
