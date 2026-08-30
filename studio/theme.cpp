// Geekatplay Studio — flat dark-gray theme, dim orange accent, zero rounding
#include "app.hpp"
#include <imgui.h>

namespace studio {

// palette
static ImVec4 hex(unsigned c, float a = 1.f) {
  return ImVec4(((c >> 16) & 255) / 255.f, ((c >> 8) & 255) / 255.f,
                (c & 255) / 255.f, a);
}

void apply_theme() {
  ImGuiStyle &s = ImGui::GetStyle();

  // geometry: strictly flat
  s.WindowRounding = 0;
  s.ChildRounding = 0;
  s.FrameRounding = 0;
  s.PopupRounding = 0;
  s.ScrollbarRounding = 0;
  s.GrabRounding = 0;
  s.TabRounding = 0;
  s.WindowBorderSize = 1;
  s.FrameBorderSize = 0;
  s.PopupBorderSize = 1;
  s.WindowPadding = ImVec2(12, 10);
  s.FramePadding = ImVec2(9, 5);
  s.ItemSpacing = ImVec2(9, 6);
  s.ItemInnerSpacing = ImVec2(6, 4);
  s.CellPadding = ImVec2(6, 4);
  s.IndentSpacing = 18;
  s.ScrollbarSize = 13;
  s.GrabMinSize = 10;
  s.TabBarBorderSize = 1;
  s.DockingSeparatorSize = 4;
  s.SeparatorTextBorderSize = 2;
  s.SeparatorTextPadding = ImVec2(18, 4);
  s.FrameBorderSize = 1; // subtle inset outline on controls (C4D-style depth)

  const ImVec4 bg_deep = hex(0x181818);
  const ImVec4 bg_panel = hex(0x232323);
  const ImVec4 bg_frame = hex(0x2e2e2e);
  const ImVec4 bg_hover = hex(0x3a3a3a);
  const ImVec4 bg_active = hex(0x454545);
  const ImVec4 border = hex(0x101010);
  const ImVec4 text = hex(0xdcdad5);
  const ImVec4 text_dim = hex(0x8a8781);
  const ImVec4 orange = hex(0xc87830);      // dim orange accent
  const ImVec4 orange_hi = hex(0xe08a3c);
  const ImVec4 orange_dk = hex(0x8a5322);

  ImVec4 *c = s.Colors;
  c[ImGuiCol_Text] = text;
  c[ImGuiCol_TextDisabled] = text_dim;
  c[ImGuiCol_WindowBg] = bg_panel;
  c[ImGuiCol_ChildBg] = bg_panel;
  c[ImGuiCol_PopupBg] = hex(0x1a1a1a);
  c[ImGuiCol_Border] = border;
  c[ImGuiCol_BorderShadow] = ImVec4(0, 0, 0, 0);
  c[ImGuiCol_FrameBg] = bg_frame;
  c[ImGuiCol_FrameBgHovered] = bg_hover;
  c[ImGuiCol_FrameBgActive] = bg_active;
  c[ImGuiCol_TitleBg] = bg_deep;
  c[ImGuiCol_TitleBgActive] = bg_deep;
  c[ImGuiCol_TitleBgCollapsed] = bg_deep;
  c[ImGuiCol_MenuBarBg] = bg_deep;
  c[ImGuiCol_ScrollbarBg] = bg_deep;
  c[ImGuiCol_ScrollbarGrab] = bg_frame;
  c[ImGuiCol_ScrollbarGrabHovered] = bg_hover;
  c[ImGuiCol_ScrollbarGrabActive] = orange_dk;
  c[ImGuiCol_CheckMark] = orange;
  c[ImGuiCol_SliderGrab] = orange_dk;
  c[ImGuiCol_SliderGrabActive] = orange;
  c[ImGuiCol_Button] = bg_frame;
  c[ImGuiCol_ButtonHovered] = bg_hover;
  c[ImGuiCol_ButtonActive] = orange_dk;
  c[ImGuiCol_Header] = bg_frame;
  c[ImGuiCol_HeaderHovered] = bg_hover;
  c[ImGuiCol_HeaderActive] = bg_active;
  c[ImGuiCol_Separator] = border;
  c[ImGuiCol_SeparatorHovered] = orange_dk;
  c[ImGuiCol_SeparatorActive] = orange;
  c[ImGuiCol_ResizeGrip] = bg_frame;
  c[ImGuiCol_ResizeGripHovered] = orange_dk;
  c[ImGuiCol_ResizeGripActive] = orange;
  c[ImGuiCol_Tab] = bg_deep;
  c[ImGuiCol_TabHovered] = bg_hover;
  c[ImGuiCol_TabSelected] = bg_panel;
  c[ImGuiCol_TabSelectedOverline] = orange;
  c[ImGuiCol_TabDimmed] = bg_deep;
  c[ImGuiCol_TabDimmedSelected] = bg_panel;
  c[ImGuiCol_TabDimmedSelectedOverline] = orange_dk;
  c[ImGuiCol_DockingPreview] = hex(0xc87830, 0.4f);
  c[ImGuiCol_DockingEmptyBg] = bg_deep;
  c[ImGuiCol_PlotLines] = orange;
  c[ImGuiCol_PlotLinesHovered] = orange_hi;
  c[ImGuiCol_PlotHistogram] = orange_dk;
  c[ImGuiCol_PlotHistogramHovered] = orange;
  c[ImGuiCol_TableHeaderBg] = bg_deep;
  c[ImGuiCol_TableBorderStrong] = border;
  c[ImGuiCol_TableBorderLight] = border;
  c[ImGuiCol_TableRowBg] = ImVec4(0, 0, 0, 0);
  c[ImGuiCol_TableRowBgAlt] = hex(0xffffff, 0.02f);
  c[ImGuiCol_TextSelectedBg] = hex(0xc87830, 0.35f);
  c[ImGuiCol_DragDropTarget] = orange;
  c[ImGuiCol_NavCursor] = orange;
  c[ImGuiCol_NavWindowingHighlight] = orange;
  c[ImGuiCol_NavWindowingDimBg] = hex(0x000000, 0.5f);
  c[ImGuiCol_ModalWindowDimBg] = hex(0x000000, 0.6f);
}

} // namespace studio
