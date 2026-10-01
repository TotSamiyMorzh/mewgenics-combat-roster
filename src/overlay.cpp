// overlay.cpp -- Dear ImGui drawn on top of the game, once per frame.
//
// Mewgenics presents with SDL_GL_SwapWindow on a GL 3.2 core context. SDL3 is
// statically linked WITH its dynamic-API shim, so every SDL_* call goes through
// a jump table in .data; the only interception that actually fires is writing
// that table's slot (mgmp CLAUDE.md, "SDL3 is statically linked"). The slot is
// written lazily, after SDL_InitDynamicAPI has filled the table -- writing it
// earlier is undone by the init.
//
// Input: the win32 backend + a WndProc subclass (above SDL's own). Mouse button
// and wheel messages are swallowed only while the cursor is over our panel, so
// clicking the panel never clicks the board under it.
#include "overlay.h"

#include "game.h"
#include "log.h"
#include "assets.h"
#include "panel.h"
#include "roster.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <GL/gl.h>

#include <string>

#include "imgui.h"
#include "imgui_impl_opengl3.h"
#include "imgui_impl_win32.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

namespace cr {
namespace {

// SDL_GL_SwapWindow's DYNAPI slot (mgmp kRva_SdlSwapSlot, this build).
constexpr uint32_t kRva_SwapSlot = 0x012E7650;

using fn_swap = bool(__cdecl*)(void* window);

constexpr GLenum kGL_DRAW_FRAMEBUFFER         = 0x8CA9;
constexpr GLenum kGL_DRAW_FRAMEBUFFER_BINDING = 0x8CA6;
constexpr GLenum kGL_PIXEL_UNPACK_BUFFER      = 0x88EC;
constexpr GLenum kGL_PIXEL_UNPACK_BINDING     = 0x88EF;
using fn_glBindFramebuffer = void(APIENTRY*)(GLenum, GLuint);
using fn_glBindBuffer      = void(APIENTRY*)(GLenum, GLuint);

struct State {
    void**   slot       = nullptr;
    void*    slot_stub  = nullptr;   // the slot's value before SDL initialised
    fn_swap  prev_swap  = nullptr;
    bool     installed  = false;

    HWND     hwnd       = nullptr;
    WNDPROC  prev_proc  = nullptr;
    HGLRC    gl_ctx     = nullptr;
    bool     imgui_ok   = false;
    bool     gl_ok      = false;
    bool     failed     = false;

    fn_glBindFramebuffer bind_fb  = nullptr;
    fn_glBindBuffer      bind_buf = nullptr;

    bool     capture_mouse = false;  // swallow clicks this frame
    LARGE_INTEGER last_t{};
    ImGuiStyle base_style;
    float    ui_scale = 0.0f;

    Roster   roster;
    PanelState panel;
    std::string game_dir;
    bool     icons_ok = false;
} g;

// --- window / input --------------------------------------------------------------

BOOL CALLBACK find_game_window(HWND hwnd, LPARAM out) {
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid != GetCurrentProcessId() || !IsWindowVisible(hwnd)) return TRUE;
    if (GetWindow(hwnd, GW_OWNER)) return TRUE;
    *(HWND*)out = hwnd;
    return FALSE;
}

LRESULT CALLBACK wndproc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (g.imgui_ok) {
        ImGui_ImplWin32_WndProcHandler(hwnd, msg, wp, lp);
        if (g.capture_mouse) {
            switch (msg) {
            case WM_LBUTTONDOWN: case WM_LBUTTONUP: case WM_LBUTTONDBLCLK:
            case WM_RBUTTONDOWN: case WM_RBUTTONUP: case WM_RBUTTONDBLCLK:
            case WM_MBUTTONDOWN: case WM_MBUTTONUP: case WM_MBUTTONDBLCLK:
            case WM_XBUTTONDOWN: case WM_XBUTTONUP: case WM_XBUTTONDBLCLK:
            case WM_MOUSEWHEEL:  case WM_MOUSEHWHEEL:
                return 0;
            default: break;
            }
        }
    }
    return CallWindowProcW(g.prev_proc, hwnd, msg, wp, lp);
}

// --- ImGui lifetime --------------------------------------------------------------

void load_fonts() {
    ImGuiIO& io = ImGui::GetIO();
    // Segoe UI covers Latin + Cyrillic; ImGui 1.92 loads glyphs on demand.
    char path[MAX_PATH];
    GetWindowsDirectoryA(path, MAX_PATH);
    strcat_s(path, "\\Fonts\\segoeui.ttf");
    if (GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES)
        io.Fonts->AddFontFromFileTTF(path, 18.0f);
    else
        io.Fonts->AddFontDefault();
}

