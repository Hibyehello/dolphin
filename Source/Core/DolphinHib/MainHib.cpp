#include "PlatformCommon/Platform.h"

#include <OptionParser.h>
#include <csignal>
#include <cstdio>
#include <imgui.h>
#include <string>
#include <vector>

#ifndef _WIN32
#include <unistd.h>
#else
#include <Windows.h>
#endif

#include "Common/ScopeGuard.h"
#include "Common/StringUtil.h"
#include "Core/Boot/Boot.h"
#include "Core/BootManager.h"
#include "Core/Core.h"
#include "Core/DolphinAnalytics.h"
#include "Core/Host.h"
#include "Core/System.h"

#include "UICommon/CommandLineParse.h"
#ifdef USE_DISCORD_PRESENCE
#include "UICommon/DiscordPresence.h"
#endif
#include "UICommon/UICommon.h"

#include "VideoCommon/OnScreenDisplay.h"
#include "VideoCommon/VideoBackendBase.h"
#include "VideoCommon/Present.h"

static std::unique_ptr<Platform> s_platform;
static std::thread s_hib_thread;

static void signal_handler(int)
{
  constexpr char message[] = "A signal was received. A second signal will force Dolphin to stop.\n";
#ifdef _WIN32
  puts(message);
#else
  if (write(STDERR_FILENO, message, sizeof(message)) < 0)
  {
  }
#endif

  s_platform->RequestShutdown();
}

std::vector<std::string> Host_GetPreferredLocales()
{
  return {};
}

void Host_PPCSymbolsChanged()
{
}

void Host_PPCBreakpointsChanged()
{
}

bool Host_UIBlocksControllerState()
{
  return false;
}

void Host_Message(const HostMessageID id)
{
  if (id == HostMessageID::WMUserStop)
    s_platform->Stop();
}

void Host_UpdateTitle(const std::string& title)
{
  s_platform->SetTitle(title);
}

void Host_UpdateDisasmDialog()
{
}

void Host_JitCacheInvalidation()
{
}

void Host_JitProfileDataWiped()
{
}

void Host_RequestRenderWindowSize(int width, int height)
{
}

bool Host_RendererHasFocus()
{
  return s_platform->IsWindowFocused();
}

bool Host_RendererHasFullFocus()
{
  // Mouse capturing isn't implemented
  return Host_RendererHasFocus();
}

bool Host_RendererIsFullscreen()
{
  return s_platform->IsWindowFullscreen();
}

bool Host_TASInputHasFocus()
{
  return false;
}

void Host_YieldToUI()
{
}

void Host_TitleChanged()
{
#ifdef USE_DISCORD_PRESENCE
  Discord::UpdateDiscordPresence();
#endif
}

void Host_UpdateDiscordClientID(const std::string& client_id)
{
#ifdef USE_DISCORD_PRESENCE
  Discord::UpdateClientID(client_id);
#endif
}

bool Host_UpdateDiscordPresenceRaw(const std::string& details, const std::string& state,
                                   const std::string& large_image_key,
                                   const std::string& large_image_text,
                                   const std::string& small_image_key,
                                   const std::string& small_image_text,
                                   const int64_t start_timestamp, const int64_t end_timestamp,
                                   const int party_size, const int party_max)
{
#ifdef USE_DISCORD_PRESENCE
  return Discord::UpdateDiscordPresenceRaw(details, state, large_image_key, large_image_text,
                                           small_image_key, small_image_text, start_timestamp,
                                           end_timestamp, party_size, party_max);
#else
  return false;
#endif
}

std::unique_ptr<GBAHostInterface> Host_CreateGBAHost(std::weak_ptr<HW::GBA::Core> core)
{
  return nullptr;
}

static void HibThread(WindowSystemInfo wsi) {
    Core::DeclareAsGPUThread();

    if (!g_video_backend->Initialize(wsi))
    {
        PanicAlertFmt("Failed to initialize video backend!");
        return;
    }

    while(s_platform->IsRunning()) {
        g_presenter->Present();

        ImGui::ShowDemoWindow();
    }
}

int main(const int argc, char* argv[])
{
    Core::DeclareAsHostThread();

    auto parser = CommandLineParse::CreateParser(CommandLineParse::ParserOptions::IncludeGUIOptions);
    const optparse::Values& options = CommandLineParse::ParseArguments(parser.get(), argc, argv);
    const std::vector<std::string> args = parser->args();

    
    #if HAVE_X11
        s_platform = Platform::CreateX11Platform();
    #endif

    #ifdef __linux__
        s_platform = Platform::CreateFBDevPlatform();
    #endif

    #ifdef _WIN32
        s_platform = Platform::CreateWin32Platform();
    #endif

    #ifdef __APPLE__
        s_platform = Platform::CreateMacOSPlatform();
    #endif

    if (!s_platform->Init())
    {
        fprintf(stderr, "Platform failed to initialize.\n");
        return 1;
    }

    s_platform->SetTitle("Dolphin Custom");

    const WindowSystemInfo wsi = s_platform->GetWindowSystemInfo();

    UICommon::SetUserDirectory(static_cast<const char*>(options.get("user")));
    UICommon::CreateDirectories();
    UICommon::Init();
    UICommon::InitControllers(wsi);

      Core::AddOnStateChangedCallback([](const Core::State state) {
    if (state == Core::State::Uninitialized)
      s_platform->Stop();
  });

    #ifdef _WIN32
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);
    #else
    // Shut down cleanly on SIGINT and SIGTERM
    struct sigaction sa;
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESETHAND;
    sigaction(SIGINT, &sa, nullptr);
    sigaction(SIGTERM, &sa, nullptr);
    #endif

    // Manually reactivate the video backend in case a GameINI overrides the video backend setting.
    VideoBackendBase::PopulateBackendInfo(wsi);

    // Issue any API calls which must occur on the main thread for the graphics backend.
    WindowSystemInfo prepared_wsi(wsi);
    g_video_backend->PrepareWindow(prepared_wsi);

    s_hib_thread = std::thread(HibThread, prepared_wsi);


    s_platform->MainLoop();
    Core::Stop(Core::System::GetInstance());
    
    Core::Shutdown(Core::System::GetInstance());
    s_platform.reset();

    return 0;
}
