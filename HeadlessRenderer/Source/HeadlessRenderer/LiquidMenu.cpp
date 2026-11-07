#include <HeadlessRenderer/LiquidMenu.h>
#include <imgui/imgui.h>

// ─────────────────────────────────────────────────────────────────────────────
//  Helpers
// ─────────────────────────────────────────────────────────────────────────────

static void HelpMarker(const char* desc)
{
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered())
    {
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 28.0f);
        ImGui::TextUnformatted(desc);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

static void SectionHeader(const char* label)
{
    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.30f, 0.65f, 1.0f, 1.0f));
    ImGui::TextUnformatted(label);
    ImGui::PopStyleColor();
    ImGui::Separator();
    ImGui::Spacing();
}

static void Badge(const char* text, ImVec4 col)
{
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImVec2 textSz = ImGui::CalcTextSize(text);
    float  pad = 4.0f;
    ImVec2 rectMin = p;
    ImVec2 rectMax = ImVec2(p.x + textSz.x + pad * 2, p.y + textSz.y + pad * 0.8f);

    ImGui::GetWindowDrawList()->AddRectFilled(rectMin, rectMax, ImGui::ColorConvertFloat4ToU32(col), 4.0f);
    ImGui::SetCursorScreenPos(ImVec2(p.x + pad, p.y + pad * 0.4f));
    ImGui::TextUnformatted(text);
    ImGui::SetCursorScreenPos(ImVec2(p.x, rectMax.y + 2.0f));
}

static void InlineCode(const char* text)
{
    ImVec4 bgCol = ImVec4(0.12f, 0.14f, 0.20f, 1.0f);
    ImVec4 fgCol = ImVec4(0.80f, 0.85f, 1.00f, 1.0f);
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImVec2 textSz = ImGui::CalcTextSize(text);
    float  padX = 5.0f, padY = 2.0f;
    ImVec2 rMin = p;
    ImVec2 rMax = ImVec2(p.x + textSz.x + padX * 2, p.y + textSz.y + padY * 2);

    ImGui::GetWindowDrawList()->AddRectFilled(rMin, rMax, ImGui::ColorConvertFloat4ToU32(bgCol), 3.0f);
    ImGui::SetCursorScreenPos(ImVec2(p.x + padX, p.y + padY));
    ImGui::PushStyleColor(ImGuiCol_Text, fgCol);
    ImGui::TextUnformatted(text);
    ImGui::PopStyleColor();
    ImGui::SetCursorScreenPos(ImVec2(p.x + rMax.x - rMin.x + 6.0f, p.y));
    ImGui::Dummy(ImVec2(0, rMax.y - rMin.y));
    ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + rMax.y - rMin.y + 2.0f));
}

