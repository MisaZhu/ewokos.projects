// dear imgui: Renderer Backend for the EwokOS GLES2 front end (libs/gles)
//
// Rewritten from the PortableGL backend onto the standard GLES2 + GLSL ES 1.00
// front end exposed by libGLESv2/libEGL.  The renderer keeps the classic
// OpenGL3 imgui design (a VBO + an element buffer, one textured/vertex-coloured
// shader program, per-command scissor + texture bind), with two concessions to
// this front end's deliberately narrow GLES2 surface:
//   - no VAOs: glGenVertexArrays/glBindVertexArray are not implemented, so the
//     vertex attribute pointers are (re)bound from the VBOs every frame;
//   - no glPolygonMode: fill is the only rasterisation mode anyway.
//
// The projection uniform is named uMVP so that on raspi5 (PGL_G3D) the engine
// recognises this program as a geometry-kernel candidate: pgl_g3d_shader.c keys
// the compiled V3D geometry route off a uMVP in the vertex shader.  ImGui's
// fragment stage multiplies a per-vertex colour by a texture sample
// (gl_FragColor = vColor * texture2D(uTexture, vUV)), which selects the
// colour-modulated textured kernel variant (cs_is_geom = 4: bilinear sampling,
// per-vertex colour modulation, SRC_ALPHA/ONE_MINUS_SRC_ALPHA blending and
// per-command scissor, no depth write).  That kernel reproduces this renderer's
// exact visual design on the V3D GPU; when it cannot (no GPU, unstaged font
// atlas, unsupported blend state) the draw declines to the CPU GLSL
// interpreter, which renders pixel-identical output.

// CHANGELOG
// (minor and older changes stripped away, please see git history for details)

#if defined(_MSC_VER) && !defined(_CRT_SECURE_NO_WARNINGS)
#define _CRT_SECURE_NO_WARNINGS
#endif

#include "imgui.h"
#include "imgui_impl_gles2.h"
#include <stdio.h>
#if defined(_MSC_VER) && _MSC_VER <= 1500 // MSVC 2008 or earlier
#include <stddef.h>     // intptr_t
#else
#include <stdint.h>     // intptr_t
#endif

#include <GLES2/gl2.h>

// [Debugging]
//#define IMGUI_IMPL_OPENGL_DEBUG
#ifdef IMGUI_IMPL_OPENGL_DEBUG
#define GL_CALL(_CALL)      do { _CALL; GLenum gl_err = glGetError(); if (gl_err != 0) fprintf(stderr, "GL error 0x%x returned from '%s'.\n", gl_err, #_CALL); } while (0)  // Call with error check
#else
#define GL_CALL(_CALL)      _CALL   // Call without error check
#endif

// GLES2 front end data
struct ImGui_ImplGLES2_Data
{
    GLuint          FontTexture;
    GLuint          ShaderHandle;

    GLint           AttribLocationProjMtx;   // Uniform locations
    GLint           AttribLocationTex;
    GLuint          AttribLocationVtxPos;    // Vertex attributes location
    GLuint          AttribLocationVtxUV;
    GLuint          AttribLocationVtxColor;
    GLuint          VboHandle, ElementsHandle;
    GLsizeiptr      VertexBufferSize;
    GLsizeiptr      IndexBufferSize;

    ImGui_ImplGLES2_Data() { memset((void*)this, 0, sizeof(*this)); }
};

// Backend data stored in io.BackendRendererUserData to allow support for multiple Dear ImGui contexts
// It is STRONGLY preferred that you use docking branch with multi-viewports (== single Dear ImGui context + multiple windows) instead of multiple Dear ImGui contexts.
static ImGui_ImplGLES2_Data* ImGui_ImplGLES2_GetBackendData()
{
    return ImGui::GetCurrentContext() ? (ImGui_ImplGLES2_Data*)ImGui::GetIO().BackendRendererUserData : NULL;
}

// Functions
bool    ImGui_ImplGLES2_Init()
{
    ImGuiIO& io = ImGui::GetIO();
    IM_ASSERT(io.BackendRendererUserData == NULL && "Already initialized a renderer backend!");

    // Setup backend capabilities flags
    ImGui_ImplGLES2_Data* bd = IM_NEW(ImGui_ImplGLES2_Data)();
    io.BackendRendererUserData = (void*)bd;
    io.BackendRendererName = "imgui_impl_gles2";
    io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;  // We can honor the ImDrawCmd::VtxOffset field, allowing for large meshes.

    return true;
}