bool init_imgui() {
    EnumWindows(find_game_window, (LPARAM)&g.hwnd);
    if (!g.hwnd) return false;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;  // the game owns the cursor

    ImGui::StyleColorsDark();
    ImGuiStyle& st = ImGui::GetStyle();
    st.WindowRounding = 6.0f;
    st.FrameRounding = 3.0f;
    st.WindowBorderSize = 1.0f;
    st.WindowPadding = ImVec2(8, 8);
    st.ItemSpacing = ImVec2(6, 5);
    st.Colors[ImGuiCol_WindowBg] = ImVec4(0.09f, 0.08f, 0.10f, 0.88f);
    st.Colors[ImGuiCol_PopupBg]  = ImVec4(0.10f, 0.09f, 0.11f, 0.96f);
    st.Colors[ImGuiCol_Border]   = ImVec4(0.55f, 0.45f, 0.30f, 0.60f);
    g.base_style = st;
    load_fonts();

    if (!ImGui_ImplWin32_Init(g.hwnd)) return false;
    g.prev_proc = (WNDPROC)SetWindowLongPtrW(g.hwnd, GWLP_WNDPROC, (LONG_PTR)&wndproc);
    QueryPerformanceCounter(&g.last_t);
    g.imgui_ok = true;
    log_line("overlay: ImGui up, hwnd=%p", (void*)g.hwnd);
    return true;
}

// The game recreates its GL context on a resolution change. GL names belong to
// the old context, so the renderer backend is re-initialised from scratch and
// its textures re-requested. Its old book-keeping is abandoned rather than
// destroyed: calling glDelete* in the NEW context could delete the game's own
// objects that happen to share the numbers.
bool ensure_gl() {
    HGLRC ctx = wglGetCurrentContext();
    if (!ctx) return false;
    if (g.gl_ok && ctx == g.gl_ctx) return true;

    ImGuiIO& io = ImGui::GetIO();
    if (g.gl_ok) {
        log_line("overlay: GL context changed (%p -> %p), rebuilding renderer", (void*)g.gl_ctx, (void*)ctx);
        io.BackendRendererUserData = nullptr;
        io.BackendRendererName = nullptr;
        io.BackendFlags &= ~(ImGuiBackendFlags_RendererHasVtxOffset | ImGuiBackendFlags_RendererHasTextures);
        for (ImTextureData* tex : ImGui::GetPlatformIO().Textures) {
            tex->SetTexID(ImTextureID_Invalid);
            tex->BackendUserData = nullptr;
            tex->SetStatus(ImTextureStatus_WantCreate);
        }
        g.gl_ok = false;
        assets_gl_lost();
    }
    g.bind_fb  = (fn_glBindFramebuffer)wglGetProcAddress("glBindFramebuffer");
    g.bind_buf = (fn_glBindBuffer)wglGetProcAddress("glBindBuffer");
    if (!ImGui_ImplOpenGL3_Init("#version 150")) return false;
    g.gl_ctx = ctx;
    g.gl_ok = true;
    return true;
}

void apply_scale(float display_h) {
    float s = display_h / 1080.0f;
    if (s < 0.75f) s = 0.75f;
    if (s > 2.5f)  s = 2.5f;
    if (s == g.ui_scale) return;
    g.ui_scale = s;
    ImGuiStyle& st = ImGui::GetStyle();
    st = g.base_style;
    st.ScaleAllSizes(s);
    st.FontScaleMain = s;
}