// Draw a simple horizontal flow diagram using the draw list
static void DrawFlowDiagram()
{
    const float nodeW = 130.0f;
    const float nodeH = 36.0f;
    const float gapX = 44.0f;
    const float rounding = 6.0f;

    struct Node { const char* label; ImVec4 col; };
    Node nodes[] = {
        { "HeadlessRenderer",  ImVec4(0.15f, 0.40f, 0.70f, 1.0f) },
        { "SharedTexture\n(KeyedMutex)", ImVec4(0.20f, 0.55f, 0.35f, 1.0f) },
        { "hkPresent\n(<your_target>.exe)",        ImVec4(0.65f, 0.30f, 0.20f, 1.0f) },
        { "Backbuffer\nComposite",        ImVec4(0.45f, 0.20f, 0.60f, 1.0f) },
    };

    const int   count = (int)(sizeof(nodes) / sizeof(nodes[0]));
    const float totalW = count * nodeW + (count - 1) * gapX;

    ImVec2 cursor = ImGui::GetCursorScreenPos();
    float  startX = cursor.x + (ImGui::GetContentRegionAvail().x - totalW) * 0.5f;
    float  startY = cursor.y + 6.0f;

    ImDrawList* dl = ImGui::GetWindowDrawList();

    for (int i = 0; i < count; ++i)
    {
        float x0 = startX + i * (nodeW + gapX);
        float y0 = startY;
        float x1 = x0 + nodeW;
        float y1 = y0 + nodeH;

        // Box
        dl->AddRectFilled(ImVec2(x0, y0), ImVec2(x1, y1),
            ImGui::ColorConvertFloat4ToU32(nodes[i].col), rounding);
        dl->AddRect(ImVec2(x0, y0), ImVec2(x1, y1),
            IM_COL32(255, 255, 255, 40), rounding);

        // Label (centred, multiline-aware)
        ImVec2 tsz = ImGui::CalcTextSize(nodes[i].label, nullptr, false, nodeW - 8.0f);
        float  tx = x0 + (nodeW - tsz.x) * 0.5f;
        float  ty = y0 + (nodeH - tsz.y) * 0.5f;
        dl->AddText(nullptr, 0.0f, ImVec2(tx, ty),
            IM_COL32(255, 255, 255, 230), nodes[i].label, nullptr, nodeW - 8.0f);

        // Arrow to next
        if (i < count - 1)
        {
            float ax0 = x1 + 2.0f;
            float ax1 = x1 + gapX - 2.0f;
            float ay = y0 + nodeH * 0.5f;
            dl->AddLine(ImVec2(ax0, ay), ImVec2(ax1, ay), IM_COL32(180, 180, 180, 180), 1.5f);
            // Arrowhead
            dl->AddTriangleFilled(
                ImVec2(ax1, ay),
                ImVec2(ax1 - 7.0f, ay - 4.0f),
                ImVec2(ax1 - 7.0f, ay + 4.0f),
                IM_COL32(180, 180, 180, 180));
        }
    }

    // Advance cursor past the diagram
    ImGui::Dummy(ImVec2(totalW, nodeH + 14.0f));
}

// ─────────────────────────────────────────────────────────────────────────────
//  Tab renderers
// ─────────────────────────────────────────────────────────────────────────────

static void Tab_Overview()
{
    SectionHeader("  What is this project?");

    ImGui::TextWrapped(
        "External-DX11-Hook is a two-process overlay framework for DX11."
        "It renders a fully interactive ImGui UI in a "
        "completely separate, headless Direct3D 11 process, then composites the "
        "result onto the proc's back-buffer every frame via a hooked "
        "IDXGISwapChain::Present.");

    ImGui::Spacing();
    SectionHeader("  Frame pipeline");
    DrawFlowDiagram();

    SectionHeader("  Key design goals");

    const char* goals[] = {
        "No DLL injection  -  runs entirely as an external process.",
        "Zero game-state coupling  -  overlay and hook are decoupled by IPC.",
        "Full D3D11 state save/restore  -  the hook leaves the renderer spotless.",
        "IDXGIKeyedMutex sync  -  GPU-side cross-process synchronisation, no spin-locks.",
        "LiquidHookEx VTable engine  -  shellcode-section hooks with remote trampolines.",
    };
    for (auto* g : goals)
    {
        ImGui::Bullet();
        ImGui::TextWrapped("%s", g);
    }

    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.6f, 0.6f, 1.0f));
    ImGui::TextWrapped("Press DELETE at any time to cleanly unhook and exit.");
    ImGui::PopStyleColor();
}

// ─────────────────────────────────────────────────────────────────────────────