void    ImGui_ImplGLES2_Shutdown()
{
    ImGui_ImplGLES2_Data* bd = ImGui_ImplGLES2_GetBackendData();
    IM_ASSERT(bd != NULL && "No renderer backend to shutdown, or already shutdown?");
    ImGuiIO& io = ImGui::GetIO();

    ImGui_ImplGLES2_DestroyDeviceObjects();
    io.BackendRendererName = NULL;
    io.BackendRendererUserData = NULL;
    io.BackendFlags &= ~ImGuiBackendFlags_RendererHasVtxOffset;
    IM_DELETE(bd);
}

void    ImGui_ImplGLES2_NewFrame()
{
    ImGui_ImplGLES2_Data* bd = ImGui_ImplGLES2_GetBackendData();
    IM_ASSERT(bd != NULL && "Did you call ImGui_ImplGLES2_Init()?");

    if (!bd->ShaderHandle)
        ImGui_ImplGLES2_CreateDeviceObjects();
}

static void ImGui_ImplGLES2_SetupRenderState(ImDrawData* draw_data, int fb_width, int fb_height)
{
    ImGui_ImplGLES2_Data* bd = ImGui_ImplGLES2_GetBackendData();

    // Setup render state: alpha-blending enabled, no face culling, no depth testing, scissor enabled, polygon fill
    glEnable(GL_BLEND);
    glBlendEquation(GL_FUNC_ADD);
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_CULL_FACE);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_STENCIL_TEST);
    glEnable(GL_SCISSOR_TEST);

    // Setup viewport, orthographic projection matrix
    // Our visible imgui space lies from draw_data->DisplayPos (top left) to draw_data->DisplayPos+data_data->DisplaySize (bottom right). DisplayPos is (0,0) for single viewport apps.
    GL_CALL(glViewport(0, 0, (GLsizei)fb_width, (GLsizei)fb_height));
    float L = draw_data->DisplayPos.x;
    float R = draw_data->DisplayPos.x + draw_data->DisplaySize.x;
    float T = draw_data->DisplayPos.y;
    float B = draw_data->DisplayPos.y + draw_data->DisplaySize.y;
    const float ortho_projection[4][4] =
    {
        { 2.0f/(R-L),   0.0f,         0.0f,   0.0f },
        { 0.0f,         2.0f/(T-B),   0.0f,   0.0f },
        { 0.0f,         0.0f,        -1.0f,   0.0f },
        { (R+L)/(L-R),  (T+B)/(B-T),  0.0f,   1.0f },
    };
    glUseProgram(bd->ShaderHandle);
    // The font/white-pixel texture is sampled from unit 0 (glUniform1i below),
    // but RenderDrawData binds it with a bare glBindTexture, which targets the
    // CURRENTLY ACTIVE unit.  An app that ends its scene with a non-zero active
    // unit (e.g. a multi-sampler shadertoy leaving GL_TEXTURE1 bound) would send
    // the atlas to that unit and leave unit 0 pointing at the scene texture, so
    // every glyph samples scene texels (mosaic).  Pin the active unit to 0 here
    // so the bind and the sampler agree; apps re-bind their own textures each
    // frame, so leaving unit 0 active afterwards is the documented contract.
    glActiveTexture(GL_TEXTURE0);
    glUniform1i(bd->AttribLocationTex, 0);
    glUniformMatrix4fv(bd->AttribLocationProjMtx, 1, GL_FALSE, &ortho_projection[0][0]);

    // Bind vertex/index buffers and setup attributes for ImDrawVert.
    // This front end has no VAOs, so the attribute pointers are bound here from
    // the currently bound VBO/element buffer every time the render state is set.
    GL_CALL(glBindBuffer(GL_ARRAY_BUFFER, bd->VboHandle));
    GL_CALL(glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, bd->ElementsHandle));
    GL_CALL(glEnableVertexAttribArray(bd->AttribLocationVtxPos));
    GL_CALL(glEnableVertexAttribArray(bd->AttribLocationVtxUV));
    GL_CALL(glEnableVertexAttribArray(bd->AttribLocationVtxColor));
    GL_CALL(glVertexAttribPointer(bd->AttribLocationVtxPos,   2, GL_FLOAT,         GL_FALSE, sizeof(ImDrawVert), (GLvoid*)IM_OFFSETOF(ImDrawVert, pos)));
    GL_CALL(glVertexAttribPointer(bd->AttribLocationVtxUV,    2, GL_FLOAT,         GL_FALSE, sizeof(ImDrawVert), (GLvoid*)IM_OFFSETOF(ImDrawVert, uv)));
    GL_CALL(glVertexAttribPointer(bd->AttribLocationVtxColor, 4, GL_UNSIGNED_BYTE, GL_TRUE,  sizeof(ImDrawVert), (GLvoid*)IM_OFFSETOF(ImDrawVert, col)));
}

