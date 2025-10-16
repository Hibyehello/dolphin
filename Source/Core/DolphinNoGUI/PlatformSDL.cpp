// Copyright 2025 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <string>

#include "Common/WindowSystemInfo.h"
#include "Platform.h"
#include "VideoCommon/Present.h"

#ifdef __APPLE__
  #include <dispatch/dispatch.h>
#endif

#include "Common/MsgHandler.h"
#include "Core/Config/MainSettings.h"
#include "Core/Core.h"
#include "Core/State.h"
#include "Core/System.h"
#include "SDL3/SDL_video.h"

namespace
{
class PlatformSDL : public Platform
{
public:
  ~PlatformSDL() override;

  bool Init() override;
  void SetTitle(const std::string& string) override;
  void MainLoop() override;

  WindowSystemInfo GetWindowSystemInfo() const override;

private:
  void ProcessEvents();
  void UpdateWindowPosition();

  SDL_Window* m_window;
  SDL_Surface* m_surface;

  int m_window_x = Config::Get(Config::MAIN_RENDER_WINDOW_XPOS);
  int m_window_y = Config::Get(Config::MAIN_RENDER_WINDOW_YPOS);
  unsigned int m_window_width = Config::Get(Config::MAIN_RENDER_WINDOW_WIDTH);
  unsigned int m_window_height = Config::Get(Config::MAIN_RENDER_WINDOW_HEIGHT);
};

PlatformSDL::~PlatformSDL()
{
  SDL_DestroySurface(m_surface);
  m_surface = nullptr;

  SDL_DestroyWindow(m_window);
  m_window = nullptr;

  SDL_Quit();
}

bool PlatformSDL::Init()
{
  if(!SDL_Init(SDL_INIT_VIDEO))
  {
    PanicAlertFmt("Failed to Initialize SDL!");
    return false;
  }

  m_window = SDL_CreateWindow("Dolphin-emu-nogui", m_window_width, m_window_height, SDL_WINDOW_RESIZABLE);

  if(m_window == nullptr)
  {
    PanicAlertFmt("Failed to create window!");
    return false;
  }

  m_surface = SDL_GetWindowSurface(m_window);

  return true;
}

void PlatformSDL::SetTitle(const std::string& string)
{
#ifndef __APPLE__
  SDL_SetWindowTitle(m_window, string.c_str());
#else
  dispatch_sync(dispatch_get_main_queue(), ^{SDL_SetWindowTitle(m_window, string.c_str());});
#endif
}

void PlatformSDL::MainLoop()
{
  while (IsRunning())
  {
    UpdateRunningFlag();
    Core::HostDispatchJobs(Core::System::GetInstance());
    ProcessEvents();
    UpdateWindowPosition();
  }
}

WindowSystemInfo PlatformSDL::GetWindowSystemInfo() const
{
  WindowSystemInfo wsi;
  SDL_PropertiesID props = SDL_GetWindowProperties(m_window);
#ifdef _WIN32
  wsi.type = WindowSystemType::Windows;
  wsi.render_window = (void*)SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WIN32_HWND_POINTER, NULL);
#endif
#ifdef __linux__
  if (SDL_strcmp(SDL_GetCurrentVideoDriver(), "x11") == 0)
  {
    wsi.type = WindowSystemType::X11;
    wsi.display_connection = (void*)SDL_GetPointerProperty(props, SDL_PROP_WINDOW_X11_DISPLAY_POINTER, NULL);
    wsi.render_window = (void*)SDL_GetPointerProperty(props, SDL_PROP_WINDOW_X11_WINDOW_NUMBER, NULL);
  } else if (SDL_strcmp(SDL_GetCurrentVideoDriver(), "wayland") == 0)
  {
    wsi.type = WindowSystemType::Wayland;
    wsi.display_connection = (void*)SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WAYLAND_DISPLAY_POINTER, NULL);
    wsi.render_window = (void*)SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WAYLAND_SURFACE_POINTER, NULL);
  }
#endif
#ifdef __APPLE__
  wsi.type = WindowSystemType::MacOS;
  wsi.render_window = getContentView((void*)SDL_GetPointerProperty(props, SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, NULL));
#endif

wsi.render_surface = wsi.render_window;
wsi.height = m_window_height;
wsi.width = m_window_width;

return wsi;
}

void PlatformSDL::ProcessEvents()
{
  SDL_Event e;


  while(SDL_PollEvent(&e))
  {
    switch(e.type)
    {
      case SDL_EVENT_QUIT:
        RequestShutdown();
        break;
      case SDL_EVENT_KEY_DOWN:
        if(e.key.key == SDLK_ESCAPE)
          RequestShutdown();

        break;

      default:
        break;
    }

    fprintf(stderr, "PlatformSDL: %d, %d\n", m_window_height, m_window_width);

    if(g_presenter)
      g_presenter->ResizeSurface(m_window_width, m_window_height);
  }
}

void PlatformSDL::UpdateWindowPosition()
{
  if(m_window_fullscreen)
    return;

  SDL_GetWindowPosition(m_window, &m_window_x, &m_window_y);
  SDL_GetWindowSize(m_window, (int*)&m_window_width, (int*)&m_window_height);
}

}

std::unique_ptr<Platform> Platform::CreateSDLPlatform()
{
  return std::make_unique<PlatformSDL>();
}
