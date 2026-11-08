#include <HeadlessRenderer/LiquidMenu.h>
#include <imgui/imgui.h>

namespace {
    void SectionHeader(const char* label) {
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.30f, 0.72f, 1.0f, 1.0f));
        ImGui::TextUnformatted(label);
        ImGui::PopStyleColor();
        ImGui::Separator();
        ImGui::Spacing();
    }

    void StepRow(const char* number, const char* title, const char* description) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.28f, 0.75f, 1.0f, 1.0f));
        ImGui::Text("%s", number);
        ImGui::PopStyleColor();
        ImGui::SameLine(36.0f);
        ImGui::TextUnformatted(title);
        ImGui::SameLine(190.0f);
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextWrapped("%s", description);
        ImGui::PopTextWrapPos();
        ImGui::Spacing();
    }

    void DrawPipeline() {
        const char* labels[] = { "Example.exe", "Target DX12", "Shared BGRA", "Present" };
        const ImU32 colors[] = {
            IM_COL32(48, 112, 190, 255), IM_COL32(50, 145, 190, 255),
            IM_COL32(56, 150, 112, 255), IM_COL32(128, 82, 190, 255)
        };
        const float boxWidth = 112.0f;
        const float boxHeight = 38.0f;
        const float gap = 34.0f;
        const float totalWidth = boxWidth * 4.0f + gap * 3.0f;
        const float startX = ImGui::GetCursorScreenPos().x +
            (ImGui::GetContentRegionAvail().x - totalWidth) * 0.5f;
        const float startY = ImGui::GetCursorScreenPos().y + 4.0f;
        ImDrawList* drawList = ImGui::GetWindowDrawList();

        for (int i = 0; i < 4; ++i) {
            const float x = startX + i * (boxWidth + gap);
            drawList->AddRectFilled(ImVec2(x, startY), ImVec2(x + boxWidth, startY + boxHeight),
                colors[i], 6.0f);
            drawList->AddRect(ImVec2(x, startY), ImVec2(x + boxWidth, startY + boxHeight),
                IM_COL32(255, 255, 255, 55), 6.0f);
            const ImVec2 textSize = ImGui::CalcTextSize(labels[i]);
            drawList->AddText(ImVec2(x + (boxWidth - textSize.x) * 0.5f,
                startY + (boxHeight - textSize.y) * 0.5f), IM_COL32_WHITE, labels[i]);
            if (i != 3) {
                const float arrowStart = x + boxWidth + 3.0f;
                const float arrowEnd = x + boxWidth + gap - 4.0f;
                const float y = startY + boxHeight * 0.5f;
                drawList->AddLine(ImVec2(arrowStart, y), ImVec2(arrowEnd, y),
                    IM_COL32(115, 195, 235, 230), 2.0f);
                drawList->AddTriangleFilled(ImVec2(arrowEnd, y), ImVec2(arrowEnd - 6.0f, y - 4.0f),
                    ImVec2(arrowEnd - 6.0f, y + 4.0f), IM_COL32(115, 195, 235, 230));
            }
        }
        ImGui::Dummy(ImVec2(totalWidth, boxHeight + 14.0f));
    }

    void TabOverview() {
        SectionHeader("External DX12 overlay");
        ImGui::TextWrapped(
            "The overlay is rendered by a separate process. Example.exe installs remote hooks, "
            "while HeadlessRenderer produces the UI into a shared D3D12 texture. The target's "
            "Present hook composites that texture onto the current back buffer. The design supports fullscreen use.");

        ImGui::Spacing();
        SectionHeader("Frame path");
        DrawPipeline();
        ImGui::TextWrapped(
            "The target process owns the D3D12 device and direct queue used for composition. "
            "The renderer uses a device on the same adapter and shares a texture and fence with the target.");

        ImGui::Spacing();
        SectionHeader("What stays external");
        ImGui::BulletText("No renderer DLL is loaded into the target process.");
        ImGui::BulletText("LiquidHookEx writes hook code and state into target memory.");
        ImGui::BulletText("Shared handles and a named handshake connect producer and consumer.");
        ImGui::BulletText("The target hook forwards to the original Present after composition.");
    }

    void TabRemoteHook() {
        SectionHeader("Bootstrap graphics context");
        ImGui::TextWrapped(
            "The graphics provider starts a remote bootstrap thread. It creates a helper window, "
            "DXGI factory, D3D12 device, direct command queue, and helper swap chain. Those COM "
            "pointers belong to the target process: the injector treats them as opaque addresses "
            "and releases them through the remote release path.");

        ImGui::Spacing();
        SectionHeader("Hooks installed by Example.exe");
        StepRow("01", "Present entry", "Resolves the Present slot from the bootstrap swap chain and installs the DXGI hook.");
        StepRow("02", "Queue capture", "Hooks ID3D12CommandQueue::ExecuteCommandLists and remembers a target direct queue.");
        StepRow("03", "Adapter selection", "The first target Present reports its adapter LUID; the renderer is created on that adapter.");
        StepRow("04", "Composition", "The callback imports shared resources, records a full-screen triangle, submits it, then calls original Present.");

        ImGui::Spacing();
        SectionHeader("Remote callback constraints");
        ImGui::TextWrapped(
            "Hook callbacks run inside the target and are copied as shellcode. Their state lives in "
            "target-side hook data. Interface pointers stored there must only be used or released "
            "from the target process.");
    }

    void TabSharedFrame() {
        SectionHeader("Named handshake");
        ImGui::TextWrapped(
            "HeadlessRenderer publishes a DX12Overlay::Handshake in a named Windows file mapping. "
            "It contains the protocol version, target PID, texture size and format, adapter LUID, "
            "generation, and shared resource/fence handle values duplicated into the target.");
        ImGui::Spacing();
        ImGui::TextDisabled("Local\\LiquidOverlayHandshake_DX12_<target PID>");

        ImGui::Spacing();
        SectionHeader("Shared texture synchronization");
        ImGui::BulletText("Producer renders ImGui into a BGRA shared texture.");
        ImGui::BulletText("Producer signals ready value 2 * frame + 1.");
        ImGui::BulletText("Target waits briefly for that value, composites the texture, and signals 2 * frame + 2.");
        ImGui::BulletText("Producer waits for the consumer value before reusing the texture.");

        ImGui::Spacing();
        SectionHeader("Why use a fence?");
        ImGui::TextWrapped(
            "D3D12 work is asynchronous. The shared fence orders access to the texture across "
            "the renderer and target queues. A short producer wait in Present lets the game "
            "continue presenting if a new overlay frame is not ready yet.");
    }

    void TabRenderer() {
        SectionHeader("HeadlessRenderer");
        ImGui::TextWrapped(
            "HeadlessRenderer creates a D3D12 device on the selected adapter, allocates the shared "
            "BGRA texture and fence, and runs an ImGui render loop without a presentation swap chain.");

        ImGui::Spacing();
        SectionHeader("Input and UI");
        ImGui::BulletText("Mouse position and buttons are sampled relative to the target window.");
        ImGui::BulletText("A low-level mouse hook collects wheel input for ImGui.");
        ImGui::BulletText("RenderObjectManager draws the menu and registered render objects into the texture.");

        ImGui::Spacing();
        SectionHeader("Recovery");
        ImGui::TextWrapped(
            "Example.exe watches the target adapter and the composited frame counter. It can "
            "restart the renderer after an adapter change or stalled frame stream. A new handshake "
            "generation tells the Present hook to release its old imports and rebuild against the new texture and fence.");
        ImGui::Spacing();
        ImGui::TextDisabled("Press DELETE in Example.exe to stop the renderer and remove the hooks.");
    }
}