// GLES2 Render function.
// Note that this implementation is little overcomplicated because we are saving/setting up/restoring every OpenGL state explicitly.
// This is in order to be able to run within an OpenGL engine that doesn't do so.
void    ImGui_ImplGLES2_RenderDrawData(ImDrawData* draw_data)
{
    // Avoid rendering when minimized, scale coordinates for retina displays (screen coordinates != framebuffer coordinates)
    int fb_width = (int)(draw_data->DisplaySize.x * draw_data->FramebufferScale.x);
    int fb_height = (int)(draw_data->DisplaySize.y * draw_data->FramebufferScale.y);
    if (fb_width <= 0 || fb_height <= 0)
        return;

    ImGui_ImplGLES2_Data* bd = ImGui_ImplGLES2_GetBackendData();

    // Backup GL state that this front end can actually query back (only the
    // viewport, scissor box, array-buffer binding and the enable caps are
    // meaningful here; the rest of the render state is re-established by
    // SetupRenderState every frame and by the caller after RenderDrawData).
    GLint last_viewport[4]; glGetIntegerv(GL_VIEWPORT, last_viewport);
    GLint last_scissor_box[4]; glGetIntegerv(GL_SCISSOR_BOX, last_scissor_box);
    GLint last_array_buffer; glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &last_array_buffer);
    // The active texture unit too: SetupRenderState pins it to 0 so the font
    // atlas bind lands on the sampler's unit, and we hand the app's unit back
    // on restore so a multi-sampler scene is not left on unit 0.
    GLint last_active_texture; glGetIntegerv(GL_ACTIVE_TEXTURE, &last_active_texture);

    GLboolean last_enable_blend = glIsEnabled(GL_BLEND);
    GLboolean last_enable_cull_face = glIsEnabled(GL_CULL_FACE);
    GLboolean last_enable_depth_test = glIsEnabled(GL_DEPTH_TEST);
    GLboolean last_enable_stencil_test = glIsEnabled(GL_STENCIL_TEST);
    GLboolean last_enable_scissor_test = glIsEnabled(GL_SCISSOR_TEST);

    // Setup desired GL state
    ImGui_ImplGLES2_SetupRenderState(draw_data, fb_width, fb_height);

    // Will project scissor/clipping rectangles into framebuffer space
    ImVec2 clip_off = draw_data->DisplayPos;         // (0,0) unless using multi-viewports
    ImVec2 clip_scale = draw_data->FramebufferScale; // (1,1) unless using retina display which are often (2,2)

    // Render command lists
    for (int n = 0; n < draw_data->CmdListsCount; n++)
    {
        const ImDrawList* cmd_list = draw_data->CmdLists[n];

        // Upload vertex/index buffers.  Grow the orphaned buffer only when the
        // incoming data no longer fits, then stream the whole list in with
        // glBufferSubData (the front end keeps one GL_STREAM_DRAW allocation).
        const GLsizeiptr vtx_buffer_size = (GLsizeiptr)cmd_list->VtxBuffer.Size * (int)sizeof(ImDrawVert);
        const GLsizeiptr idx_buffer_size = (GLsizeiptr)cmd_list->IdxBuffer.Size * (int)sizeof(ImDrawIdx);
        if (bd->VertexBufferSize < vtx_buffer_size)
        {
            bd->VertexBufferSize = vtx_buffer_size;
            GL_CALL(glBufferData(GL_ARRAY_BUFFER, bd->VertexBufferSize, NULL, GL_STREAM_DRAW));
        }
        if (bd->IndexBufferSize < idx_buffer_size)
        {
            bd->IndexBufferSize = idx_buffer_size;
            GL_CALL(glBufferData(GL_ELEMENT_ARRAY_BUFFER, bd->IndexBufferSize, NULL, GL_STREAM_DRAW));
        }
        GL_CALL(glBufferSubData(GL_ARRAY_BUFFER, 0, vtx_buffer_size, (const GLvoid*)cmd_list->VtxBuffer.Data));
        GL_CALL(glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, 0, idx_buffer_size, (const GLvoid*)cmd_list->IdxBuffer.Data));

        for (int cmd_i = 0; cmd_i < cmd_list->CmdBuffer.Size; cmd_i++)
        {
            const ImDrawCmd* pcmd = &cmd_list->CmdBuffer[cmd_i];
            if (pcmd->UserCallback != NULL)
            {
                // User callback, registered via ImDrawList::AddCallback()
                // (ImDrawCallback_ResetRenderState is a special callback value used by the user to request the renderer to reset render state.)
                if (pcmd->UserCallback == ImDrawCallback_ResetRenderState)
                    ImGui_ImplGLES2_SetupRenderState(draw_data, fb_width, fb_height);
                else
                    pcmd->UserCallback(cmd_list, pcmd);
            }
            else
            {
                // Project scissor/clipping rectangles into framebuffer space
                ImVec2 clip_min((pcmd->ClipRect.x - clip_off.x) * clip_scale.x, (pcmd->ClipRect.y - clip_off.y) * clip_scale.y);
                ImVec2 clip_max((pcmd->ClipRect.z - clip_off.x) * clip_scale.x, (pcmd->ClipRect.w - clip_off.y) * clip_scale.y);
                if (clip_max.x <= clip_min.x || clip_max.y <= clip_min.y)
                    continue;

                // Apply scissor/clipping rectangle (Y is inverted in OpenGL)
                GL_CALL(glScissor((int)clip_min.x, (int)((float)fb_height - clip_max.y), (int)(clip_max.x - clip_min.x), (int)(clip_max.y - clip_min.y)));

                // Bind texture, Draw
                glBindTexture(GL_TEXTURE_2D, (GLuint)(intptr_t)pcmd->GetTexID());
                GL_CALL(glDrawElements(GL_TRIANGLES, (GLsizei)pcmd->ElemCount, sizeof(ImDrawIdx) == 2 ? GL_UNSIGNED_SHORT : GL_UNSIGNED_INT, (void*)(intptr_t)(pcmd->IdxOffset * sizeof(ImDrawIdx))));
            }
        }
    }

    // Restore modified GL state
    glDisableVertexAttribArray(bd->AttribLocationVtxPos);
    glDisableVertexAttribArray(bd->AttribLocationVtxUV);
    glDisableVertexAttribArray(bd->AttribLocationVtxColor);
    glBindBuffer(GL_ARRAY_BUFFER, (GLuint)last_array_buffer);

    if (last_enable_blend) glEnable(GL_BLEND); else glDisable(GL_BLEND);
    if (last_enable_cull_face) glEnable(GL_CULL_FACE); else glDisable(GL_CULL_FACE);
    if (last_enable_depth_test) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
    if (last_enable_stencil_test) glEnable(GL_STENCIL_TEST); else glDisable(GL_STENCIL_TEST);
    if (last_enable_scissor_test) glEnable(GL_SCISSOR_TEST); else glDisable(GL_SCISSOR_TEST);

    glViewport(last_viewport[0], last_viewport[1], (GLsizei)last_viewport[2], (GLsizei)last_viewport[3]);
    glScissor(last_scissor_box[0], last_scissor_box[1], (GLsizei)last_scissor_box[2], (GLsizei)last_scissor_box[3]);
    glActiveTexture((GLenum)last_active_texture);
}

