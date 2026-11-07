#pragma once
#include <vector>
#include <HeadlessRenderer/RenderObject.h>
struct ImVec2;
class View3D;
class RenderObjectManager {
public:
	std::vector<RenderObject*> renderObjects;
public:
	RenderObjectManager(std::vector<RenderObject*> _renderObjects) {
		renderObjects = _renderObjects;
	}
	void AddRenderObject(RenderObject* obj) {
		renderObjects.push_back(obj);
	}


private:
	static void RenderWatermark(ImVec2 displaySize);
public:
	void Render(ImVec2 displaySize, ImVec2 cursorPos, bool bMouseReleased);
};