static void Tab_Architecture()
{
    SectionHeader("  Projects / modules");

    struct Module { const char* name; const char* role; ImVec4 accent; };
    Module modules[] = {
        { "Overlay.dll",        "HeadlessRenderer + ImGui render loop (producer side)",  ImVec4(0.25f,0.55f,1.0f,1.0f) },
        { "Example.exe",    "Bootstrapper: inits LiquidHookEx, obtains dummy swapchain, hooks Present",       ImVec4(1.0f, 0.55f,0.20f,1.0f) },
        { "LiquidHookEx.lib",   "External-process VTable / Detour / CallSite hook engine", ImVec4(0.30f,0.75f,0.45f,1.0f) },
    };

    for (auto& m : modules)
    {
        ImGui::PushStyleColor(ImGuiCol_Text, m.accent);
        ImGui::Text("  %s", m.name);
        ImGui::PopStyleColor();
        ImGui::SameLine();
        ImGui::SetCursorPosX(200.0f);
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.75f, 0.75f, 0.75f, 1.0f));
        ImGui::TextWrapped("%s", m.role);
        ImGui::PopStyleColor();
        ImGui::Separator();
    }

    SectionHeader("  Handshake protocol (IPC)");
    ImGui::TextWrapped(
        "Before hooking, HeadlessRenderer publishes a SharedOverlayHandshake struct "
        "into a named Windows file mapping:");

    ImGui::Spacing();
    ImGui::Indent(12.0f);
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.80f, 0.85f, 1.00f, 1.0f));
    ImGui::Text("struct SharedOverlayHandshake {");
    ImGui::Text("    HANDLE  sharedTextureHandle;  // NT kernel handle");
    ImGui::Text("    UINT    width, height;         // texture dimensions");
    ImGui::Text("    DWORD   producerPid;            // overlay process PID");
    ImGui::Text("};");
    ImGui::PopStyleColor();
    ImGui::Unindent(12.0f);

    ImGui::Spacing();
    ImGui::TextWrapped(
        "On the first Present() call after hooking, the shellcode reads this mapping "
        "from inside <target>.exe's address space, calls OpenSharedResource() to import "
        "the texture handle, then composites it every frame.");

    SectionHeader("  Synchronisation");
    ImGui::TextWrapped(
        "Cross-process texture access is serialised with IDXGIKeyedMutex:");

    ImGui::Spacing();
    ImGui::Indent(12.0f);
    ImGui::Bullet(); ImGui::Text("Producer (overlay) holds key 0  ->  renders into the texture.");
    ImGui::Bullet(); ImGui::Text("ReleaseSync(key 1)  ->  hands ownership to the game.");
    ImGui::Bullet(); ImGui::Text("Consumer (hkPresent) acquires key 1  ->  composites, then ReleaseSync(0).");
    ImGui::Unindent(12.0f);

    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.85f, 0.70f, 0.20f, 1.0f));
    ImGui::TextWrapped("  No CPU spin-locks. All waiting happens inside the GPU driver.");
    ImGui::PopStyleColor();
}

// ─────────────────────────────────────────────────────────────────────────────

