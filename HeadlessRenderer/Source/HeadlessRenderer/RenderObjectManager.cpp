#include <HeadlessRenderer/Definitions.h>
#include <imgui/imgui.h>
#include <HeadlessRenderer/RenderObjectManager.h>
#include <HeadlessRenderer/RenderObject.h>

void RenderObjectManager::RenderWatermark(ImVec2 displaySize) {
	auto pDrawList = ImGui::GetForegroundDrawList();
	pDrawList->AddText(ImVec2(5, 5), ImColor(255, 0, 255, 230), "xsip's PureLiquid v0.1");
}

void RenderObjectManager::Render(ImVec2 displaySize, ImVec2 cursorPos, bool bMouseReleased) {
	RenderWatermark(displaySize);
	for (auto& renderObject : renderObjects) {
		renderObject->Render(displaySize, cursorPos, bMouseReleased);
	}
}