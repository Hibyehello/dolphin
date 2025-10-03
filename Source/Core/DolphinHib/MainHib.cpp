#include "Core/HotkeyManager.h"
#include "PlatformCommon/Platform.h"
#include <nfd.h>

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

#include "GameList.h"

#include "Common/ScopeGuard.h"
#include "Common/StringUtil.h"
#include "Common/Thread.h"
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
  Common::SetCurrentThreadName("HibUI");
  
    if (!g_video_backend->Initialize(wsi))
    {
        PanicAlertFmt("Failed to initialize video backend!");
        return;
    }

  while(s_platform->IsRunning()) {
    g_presenter->PresentUI();

    ImGui::Begin("Dolphin", NULL, ImGuiWindowFlags_NoTitleBar);
    if(ImGui::Button("Click me!")) {
        HibUI::GameList::SetIsoPath();
      }
    ImGui::End();

    Common::SleepCurrentThread(16);
  }

    g_video_backend->Shutdown();
}

int main(const int argc, char* argv[])
{
  Core::DeclareAsHostThread();

  auto parser = CommandLineParse::CreateParser(CommandLineParse::ParserOptions::IncludeGUIOptions);
  optparse::Values& options = CommandLineParse::ParseArguments(parser.get(), argc, argv);
  std::vector<std::string> args = parser->args();

  std::optional<std::string> save_state_path;
  if (options.is_set("save_state"))
  {
    save_state_path = static_cast<const char*>(options.get("save_state"));
  }

  std::unique_ptr<BootParameters> boot;
  bool game_specified = false;
  if (options.is_set("exec"))
  {
    const std::list<std::string> paths_list = options.all("exec");
    const std::vector<std::string> paths{std::make_move_iterator(std::begin(paths_list)),
                                        std::make_move_iterator(std::end(paths_list))};
    boot = BootParameters::GenerateFromFile(
        paths, BootSessionData(save_state_path, DeleteSavestateAfterBoot::No));
    game_specified = true;
  }
  else if (options.is_set("nand_title"))
  {
    const std::string hex_string = static_cast<const char*>(options.get("nand_title"));
    if (hex_string.length() != 16)
    {
      fprintf(stderr, "Invalid title ID\n");
      parser->print_help();
      return 1;
    }
    const u64 title_id = std::stoull(hex_string, nullptr, 16);
    boot = std::make_unique<BootParameters>(BootParameters::NANDTitle{title_id});
  }
  else if (args.size())
  {
    boot = BootParameters::GenerateFromFile(
        args.front(), BootSessionData(save_state_path, DeleteSavestateAfterBoot::No));
    args.erase(args.begin());
    game_specified = true;
  }

  std::string user_directory;
  if (options.is_set("user"))
    user_directory = static_cast<const char*>(options.get("user"));

  
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

  UICommon::SetUserDirectory(user_directory);
  UICommon::CreateDirectories();
  UICommon::Init();
  UICommon::InitControllers(wsi);
  NFD_Init();
  
  Common::ScopeGuard ui_common_guard([] {
    UICommon::ShutdownControllers();
    UICommon::Shutdown();
  });

  if (save_state_path && !game_specified)
  {
    fprintf(stderr, "A save state cannot be loaded without specifying a game to launch.\n");
    return 1;
  }

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

  if(game_specified)
  {
    if (!BootManager::BootCore(Core::System::GetInstance(), std::move(boot), wsi))
    {
      fprintf(stderr, "Could not boot the specified file\n");
      return 1;
    }
  }
  else 
  {
    // Manually reactivate the video backend in case a GameINI overrides the video backend setting.
    VideoBackendBase::PopulateBackendInfo(wsi);

    // Issue any API calls which must occur on the main thread for the graphics backend.
    WindowSystemInfo prepared_wsi(wsi);
    g_video_backend->PrepareWindow(prepared_wsi);

    s_hib_thread = std::thread(HibThread, prepared_wsi);
  }

  s_platform->MainLoop();
  Core::Stop(Core::System::GetInstance());

  Core::Shutdown(Core::System::GetInstance());

  // Kill Our UI Thread for if dolphin is ran without a game
  if(!game_specified)
    s_hib_thread.join();

  NFD_Quit();

  s_platform.reset();

  return 0;
}