static void Tab_Hook()
{
    SectionHeader("  LiquidHookEx - VTable hook engine");
    ImGui::TextWrapped(
        "LiquidHookEx is an external-process hooking library (x64, Windows). "
        "It allocates a trampoline inside the target process, writes a 14-byte "
        "FF 25 absolute indirect jump to redirect execution, and injects a "
        "position-independent shellcode payload that carries all state in a "
        "remote PresentHookData struct.");

    ImGui::Spacing();
    SectionHeader("  IDXGISwapChain::Present - slot 8");

    ImGui::Indent(12.0f);
    ImGui::Bullet(); ImGui::TextWrapped("Address resolved via GetVTableFunction<8>(swapChain).");
    ImGui::Bullet(); ImGui::TextWrapped("Hook installed with HookUsingAddr<PresentHookData>().");
    ImGui::Bullet(); ImGui::TextWrapped(
        "Two RipSlots are patched into the shellcode body: "
        "g_pOriginalPresent (trampoline address) and g_pHookData (remote struct pointer).");
    ImGui::Unindent(12.0f);

    ImGui::Spacing();
    SectionHeader("  hkPresent - per-frame work");

    struct Step { const char* num; const char* desc; };
    Step steps[] = {
        { "1", "First call: init D3D11 device ref, compile VS/PS, open shared texture, create SRV + keyed mutex." },
        { "2", "Save full D3D11StateBlock (shaders, IA, OM, RS, viewports, SRVs, samplers)." },
        { "3", "AcquireSync(key 1, 8 ms timeout) on the overlay texture." },
        { "4", "Get back-buffer, create RTV, set fullscreen viewport." },
        { "5", "Draw 4 verts as TRIANGLESTRIP - fullscreen-quad via SV_VertexID, no VB needed." },
        { "6", "Restore full D3D11StateBlock, release all captured COM refs." },
        { "7", "ReleaseSync(key 0) - returns texture to the overlay producer." },
        { "8", "Call original Present trampoline." },
    };
    for (auto& s : steps)
    {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.30f, 0.65f, 1.0f, 1.0f));
        ImGui::Text("  [%s]", s.num);
        ImGui::PopStyleColor();
        ImGui::SameLine();
        ImGui::TextWrapped("%s", s.desc);
    }

    ImGui::Spacing();
    SectionHeader("  Composite shaders (HLSL)");
    ImGui::TextWrapped("Compiled at hook-install time into PresentHookData::vsBlob / psBlob (≤ 4096 bytes each):");
    ImGui::Spacing();
    ImGui::Indent(12.0f);
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.80f, 0.85f, 1.00f, 1.0f));
    ImGui::Text("VS: SV_VertexID -> uv -> SV_Position  (no vertex buffer)");
    ImGui::Text("PS: Texture2D.Sample(s, uv)  ->  SV_Target");
    ImGui::Text("Blend: SRC_ALPHA / INV_SRC_ALPHA  (straight alpha)");
    ImGui::PopStyleColor();
    ImGui::Unindent(12.0f);
}

// ─────────────────────────────────────────────────────────────────────────────

static void Tab_Renderer()
{
    SectionHeader("  HeadlessRenderer - overlay producer");
    ImGui::TextWrapped(
        "HeadlessRenderer owns a dedicated D3D11 device (no swap chain, no window). "
        "It renders ImGui into a DXGI_FORMAT_B8G8R8A8_UNORM shared texture with "
        "D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX, then publishes the NT handle via "
        "the handshake file mapping.");

    ImGui::Spacing();
    SectionHeader("  Thread model");

    struct Thread { const char* name; const char* purpose; };
    Thread threads[] = {
        { "Render thread",       "Device init -> shared texture -> PublishHandshake -> ImGui render loop." },
        { "ScrollHook thread",   "Installs WH_MOUSE_LL; pumps messages; feeds wheel delta to ImGuiIO via InterlockedAdd." },
        { "Resize watch thread", "Polls GetClientRect; calls Resize() when game window changes. (disabled - see known issues)" },
    };
    for (auto& t : threads)
    {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.30f, 0.75f, 0.45f, 1.0f));
        ImGui::Text("  %s", t.name);
        ImGui::PopStyleColor();
        ImGui::SameLine();
        ImGui::SetCursorPosX(170.0f);
        ImGui::TextWrapped("%s", t.purpose);
        ImGui::Separator();
    }

    SectionHeader("  Input feeding (FeedInput)");
    ImGui::Bullet(); ImGui::TextWrapped("GetCursorPos + ScreenToClient -> ImGuiIO::MousePos.");
    ImGui::Bullet(); ImGui::TextWrapped("GetAsyncKeyState for LMB / RMB / MMB.");
    ImGui::Bullet(); ImGui::TextWrapped("InterlockedExchange on s_wheelAccum -> ImGuiIO::MouseWheel.");
    ImGui::Bullet(); ImGui::TextWrapped("DeltaTime from GetTickCount64 (no QPC overhead).");

    ImGui::Spacing();
    SectionHeader("  RenderObjectManager");
    ImGui::TextWrapped(
        "A simple polymorphic list of RenderObject* instances. Each object implements "
        "virtual void Render(ImVec2 displaySize, ImVec2 cursorPos, bool bMouseReleased). "
        "Objects are pushed via AddRenderObject() and iterated each frame.");
}


