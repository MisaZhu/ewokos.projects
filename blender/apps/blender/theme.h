// Blender-consistent dark theme for Dear ImGui.
//
// Colour values mirror the default Blender "Blender Dark" theme
// (source/blender/editors/interface/resources.cc, ui_theme_init_dark).
#ifndef BLENDER_THEME_H
#define BLENDER_THEME_H

struct ImGuiStyle;
struct ImVec4;

namespace blender {
namespace theme {

// Blender palette constants used both by the imgui style and by the
// viewport clear / gizmo colours.  8-bit per channel, matching Blender's
// own sRGB byte values.
struct Palette {
    unsigned char back[3];        // window/panel backdrop         #252525
    unsigned char back_hi[3];     // hovered panel backdrop        #2e2e2e
    unsigned char title[3];       // editor title bar              #1f1f1f
    unsigned char header[3];      // panel header                  #393939
    unsigned char header_hi[3];   // hovered panel header          #454545
    unsigned char button[3];      // button face                   #3d3d3d
    unsigned char button_hi[3];   // button hovered                #4a4a4a
    unsigned char button_sel[3];  // button pressed / selected     #4772b3
    unsigned char text[3];        // primary text                  #e6e6e6
    unsigned char text_hi[3];     // hovered/active text           #ffffff
    unsigned char text_dim[3];    // de-emphasised text            #a5a5a5
    unsigned char border[3];      // panel borders                 #1a1a1a
    unsigned char grid[3];        // viewport minor grid           #393939
    unsigned char grid_major[3];  // viewport major grid / axes    #4b4b4b
    unsigned char viewport[3];    // viewport backdrop             #393939
    unsigned char axis_x[3];      // +X red                        #dc3232
    unsigned char axis_y[3];      // +Y green                      #8ae234
    unsigned char axis_z[3];      // +Z blue                       #4772b3
    unsigned char select[3];      // selection outline             #f9c258
    unsigned char cursor3d[3];    // 3D cursor accent              #ff3355
    unsigned char wire[3];        // wireframe / object outline    #000000
};

extern const Palette palette;

// Populate an ImGuiStyle with the Blender Dark colours + metrics.
// Called once from init_imgui().
void apply(ImGuiStyle& style);

} // namespace theme
} // namespace blender

#endif // BLENDER_THEME_H
