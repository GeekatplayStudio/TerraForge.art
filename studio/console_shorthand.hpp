// Geekatplay TerraForge — turning a typed command into an action document.
//
// Kept apart from the console panel it serves (console_cmd.cpp) because this
// half has no window in it, and it is the half that can be wrong quietly: a
// mistranslation produces a well-formed document that means something other
// than what was typed, and the application then does that other thing without
// complaint. So it is tested (tests/cpp/test_console_shorthand.cpp).
#pragma once
#include <string>
#include <vector>

namespace studio {

// `add_node type=Noise octaves=9` -> `[{"op":"add_node","type":"Noise",
// "attrs":{"octaves":9}}]`, ready for ai_apply_actions. A line already
// starting with { or [ is returned unchanged: it is JSON already.
//
// Returns "" and sets err if the line cannot be read as a command.
std::string console_shorthand_to_json(const std::string &line, std::string &err);

// Whitespace-separated words, except that "quoted values" stay whole (paths
// have spaces in them) and a bracketed value survives the split (`k=[1, 2]`).
std::vector<std::string> console_tokenize(const std::string &s);

// The op names the terminal completes on, NUL-terminated array.
const char *const *console_common_ops();

} // namespace studio
