#pragma once
#include <HeadlessRenderer/Definitions.h>
class View3D;
class RenderObject {
public:
	virtual void Render(ImVec2 displaySize, ImVec2 cursorPos, bool bMouseReleased) {
	};
};