bool ImGui_ImplGLES2_CreateFontsTexture()
{
    ImGuiIO& io = ImGui::GetIO();
    ImGui_ImplGLES2_Data* bd = ImGui_ImplGLES2_GetBackendData();

    // Build texture atlas
    unsigned char* pixels;
    int width, height;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);   // Load as RGBA 32-bit (75% of the memory is wasted, but default font is so small) because it is more likely to be compatible with user's existing shaders. If your ImTextureId represent a higher-level concept than just a GL texture id, consider calling GetTexDataAsAlpha8() instead to save on GPU memory.

    // Upload texture to graphics system
    // (Bilinear sampling is required by default. Set 'io.Fonts->Flags |= ImFontAtlasFlags_NoBakedLines' or 'style.AntiAliasedLinesUseTex = false' to allow point/nearest sampling)
    GLint last_texture;
    GL_CALL(glGetIntegerv(GL_TEXTURE_BINDING_2D, &last_texture));
    GL_CALL(glGenTextures(1, &bd->FontTexture));
    GL_CALL(glBindTexture(GL_TEXTURE_2D, bd->FontTexture));
    GL_CALL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR));
    GL_CALL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR));
    GL_CALL(glPixelStorei(GL_UNPACK_ALIGNMENT, 4));
    GL_CALL(glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels));

    // Store our identifier
    io.Fonts->SetTexID((ImTextureID)(intptr_t)bd->FontTexture);

    // Restore state
    glBindTexture(GL_TEXTURE_2D, (GLuint)last_texture);

    return true;
}

