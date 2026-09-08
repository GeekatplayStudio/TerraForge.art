// Geekatplay TerraForge — the console's command line.
//
// The console was a log you could read. This makes it a terminal you can type
// into, which matters because the whole scripting API — every op the AI
// assistant and the MCP server can issue — was reachable from a network socket
// and from a language model, and not from the application it drives.
//
// The panel half: the input box, the history, Tab completion and the built-in
// commands. Turning what was typed into an action document is
// console_shorthand.cpp, which has no window in it and is tested.
//
// Keys the op does not have are now reported rather than swallowed
// (ai_actions.cpp), so a typo here says so instead of appearing to work.
#include "app.hpp"
#include "ai_assist.hpp"
#include "console.hpp"
#include "console_shorthand.hpp"
#include <imgui.h>
#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

namespace studio {

namespace {

struct CmdState {
  std::vector<std::string> history;
  int browse = -1;          // -1 = not browsing, else index from the end
  std::string pending;      // what was typed before Up started browsing
  bool focus_next = false;
  char buf[512] = "";
};

CmdState &cmd_state() {
  static CmdState s;
  return s;
}

int history_callback(ImGuiInputTextCallbackData *data) {
  CmdState &S = cmd_state();
  if (data->EventFlag == ImGuiInputTextFlags_CallbackHistory) {
    if (S.history.empty()) return 0;
    if (S.browse < 0) S.pending = data->Buf;
    const int last = (int)S.history.size() - 1;
    if (data->EventKey == ImGuiKey_UpArrow)
      S.browse = S.browse < 0 ? last : std::max(S.browse - 1, 0);
    else if (data->EventKey == ImGuiKey_DownArrow) {
      if (S.browse < 0) return 0;
      if (S.browse >= last) {
        S.browse = -1;
        data->DeleteChars(0, data->BufTextLen);
        data->InsertChars(0, S.pending.c_str());
        return 0;
      }
      ++S.browse;
    }
    data->DeleteChars(0, data->BufTextLen);
    data->InsertChars(0, S.history[S.browse].c_str());
  } else if (data->EventFlag == ImGuiInputTextFlags_CallbackCompletion) {
    // complete the op name, which is the only word with a fixed vocabulary
    std::string cur = data->Buf;
    if (cur.find(' ') != std::string::npos) return 0;
    const char *hit = nullptr;
    int hits = 0;
    for (const char *const *o = console_common_ops(); *o; ++o)
      if (std::string(*o).rfind(cur, 0) == 0) {
        hit = *o;
        ++hits;
      }
    if (hits == 1 && hit) {
      data->DeleteChars(0, data->BufTextLen);
      data->InsertChars(0, hit);
      data->InsertChars(data->CursorPos, " ");
    } else if (hits > 1) {
      // more than one match: say which, the way a shell does
      std::string all;
      for (const char *const *o = console_common_ops(); *o; ++o)
        if (std::string(*o).rfind(cur, 0) == 0) all += std::string(*o) + "  ";
      log_info("console", all);
    }
  }
  return 0;
}

void run_line(App &a, const std::string &line) {
  log_info("console", "> " + line);
  if (line == "help" || line == "?") {
    log_info("console",
             "add_node type=Noise octaves=9   |   set_attr node=Noise key=octaves value=7\n"
             "connect from=Noise to=Hydraulic to_port=heightmap\n"
             "find_nodes query=erosion   |   view_node node=Hydraulic   |   undo\n"
             "Anything starting with { or [ is passed through as JSON.\n"
             "Tab completes the op name, Up/Down walks the history.");
    log_info("console", ai_action_schema(AiDomain::Terrain));
    return;
  }
  if (line == "clear" || line == "cls") {
    log_clear();
    return;
  }
  if (line == "ops") {
    std::string all;
    for (const char *const *o = console_common_ops(); *o; ++o)
      all += std::string(*o) + "  ";
    log_info("console", all);
    return;
  }
  std::string err;
  const std::string js = console_shorthand_to_json(line, err);
  if (js.empty()) {
    log_error("console", err.empty() ? "nothing to run" : err);
    return;
  }
  // Several ops answer into the status line — find_nodes lists what matched,
  // probe_height gives a number, the verifiers give a report. The status bar
  // is one line and shows the first of them; a terminal that answered "ok" to
  // a question would be useless, so whatever the op said is printed here.
  const std::string before = a.status;
  const bool ok = ai_apply_actions(a, js, err);
  if (a.status != before && !a.status.empty())
    log_info("console", a.status);
  else if (ok)
    log_info("console", "ok");
  if (!ok) log_error("console", err.empty() ? "failed" : err);
}

} // namespace

// Drawn at the bottom of the console panel, below the log.
void console_command_line(App &a) {
  CmdState &S = cmd_state();
  ImGui::Separator();
  ImGui::TextUnformatted(">");
  ImGui::SameLine(0, 4);
  ImGui::SetNextItemWidth(-1);
  const ImGuiInputTextFlags flags =
      ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CallbackHistory |
      ImGuiInputTextFlags_CallbackCompletion;
  if (S.focus_next) {
    ImGui::SetKeyboardFocusHere();
    S.focus_next = false;
  }
  if (ImGui::InputTextWithHint("##cmdline",
                               "add_node type=Noise octaves=9   (help, Tab, \xe2\x86\x91)",
                               S.buf, sizeof S.buf, flags, history_callback)) {
    std::string line = S.buf;
    while (!line.empty() && std::isspace((unsigned char)line.back())) line.pop_back();
    if (!line.empty()) {
      if (S.history.empty() || S.history.back() != line) S.history.push_back(line);
      S.browse = -1;
      run_line(a, line);
    }
    S.buf[0] = '\0';
    S.focus_next = true; // keep typing, the way a terminal does
  }
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("Runs the same operations the API and the assistant use.\n"
                      "`help` lists them. Every change is one undo step.");
}

} // namespace studio
