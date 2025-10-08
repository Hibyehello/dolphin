// Copyright 2018 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <cstdio>
#include <thread>

#include "Core/Core.h"
#include "Core/System.h"
#include "PlatformCommon/Platform.h"
#include "imgui.h"

namespace
{
class PlatformHeadless final : public Platform
{
public:
  void SetTitle(const std::string& title) override;
  void MainLoop() override;

// ImGui Platform Functions
  bool ImGuiPlatformInit(void* window = nullptr) override;
  void InitMonitors(ImGuiPlatformIO& platform_io) override;
  void RegisterMainViewport() override;
  void CreateWindow(ImGuiViewport *vp) override;
  void DestroyWindow(ImGuiViewport* vp) override;
  ImVec2 GetWindowPos(ImGuiViewport* vp) override;
  void SetWindowPos(ImGuiViewport* vp, ImVec2 size) override;
  ImVec2 GetWindowSize(ImGuiViewport* vp) override;
  void SetWindowSize(ImGuiViewport* vp, ImVec2 size) override;
  void SetImGuiWindowTitle(ImGuiViewport* vp, const char* str) override;
  void ShowWindow(ImGuiViewport* vp) override;

  WindowSystemInfo GetWindowSystemInfo() const override;
};

void PlatformHeadless::SetTitle(const std::string& title)
{
  std::fprintf(stdout, "%s\n", title.c_str());
}

void PlatformHeadless::MainLoop()
{
  while (m_running.IsSet())
  {
    UpdateRunningFlag();
    Core::HostDispatchJobs(Core::System::GetInstance());
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
}

WindowSystemInfo PlatformHeadless::GetWindowSystemInfo() const
{
  WindowSystemInfo wsi;
  wsi.type = WindowSystemType::Headless;
  wsi.display_connection = nullptr;
  wsi.render_window = nullptr;
  wsi.render_surface = nullptr;
  return wsi;
}

bool PlatformHeadless::ImGuiPlatformInit(void* window)
{
  return true;
}

void PlatformHeadless::InitMonitors(ImGuiPlatformIO& platform_io)
{
  platform_io.Monitors.push_back({});
}

void PlatformHeadless::RegisterMainViewport()
{
  return;
}

void PlatformHeadless::CreateWindow(ImGuiViewport *vp)
{
  return;
}

void PlatformHeadless::DestroyWindow(ImGuiViewport *vp)
{
  return;
}

ImVec2 PlatformHeadless::GetWindowPos(ImGuiViewport *vp)
{
  return {};
}

void PlatformHeadless::SetWindowPos(ImGuiViewport *vp, ImVec2 size)
{
  return;
}

ImVec2 PlatformHeadless::GetWindowSize(ImGuiViewport *vp)
{
  return {};
}

void PlatformHeadless::SetWindowSize(ImGuiViewport *vp, ImVec2 size)
{
  return;
}

void PlatformHeadless::SetImGuiWindowTitle(ImGuiViewport *vp, const char *str)
{
  return;
}

void PlatformHeadless::ShowWindow(ImGuiViewport *vp)
{
  return;
}

}  // namespace

std::unique_ptr<Platform> Platform::CreateHeadlessPlatform()
{
  return std::make_unique<PlatformHeadless>();
}