void ImGui_ImplGLES2_DestroyFontsTexture()
{
    ImGuiIO& io = ImGui::GetIO();
    ImGui_ImplGLES2_Data* bd = ImGui_ImplGLES2_GetBackendData();
    if (bd->FontTexture)
    {
        glDeleteTextures(1, &bd->FontTexture);
        io.Fonts->SetTexID(0);
        bd->FontTexture = 0;
    }
}

static GLuint ImGui_ImplGLES2_CompileShader(GLenum type, const char* src)
{
    GLuint sh = glCreateShader(type);
    GLint ok = 0;
    if (sh == 0)
        return 0;
    glShaderSource(sh, 1, &src, NULL);
    glCompileShader(sh);
    glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok)
    {
        char log[512];
        glGetShaderInfoLog(sh, sizeof(log), NULL, log);
        fprintf(stderr, "imgui_impl_gles2: shader compile failed: %s\n", log);
        glDeleteShader(sh);
        return 0;
    }
    return sh;
}

bool    ImGui_ImplGLES2_CreateDeviceObjects()
{
    ImGui_ImplGLES2_Data* bd = ImGui_ImplGLES2_GetBackendData();

    // GLSL ES 1.00 pair matching the classic imgui OpenGL3 renderer: a
    // position/uv/colour vertex stream projected by uMVP, and a fragment
    // stage multiplying the vertex colour by the font atlas sample.  Attribute
    // locations are bound explicitly (0=pos, 1=uv, 2=colour) so the render-state
    // setup can address them without a query round-trip.
    const char* vertex_shader =
        "attribute vec2 aPosition;\n"
        "attribute vec2 aUV;\n"
        "attribute vec4 aColor;\n"
        "uniform mat4 uMVP;\n"
        "varying vec2 vUV;\n"
        "varying vec4 vColor;\n"
        "void main()\n"
        "{\n"
        "    vUV = aUV;\n"
        "    vColor = aColor;\n"
        "    gl_Position = uMVP * vec4(aPosition, 0.0, 1.0);\n"
        "}\n";

    const char* fragment_shader =
        "precision mediump float;\n"
        "varying vec2 vUV;\n"
        "varying vec4 vColor;\n"
        "uniform sampler2D uTexture;\n"
        "void main()\n"
        "{\n"
        "    gl_FragColor = vColor * texture2D(uTexture, vUV);\n"
        "}\n";

    GLuint vs = ImGui_ImplGLES2_CompileShader(GL_VERTEX_SHADER, vertex_shader);
    GLuint fs = ImGui_ImplGLES2_CompileShader(GL_FRAGMENT_SHADER, fragment_shader);
    bd->ShaderHandle = glCreateProgram();
    glAttachShader(bd->ShaderHandle, vs);
    glAttachShader(bd->ShaderHandle, fs);
    glBindAttribLocation(bd->ShaderHandle, 0, "aPosition");
    glBindAttribLocation(bd->ShaderHandle, 1, "aUV");
    glBindAttribLocation(bd->ShaderHandle, 2, "aColor");
    glLinkProgram(bd->ShaderHandle);
    GLint ok = 0;
    glGetProgramiv(bd->ShaderHandle, GL_LINK_STATUS, &ok);
    if (!ok)
    {
        char log[512];
        glGetProgramInfoLog(bd->ShaderHandle, sizeof(log), NULL, log);
        fprintf(stderr, "imgui_impl_gles2: program link failed: %s\n", log);
    }
    glDeleteShader(vs);
    glDeleteShader(fs);

    bd->AttribLocationProjMtx = glGetUniformLocation(bd->ShaderHandle, "uMVP");
    bd->AttribLocationTex = glGetUniformLocation(bd->ShaderHandle, "uTexture");
    bd->AttribLocationVtxPos = 0;
    bd->AttribLocationVtxUV = 1;
    bd->AttribLocationVtxColor = 2;

    // Create buffers
    glGenBuffers(1, &bd->VboHandle);
    glGenBuffers(1, &bd->ElementsHandle);

    ImGui_ImplGLES2_CreateFontsTexture();

    return true;
}

void    ImGui_ImplGLES2_DestroyDeviceObjects()
{
    ImGui_ImplGLES2_Data* bd = ImGui_ImplGLES2_GetBackendData();
    if (bd->VboHandle)      { glDeleteBuffers(1, &bd->VboHandle); bd->VboHandle = 0; }
    if (bd->ElementsHandle) { glDeleteBuffers(1, &bd->ElementsHandle); bd->ElementsHandle = 0; }
    if (bd->ShaderHandle)   { glDeleteProgram(bd->ShaderHandle); bd->ShaderHandle = 0; }
    ImGui_ImplGLES2_DestroyFontsTexture();
}
