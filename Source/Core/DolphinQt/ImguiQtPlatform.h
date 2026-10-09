#pragma once

#include <imgui/imgui.h>

class QWidget;

void SetupImguiViewport(void* parent_widget, void* onscreen_ui);

void ImguiQt_CreateWindow(ImGuiViewport* vp);
void ImguiQt_DestroyWindow(ImGuiViewport* vp);
void ImguiQt_ShowWindow(ImGuiViewport* vp);
void ImguiQt_SetWindowPos(ImGuiViewport* vp, ImVec2 pos);
ImVec2 ImguiQt_GetWindowPos(ImGuiViewport* vp);
void ImguiQt_SetWindowSize(ImGuiViewport* vp, ImVec2 size);
ImVec2 ImguiQt_GetWindowSize(ImGuiViewport* vp);
void ImguiQt_SetWindowFocus(ImGuiViewport* vp);
bool ImguiQt_GetWindowFocus(ImGuiViewport* vp);
bool ImguiQt_GetWindowMinimized(ImGuiViewport* vp);
void ImguiQt_SetWindowTitle(ImGuiViewport* vp, const char* str);
float ImguiQt_GetWindowDpiScale(ImGuiViewport* vp);
