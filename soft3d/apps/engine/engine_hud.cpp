/*
 * engine_hud.cpp -- Dear ImGui overlay for the Inline-4 engine demo.
 *
 * Same read-only stats HUD as glmatrix/matrix/Fireworks: a small NoInputs
 * window in the top-left corner reporting FPS, the live renderer route and
 * the crank/cylinder state that the old glcommon bitmap hud used to print.
 * engine.c is C, so the ImGui C++ code lives in this wrapper TU and exposes
 * a flat extern "C" API.
 *
 * NOTE: the ImGui backend binds its own GL program in RenderDrawData and
 * does NOT restore GL_CURRENT_PROGRAM -- the caller must glUseProgram() its
 * own program every frame before drawing the scene (see render_frame).
 */
#include <stdio.h>

#include "imgui/imgui.h"
#include "imgui/backends/imgui_impl_gles2.h"

extern "C" void engine_hud_init(int w, int h);
extern "C" void engine_hud_frame(int w, int h, int fps, int gpu,
                                 int crank_deg, const char *stroke);
extern "C" void engine_hud_shutdown(void);

void engine_hud_init(int w, int h)
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = NULL;                                 /* no imgui.ini    */
    io.DisplaySize = ImVec2((float)w, (float)h);
    ImGui::StyleColorsDark();
    ImGui_ImplGLES2_Init();
    io.Fonts->AddFontFromFileTTF("/usr/system/fonts/system.ttf", 16.0f);
}

/* Build, render and emit one ImGui frame.  Display-only, so no input is
 * forwarded and the window is marked NoInputs. */
void engine_hud_frame(int w, int h, int fps, int gpu,
                      int crank_deg, const char *stroke)
{
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2((float)w, (float)h);

    ImGui_ImplGLES2_NewFrame();
    ImGui::NewFrame();

    ImGui::SetNextWindowPos(ImVec2(8.0f, 8.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowBgAlpha(0.55f);
    ImGui::Begin("Inline-4  1-3-4-2", NULL,
                 ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize
                 | ImGuiWindowFlags_NoInputs);
    ImGui::Text("FPS: %d", fps);
    ImGui::TextColored(gpu ? ImVec4(0.40f, 1.00f, 0.55f, 1.0f)
                           : ImVec4(1.00f, 0.70f, 0.30f, 1.0f),
                       "Renderer: %s", gpu ? "GPU" : "CPU");
    ImGui::Text("Crank: %3d  Cyl1: %s", crank_deg, stroke);
    ImGui::End();

    ImGui::Render();
    ImGui_ImplGLES2_RenderDrawData(ImGui::GetDrawData());
}

void engine_hud_shutdown(void)
{
    ImGui_ImplGLES2_Shutdown();
    ImGui::DestroyContext();
}