void RenderLiquidMenu() {
    const ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f),
        ImGuiCond_Once, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(700.0f, 520.0f), ImGuiCond_Once);
    ImGui::SetNextWindowBgAlpha(0.96f);

    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse;
    if (!ImGui::Begin("LiquidHookEx | External DX12 Overlay", nullptr, flags)) {
        ImGui::End();
        return;
    }

    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.55f, 0.60f, 0.68f, 1.0f));
    ImGui::Text("DX12   |   x64 Windows   |   FULLSCREEN");
    ImGui::PopStyleColor();
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 145.0f);
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.30f, 0.82f, 0.58f, 1.0f));
    ImGui::Text("EXTERNAL PROCESS");
    ImGui::PopStyleColor();
    ImGui::Separator();

    if (ImGui::BeginTabBar("##Dx12OverviewTabs")) {
        if (ImGui::BeginTabItem("Overview")) {
            ImGui::BeginChild("##dx12Overview", ImVec2(0, 0), false, ImGuiWindowFlags_AlwaysVerticalScrollbar);
            TabOverview();
            ImGui::EndChild();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Remote hook")) {
            ImGui::BeginChild("##dx12RemoteHook", ImVec2(0, 0), false, ImGuiWindowFlags_AlwaysVerticalScrollbar);
            TabRemoteHook();
            ImGui::EndChild();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Shared frame")) {
            ImGui::BeginChild("##dx12SharedFrame", ImVec2(0, 0), false, ImGuiWindowFlags_AlwaysVerticalScrollbar);
            TabSharedFrame();
            ImGui::EndChild();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Renderer & recovery")) {
            ImGui::BeginChild("##dx12Renderer", ImVec2(0, 0), false, ImGuiWindowFlags_AlwaysVerticalScrollbar);
            TabRenderer();
            ImGui::EndChild();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::End();
}
