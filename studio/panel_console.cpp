// Geekatplay TerraForge — the message log panel.
//
// Selectable and copyable, because the point of the whole thing is that a
// message can be quoted in a bug report. Errors used to be tooltips that
// vanished on mouse-out.
#include "app.hpp"
#include "panel_float.hpp"
#include "console.hpp"
#include "theme_colors.hpp"
#include <imgui.h>
#include <cstdio>
#include <string>

namespace studio {

// file_dialogs.cpp
std::string dialog_save_file(const char *filter, const char *def_ext,
                             const char *suggested);

namespace {

ImU32 level_color(LogLevel l) {
  switch (l) {
    case LogLevel::Error: return theme::error();
    case LogLevel::Warn:  return IM_COL32(0xd9, 0x8c, 0x33, 0xff);
    case LogLevel::Info:  return theme::text();
    default:              return theme::text_dim();
  }
}
const char *level_tag(LogLevel l) {
  switch (l) {
    case LogLevel::Error: return "ERROR";
    case LogLevel::Warn:  return "WARN";
    case LogLevel::Info:  return "info";
    default:              return "trace";
  }
}

} // namespace

void draw_console(App &a) {
  if (!a.show_console) return;
  log_pump(); // pick up anything stderr produced since the last frame

  size_t errs = log_error_count(), warns = log_warn_count();
  // The title carries the counts, so a collapsed console still says whether
  // anything went wrong.
  char title[96];
  std::snprintf(title, sizeof title, "Console%s%s###console",
                errs ? "  " : "", errs ? "(!)" : "");
  panel_float_prepare(a, "console");
  if (!ImGui::Begin(title, &a.show_console)) {
    ImGui::End();
    return;
  }
  panel_float_controls(a, "console");

  static int min_level = 0;   // 0 trace .. 3 error
  static char filter[64] = "";
  static bool autoscroll = true;

  ImGui::SetNextItemWidth(110);
  ImGui::Combo("##lvl", &min_level, "All\0Info+\0Warnings+\0Errors\0");
  ImGui::SameLine();
  ImGui::SetNextItemWidth(200);
  ImGui::InputTextWithHint("##f", "filter...", filter, sizeof filter);
  ImGui::SameLine();
  if (ImGui::Button("Copy all")) ImGui::SetClipboardText(log_as_text().c_str());
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("The whole log to the clipboard.\n"
                      "Individual lines can also be selected and copied below.");
  ImGui::SameLine();
  if (ImGui::Button("Copy errors"))
    ImGui::SetClipboardText(log_as_text(true).c_str());
  ImGui::SameLine();
  if (ImGui::Button("Save...")) {
    std::string p = dialog_save_file(
        "Log files (*.log)\0*.log\0All files\0*.*\0", "log",
        "terraforge.log");
    if (!p.empty() && !log_write_file(p))
      log_error("io", "could not write log to " + p);
  }
  ImGui::SameLine();
  if (ImGui::Button("Clear")) log_clear();
  ImGui::SameLine();
  Checkbox("Follow", &autoscroll);
  ImGui::SameLine();
  ImGui::TextDisabled("%zu errors, %zu warnings", errs, warns);

  ImGui::Separator();

  ImGui::BeginChild("##log", ImVec2(0, 0), false,
                    ImGuiWindowFlags_HorizontalScrollbar);
  std::vector<LogEntry> lines = log_snapshot();
  std::string lf = filter;
  for (auto &c : lf) c = (char)tolower(c);

  for (const LogEntry &e : lines) {
    if ((int)e.level < min_level) continue;
    if (!lf.empty()) {
      std::string hay = e.category + " " + e.text;
      for (auto &c : hay) c = (char)tolower(c);
      if (hay.find(lf) == std::string::npos) continue;
    }
    char line[2400];
    std::snprintf(line, sizeof line, "[%8.3f] %-5s %s%s%s%s", e.time,
                  level_tag(e.level), e.category.empty() ? "" : "[",
                  e.category.c_str(), e.category.empty() ? "" : "] ",
                  e.text.c_str());
    std::string text = line;
    if (e.repeat > 1) text += "  (x" + std::to_string(e.repeat) + ")";

    ImGui::PushStyleColor(ImGuiCol_Text, level_color(e.level));
    // Selectable rather than Text: a line can be clicked, and Ctrl+C or the
    // context menu copies exactly that line.
    ImGui::Selectable(text.c_str());
    ImGui::PopStyleColor();
    if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
      ImGui::SetClipboardText(text.c_str());
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("right-click to copy this line");
  }
  if (autoscroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 1.f)
    ImGui::SetScrollHereY(1.f);
  ImGui::EndChild();
  ImGui::End();
}

} // namespace studio
