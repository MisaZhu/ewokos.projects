// Blender Dark palette + ImGui style application.
#include "theme.h"

#include "imgui/imgui.h"

namespace blender {
namespace theme {

// Blender's "Blender Dark" theme constants, taken from
// source/blender/editors/interface/resources.cc::ui_theme_init_dark().
const Palette palette = {
    /* back        */ { 0x25, 0x25, 0x25 },
    /* back_hi     */ { 0x2e, 0x2e, 0x2e },
    /* title       */ { 0x1f, 0x1f, 0x1f },
    /* header      */ { 0x39, 0x39, 0x39 },
    /* header_hi   */ { 0x45, 0x45, 0x45 },
    /* button      */ { 0x3d, 0x3d, 0x3d },
    /* button_hi   */ { 0x4a, 0x4a, 0x4a },
    /* button_sel  */ { 0x47, 0x72, 0xb3 },
    /* text        */ { 0xe6, 0xe6, 0xe6 },
    /* text_hi     */ { 0xff, 0xff, 0xff },
    /* text_dim    */ { 0xa5, 0xa5, 0xa5 },
    /* border      */ { 0x1a, 0x1a, 0x1a },
    /* grid        */ { 0x39, 0x39, 0x39 },
    /* grid_major  */ { 0x4b, 0x4b, 0x4b },
    /* viewport    */ { 0x39, 0x39, 0x39 },
    /* axis_x      */ { 0xdc, 0x32, 0x32 },
    /* axis_y      */ { 0x8a, 0xe2, 0x34 },
    /* axis_z      */ { 0x47, 0x72, 0xb3 },
    /* select      */ { 0xf9, 0xc2, 0x58 },
    /* cursor3d    */ { 0xff, 0x33, 0x55 },
    /* wire        */ { 0x00, 0x00, 0x00 },
};

namespace {

inline ImVec4 rgb(const unsigned char c[3], float a = 1.0f)
{
    return ImVec4(c[0] / 255.0f, c[1] / 255.0f, c[2] / 255.0f, a);
}

} // namespace

void apply(ImGuiStyle& s)
{
    // Blender's UI is compact: small radii, thin borders, tight padding.
    s.Alpha                  = 1.0f;
    s.DisabledAlpha          = 0.5f;
    s.WindowPadding          = ImVec2(6, 4);
    s.WindowRounding         = 0.0f;   // Blender panels are square
    s.WindowBorderSize       = 1.0f;
    s.WindowMinSize          = ImVec2(32, 20);
    s.WindowTitleAlign       = ImVec2(0.5f, 0.5f);
    s.ChildRounding          = 0.0f;
    s.ChildBorderSize        = 1.0f;
    s.PopupRounding          = 0.0f;
    s.PopupBorderSize        = 1.0f;
    s.FramePadding           = ImVec2(5, 3);
    s.FrameRounding          = 2.0f;   // Blender buttons: subtle rounding
    s.FrameBorderSize        = 0.0f;
    s.ItemSpacing            = ImVec2(6, 4);
    s.ItemInnerSpacing       = ImVec2(4, 4);
    s.IndentSpacing          = 14.0f;
    s.CellPadding            = ImVec2(4, 2);
    s.ScrollbarSize          = 12.0f;
    s.ScrollbarRounding      = 2.0f;
    s.GrabMinSize            = 8.0f;
    s.GrabRounding           = 2.0f;
    s.TabRounding            = 2.0f;
    s.TabBorderSize          = 0.0f;

    ImVec4* c = s.Colors;

    // Base surfaces
    c[ImGuiCol_Text]                  = rgb(palette.text);
    c[ImGuiCol_TextDisabled]          = rgb(palette.text_dim);
    c[ImGuiCol_WindowBg]              = rgb(palette.back);
    c[ImGuiCol_ChildBg]               = rgb(palette.back);
    c[ImGuiCol_PopupBg]               = rgb(palette.back);
    c[ImGuiCol_Border]                = rgb(palette.border);
    c[ImGuiCol_BorderShadow]          = ImVec4(0, 0, 0, 0);

    // Panels / headers (imgui "FrameBg" is what Blender calls a panel body).
    c[ImGuiCol_FrameBg]               = rgb(palette.header);
    c[ImGuiCol_FrameBgHovered]        = rgb(palette.header_hi);
    c[ImGuiCol_FrameBgActive]         = rgb(palette.button_sel, 0.7f);

    // Title bars map to Blender's editor header strip.
    c[ImGuiCol_TitleBg]               = rgb(palette.title);
    c[ImGuiCol_TitleBgActive]         = rgb(palette.title);
    c[ImGuiCol_TitleBgCollapsed]      = rgb(palette.title);

    // Menu bar strip (File/Edit/Render/...).
    c[ImGuiCol_MenuBarBg]             = rgb(palette.header);

    // Scrollbars / sliders
    c[ImGuiCol_ScrollbarBg]           = rgb(palette.back);
    c[ImGuiCol_ScrollbarGrab]         = rgb(palette.button);
    c[ImGuiCol_ScrollbarGrabHovered]  = rgb(palette.button_hi);
    c[ImGuiCol_ScrollbarGrabActive]   = rgb(palette.button_sel);

    // Check / radio markers
    c[ImGuiCol_CheckMark]             = rgb(palette.button_sel);

    // Sliders & progress
    c[ImGuiCol_SliderGrab]            = rgb(palette.button_sel);
    c[ImGuiCol_SliderGrabActive]      = rgb(palette.button_sel);
    c[ImGuiCol_Button]                = rgb(palette.button);
    c[ImGuiCol_ButtonHovered]         = rgb(palette.button_hi);
    c[ImGuiCol_ButtonActive]          = rgb(palette.button_sel);

    // Headers (Outliner rows, CollapsingHeader bars)
    c[ImGuiCol_Header]                = rgb(palette.header);
    c[ImGuiCol_HeaderHovered]         = rgb(palette.header_hi);
    c[ImGuiCol_HeaderActive]          = rgb(palette.button_sel);

    // Selection rectangle
    c[ImGuiCol_Separator]             = rgb(palette.border);
    c[ImGuiCol_SeparatorHovered]      = rgb(palette.button_sel);
    c[ImGuiCol_SeparatorActive]       = rgb(palette.button_sel);

    // Resize grips (window splitters)
    c[ImGuiCol_ResizeGrip]            = rgb(palette.button, 0.35f);
    c[ImGuiCol_ResizeGripHovered]     = rgb(palette.button_sel, 0.75f);
    c[ImGuiCol_ResizeGripActive]      = rgb(palette.button_sel);

    // Tabs (workspace tabs at the top)
    c[ImGuiCol_Tab]                   = rgb(palette.button);
    c[ImGuiCol_TabHovered]            = rgb(palette.button_hi);
    c[ImGuiCol_TabActive]             = rgb(palette.button_sel);
    c[ImGuiCol_TabUnfocused]          = rgb(palette.button);
    c[ImGuiCol_TabUnfocusedActive]    = rgb(palette.button_sel, 0.6f);

    // Plot lines (timeline)
    c[ImGuiCol_PlotLines]             = rgb(palette.text_dim);
    c[ImGuiCol_PlotLinesHovered]      = rgb(palette.select);
    c[ImGuiCol_PlotHistogram]         = rgb(palette.button_sel);
    c[ImGuiCol_PlotHistogramHovered]  = rgb(palette.select);

    // Text selection
    c[ImGuiCol_TextSelectedBg]        = rgb(palette.button_sel, 0.55f);

    // Nav highlight
    c[ImGuiCol_NavHighlight]          = rgb(palette.button_sel);
    c[ImGuiCol_NavWindowingHighlight] = ImVec4(1, 1, 1, 0.7f);
    c[ImGuiCol_NavWindowingDimBg]     = ImVec4(0.8f, 0.8f, 0.8f, 0.2f);
    c[ImGuiCol_ModalWindowDimBg]      = ImVec4(0, 0, 0, 0.5f);
}

} // namespace theme
} // namespace blender