void render_frame() {
    LARGE_INTEGER now, freq;
    QueryPerformanceCounter(&now);
    QueryPerformanceFrequency(&freq);
    float dt = (float)(now.QuadPart - g.last_t.QuadPart) / (float)freq.QuadPart;
    g.last_t = now;
    if (dt <= 0.0f || dt > 0.25f) dt = 1.0f / 60.0f;

    bool in_battle = battle_active();
    if (!g.panel.loaded) panel_load(g.panel, g.game_dir);
    if (in_battle && !g.icons_ok && assets_ready()) g.icons_ok = status_icons_init();
    assets_upload_pending();
    if (in_battle) roster_build(g.roster);
    else g.roster.valid = false;

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplWin32_NewFrame();
    apply_scale(ImGui::GetIO().DisplaySize.y);
    ImGui::NewFrame();

    const void* hover = nullptr;
    if (in_battle && g.roster.valid)
        hover = panel_draw(g.roster, g.panel, dt, g.ui_scale);
    set_hover_unit(hover);

    ImGui::Render();
    g.capture_mouse = in_battle && ImGui::GetIO().WantCaptureMouse;

    // Draw into the default framebuffer with clean unpack state; the game may
    // have left an offscreen target or a PBO bound, and both fail silently.
    GLint fb = 0, pbo = 0, row = 0, skip_r = 0, skip_p = 0, align = 4;
    glGetIntegerv(kGL_DRAW_FRAMEBUFFER_BINDING, &fb);
    glGetIntegerv(kGL_PIXEL_UNPACK_BINDING, &pbo);
    glGetIntegerv(GL_UNPACK_ROW_LENGTH, &row);
    glGetIntegerv(GL_UNPACK_SKIP_ROWS, &skip_r);
    glGetIntegerv(GL_UNPACK_SKIP_PIXELS, &skip_p);
    glGetIntegerv(GL_UNPACK_ALIGNMENT, &align);
    if (g.bind_fb)  g.bind_fb(kGL_DRAW_FRAMEBUFFER, 0);
    if (g.bind_buf) g.bind_buf(kGL_PIXEL_UNPACK_BUFFER, 0);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glPixelStorei(GL_UNPACK_SKIP_ROWS, 0);
    glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0);

    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    glPixelStorei(GL_UNPACK_ROW_LENGTH, row);
    glPixelStorei(GL_UNPACK_SKIP_ROWS, skip_r);
    glPixelStorei(GL_UNPACK_SKIP_PIXELS, skip_p);
    glPixelStorei(GL_UNPACK_ALIGNMENT, align);
    if (g.bind_buf) g.bind_buf(kGL_PIXEL_UNPACK_BUFFER, (GLuint)pbo);
    if (g.bind_fb)  g.bind_fb(kGL_DRAW_FRAMEBUFFER, (GLuint)fb);
}

bool __cdecl h_swap(void* window) {
    if (!g.failed) {
        __try {
            if (!g.imgui_ok && !init_imgui()) {
                // The window may not exist on the first frames; try again later.
            } else if (g.imgui_ok && ensure_gl()) {
                render_frame();
            }
        } __except (log_exception("overlay", GetExceptionInformation())) {
            // One fault turns the overlay off for the session instead of
            // risking a fault every frame. The game keeps running.
            g.failed = true;
            g.capture_mouse = false;
            set_hover_unit(nullptr);
        }
    }
    return g.prev_swap(window);
}

}  // namespace

namespace {
// The DEFAULT stub SDL leaves in a slot before SDL_InitDynamicAPI runs ends in
// `jmp [slot]` -- it re-reads its own slot. Chaining to it after we have
// replaced the slot would call us forever, so a slot whose target still
// references the slot itself is not ready yet.
bool points_to_default_stub(const uint8_t* fn) {
    uint8_t code[96];
    if (!read_bytes(fn, code, sizeof(code))) return true;
    for (size_t i = 0; i + 6 <= sizeof(code); ++i) {
        // FF 25 rel32 (jmp [rip+x]) or 48 8B 05 rel32 (mov rax,[rip+x])
        size_t len = 0;
        if (code[i] == 0xFF && code[i + 1] == 0x25) len = 6;
        else if (i + 7 <= sizeof(code) && code[i] == 0x48 && code[i + 1] == 0x8B && code[i + 2] == 0x05) len = 7;
        if (!len) continue;
        int32_t rel;
        memcpy(&rel, code + i + len - 4, 4);
        if (fn + i + len + rel == (const uint8_t*)g.slot) return true;
    }
    return false;
}
}  // namespace

void overlay_set_game_dir(const char* dir) { g.game_dir = dir; }

void overlay_prepare() {
    g.slot = (void**)(g_base + kRva_SwapSlot);
    g.slot_stub = *g.slot;
}

void overlay_try_install(uint64_t frame) {
    if (g.installed || !g.slot) return;
    void* cur = *g.slot;
    if (points_to_default_stub((const uint8_t*)cur)) return;   // SDL not initialised yet
    g.prev_swap = (fn_swap)cur;
    *g.slot = (void*)&h_swap;   // .data, already writable
    g.installed = true;
    log_line("overlay: swap slot hooked at frame %llu (prev=%p)", (unsigned long long)frame, cur);
}

}  // namespace cr