// ─────────────────────────────────────────────────────────────────────────────

static void Tab_KnownIssues()
{
    SectionHeader("  Known issues");

    struct Issue { const char* title; const char* detail; bool critical; };
    Issue issues[] = {
        {
            "bHandleResize = false (crashes)",
            "The resize path inside hkPresent calls g_overlayMutex->Release() while "
            "still holding the AcquireSync(key 1) lock - a keyed mutex protocol "
            "violation. Fix: call ReleaseSync(0) before releasing + nulling resources, "
            "then return early to let the next frame re-init cleanly.",
            true
        }
    };

    for (auto& issue : issues)
    {
        ImVec4 headerCol = issue.critical
            ? ImVec4(1.0f, 0.40f, 0.30f, 1.0f)
            : ImVec4(0.85f, 0.70f, 0.20f, 1.0f);

        if (ImGui::CollapsingHeader(issue.title))
        {
            ImGui::Indent(12.0f);
            ImGui::PushStyleColor(ImGuiCol_Text, headerCol);
            if (issue.critical)
                ImGui::TextWrapped("  CRITICAL");
            else
                ImGui::TextWrapped("  LOW / COSMETIC");
            ImGui::PopStyleColor();
            ImGui::Spacing();
            ImGui::TextWrapped("%s", issue.detail);
            ImGui::Unindent(12.0f);
            ImGui::Spacing();
        }
    }

    ImGui::Spacing();
    SectionHeader("  Planned improvements");
    ImGui::Bullet(); ImGui::TextWrapped("Resize support with correct mutex teardown.");
    ImGui::Bullet(); ImGui::TextWrapped("Proper keyboard input via WH_KEYBOARD_LL for text-entry widgets.");
}

// ─────────────────────────────────────────────────────────────────────────────
//  Entry point
// ─────────────────────────────────────────────────────────────────────────────

void RenderLiquidMenu()
{
    const ImGuiIO& io = ImGui::GetIO();

    // Centre the window on first use
    ImGui::SetNextWindowPos(
        ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f),
        ImGuiCond_Once,
        ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(640.0f, 500.0f), ImGuiCond_Once);
    ImGui::SetNextWindowBgAlpha(0.94f);

    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoScrollWithMouse;

    if (!ImGui::Begin("  LiquidHookEx  |  External DX11 Hook", nullptr, flags))
    {
        ImGui::End();
        return;
    }

    // ── version / badge row ──────────────────────────────────────────────────
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.50f, 0.50f, 0.55f, 1.0f));
    ImGui::Text("v1.0   x64 Windows   DX11   Generic");
    ImGui::PopStyleColor();
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 150.0f);
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.30f, 0.75f, 0.45f, 1.0f));
    ImGui::Text("EXTERNAL  |  NO-INJECT");
    ImGui::PopStyleColor();
    ImGui::Separator();
    ImGui::Spacing();

    // ── tab bar ──────────────────────────────────────────────────────────────
    if (ImGui::BeginTabBar("##MainTabs", ImGuiTabBarFlags_None))
    {
        if (ImGui::BeginTabItem("  Overview  "))
        {
            ImGui::BeginChild("##ov", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);
            Tab_Overview();
            ImGui::EndChild();
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("  Architecture  "))
        {
            ImGui::BeginChild("##arch", ImVec2(0, 0), false);
            Tab_Architecture();
            ImGui::EndChild();
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("  Hook  "))
        {
            ImGui::BeginChild("##hook", ImVec2(0, 0), false);
            Tab_Hook();
            ImGui::EndChild();
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("  Renderer  "))
        {
            ImGui::BeginChild("##rend", ImVec2(0, 0), false);
            Tab_Renderer();
            ImGui::EndChild();
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("  Known Issues  "))
        {
            ImGui::BeginChild("##issues", ImVec2(0, 0), false);
            Tab_KnownIssues();
            ImGui::EndChild();
            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }

    ImGui::End();
}