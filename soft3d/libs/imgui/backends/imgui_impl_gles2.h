// dear imgui: Renderer Backend for the EwokOS GLES2 front end (libs/gles)
// This needs to be used along with a Platform Backend (the EwokOS xwin/EGL one)

#pragma once
#include "imgui.h"      // IMGUI_IMPL_API
#ifndef IMGUI_DISABLE

// Backend API
IMGUI_IMPL_API bool     ImGui_ImplGLES2_Init();
IMGUI_IMPL_API void     ImGui_ImplGLES2_Shutdown();
IMGUI_IMPL_API void     ImGui_ImplGLES2_NewFrame();
IMGUI_IMPL_API void     ImGui_ImplGLES2_RenderDrawData(ImDrawData* draw_data);

// (Optional) Called by Init/NewFrame/Shutdown
IMGUI_IMPL_API bool     ImGui_ImplGLES2_CreateFontsTexture();
IMGUI_IMPL_API void     ImGui_ImplGLES2_DestroyFontsTexture();
IMGUI_IMPL_API bool     ImGui_ImplGLES2_CreateDeviceObjects();
IMGUI_IMPL_API void     ImGui_ImplGLES2_DestroyDeviceObjects();

#endif // #ifndef IMGUI_DISABLE
