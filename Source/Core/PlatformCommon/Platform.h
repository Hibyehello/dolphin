// Copyright 2018 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <imgui.h>
#include <memory>
#include <string>

#include "Common/Flag.h"
#include "Common/WindowSystemInfo.h"

class Platform
{
public:
  virtual ~Platform();

  bool IsRunning() const { return m_running.IsSet(); }
  bool IsWindowFocused() const { return m_window_focus; }
  bool IsWindowFullscreen() const { return m_window_fullscreen; }

  virtual bool Init();
  virtual void SetTitle(const std::string& title);
  virtual void MainLoop() = 0;

  virtual WindowSystemInfo GetWindowSystemInfo() const = 0;

  // Requests a graceful shutdown, from SIGINT/SIGTERM.
  void RequestShutdown();

  // Request an immediate shutdown.
  void Stop();

  static std::unique_ptr<Platform> CreateHeadlessPlatform();
#ifdef HAVE_X11
  static std::unique_ptr<Platform> CreateX11Platform();
#endif

#ifdef __linux__
  static std::unique_ptr<Platform> CreateFBDevPlatform();
#endif

#ifdef _WIN32
  static std::unique_ptr<Platform> CreateWin32Platform();
#endif

#ifdef __APPLE__
  static std::unique_ptr<Platform> CreateMacOSPlatform();
#endif

// Imgui Platform Functions
virtual bool ImGuiPlatformInit(void* = nullptr) = 0;
virtual void InitMonitors(ImGuiPlatformIO& platform_io) = 0;
virtual void RegisterMainViewport() = 0;
virtual void CreateWindow(ImGuiViewport* vp) = 0;
virtual void DestroyWindow(ImGuiViewport* vp) = 0;
virtual ImVec2 GetWindowPos(ImGuiViewport* vp) = 0;
virtual void SetWindowPos(ImGuiViewport* vp, ImVec2 pos) = 0;
virtual ImVec2 GetWindowSize(ImGuiViewport* vp) = 0;
virtual void SetWindowSize(ImGuiViewport* vp, ImVec2 size) = 0;
virtual void SetImGuiWindowTitle(ImGuiViewport* vp, const char* str) = 0;
virtual void ShowWindow(ImGuiViewport* vp) = 0;

protected:
  void UpdateRunningFlag();

  Common::Flag m_running{true};
  Common::Flag m_shutdown_requested{false};
  Common::Flag m_tried_graceful_shutdown{false};

  bool m_window_focus = true;  // Should be made atomic if actually implemented
  bool m_window_fullscreen = false;
};

extern std::unique_ptr<Platform> s_platform;