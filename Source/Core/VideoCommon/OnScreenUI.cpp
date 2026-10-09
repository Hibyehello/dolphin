// Copyright 2023 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "VideoCommon/OnScreenUI.h"

#include "Common/CommonPaths.h"
#include "Common/EnumMap.h"
#include "Common/FileUtil.h"
#include "Common/Logging/Log.h"
#include "Common/Profiler.h"
#include "Common/Timer.h"

#include "Core/AchievementManager.h"
#include "Core/Config/AchievementSettings.h"
#include "Core/Config/GraphicsSettings.h"
#include "Core/Config/MainSettings.h"
#include "Core/Config/NetplaySettings.h"
#include "Core/Movie.h"
#include "Core/System.h"

#include "VideoCommon/AbstractGfx.h"
#include "VideoCommon/AbstractPipeline.h"
#include "VideoCommon/AbstractShader.h"
#include "VideoCommon/AbstractStagingTexture.h"
#include "VideoCommon/FramebufferShaderGen.h"
#include "VideoCommon/NetPlayChatUI.h"
#include "VideoCommon/NetPlayGolfUI.h"
#include "VideoCommon/OnScreenDisplay.h"
#include "VideoCommon/PerformanceMetrics.h"
#include "VideoCommon/Present.h"
#include "VideoCommon/Statistics.h"
#include "VideoCommon/VertexManagerBase.h"
#include "VideoCommon/VideoBackendBase.h"
#include "VideoCommon/VideoConfig.h"

#include <inttypes.h>
#include <mutex>

#include <imgui.h>
#include <imgui_internal.h>
#include <implot.h>

namespace VideoCommon
{

bool OnScreenUI::Initialize(u32 width, u32 height, float scale, void (*imgui_setup)(void*, void*), void* window_handle)
{
  std::unique_lock<std::mutex> imgui_lock(m_imgui_mutex);

  if (!IMGUI_CHECKVERSION())
  {
    PanicAlertFmt("ImGui version check failed");
    return false;
  }
  if (!ImGui::CreateContext())
  {
    PanicAlertFmt("Creating ImGui context failed");
    return false;
  }
  if (!ImPlot::CreateContext())
  {
    PanicAlertFmt("Creating ImPlot context failed");
    return false;
  }

  // Don't create an ini file. TODO: Do we want this in the future?
  ImGui::GetIO().IniFilename = nullptr;
  SetScale(scale);

  PortableVertexDeclaration vdecl = {};
  vdecl.position = {ComponentFormat::Float, 2, offsetof(ImDrawVert, pos), true, false};
  vdecl.texcoords[0] = {ComponentFormat::Float, 2, offsetof(ImDrawVert, uv), true, false};
  vdecl.colors[0] = {ComponentFormat::UByte, 4, offsetof(ImDrawVert, col), true, false};
  vdecl.stride = sizeof(ImDrawVert);
  m_imgui_vertex_format = g_gfx->CreateNativeVertexFormat(vdecl);
  if (!m_imgui_vertex_format)
  {
    PanicAlertFmt("Failed to create ImGui vertex format");
    return false;
  }

  // Font defaults
  ImGuiIO& io = ImGui::GetIO();
  m_imgui_textures.clear();

  // User supplied font
  std::string file = File::GetUserPath(D_LOAD_IDX) + "OSD_Font.ttf";

  bool font_exists = File::Exists(file);
  if (!font_exists)
  {
    // Default supplied font
    file = File::GetSysDirectory() + DIR_SEP + RESOURCES_DIR + DIR_SEP + "OSD_Font.ttf";
    font_exists = File::Exists(file);
  }

  if (font_exists)
  {
    io.Fonts->Clear();
    io.Fonts->AddFontFromFileTTF(file.c_str());
  }

  // Setup new font management behavior
  io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures | ImGuiBackendFlags_RendererHasVtxOffset | ImGuiBackendFlags_RendererHasViewports;
  io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable | ImGuiConfigFlags_DockingEnable | ImGuiConfigFlags_DpiEnableScaleViewports;

  if(imgui_setup)
    imgui_setup(window_handle, this);

  ImGuiPlatformIO& platform_io = ImGui::GetPlatformIO();
  platform_io.Renderer_CreateWindow = [](ImGuiViewport* vp) {
    ImGuiViewport* main_viewport = ImGui::GetMainViewport();
    OnScreenUI* onscreen_ui = reinterpret_cast<OnScreenUI*>(main_viewport->PlatformUserData);
    onscreen_ui->OnScreenUI_CreateWindow(vp);
  };
  platform_io.Renderer_DestroyWindow = [](ImGuiViewport* vp) {
    ImGuiViewport* main_viewport = ImGui::GetMainViewport();
    OnScreenUI* onscreen_ui = reinterpret_cast<OnScreenUI*>(main_viewport->PlatformUserData);
    onscreen_ui->OnScreenUI_DestroyWindow(vp);
  };
  platform_io.Renderer_SetWindowSize = [](ImGuiViewport* vp, ImVec2 size) {
    ImGuiViewport* main_viewport = ImGui::GetMainViewport();
    OnScreenUI* onscreen_ui = reinterpret_cast<OnScreenUI*>(main_viewport->PlatformUserData);
    onscreen_ui->OnScreenUI_SetWindowSize(vp, size);
  };
  platform_io.Renderer_RenderWindow = [](ImGuiViewport* vp, void*) {
    ImGuiViewport* main_viewport = ImGui::GetMainViewport();
    OnScreenUI* onscreen_ui = reinterpret_cast<OnScreenUI*>(main_viewport->PlatformUserData);
    onscreen_ui->OnScreenUI_RenderWindow(vp, nullptr);
  };

  if (!RecompileImGuiPipeline())
    return false;

  m_imgui_last_frame_time = Common::Timer::NowUs();
  m_ready = true;
  BeginImGuiFrameUnlocked(width, height);  // lock is already held

  return true;
}

OnScreenUI::~OnScreenUI()
{
  std::unique_lock<std::mutex> imgui_lock(m_imgui_mutex);

  ImGui::EndFrame();
  ImPlot::DestroyContext();
  ImGui::DestroyPlatformWindows();
  ImGui::DestroyContext();
  m_imgui_textures.clear();
}

bool OnScreenUI::RecompileImGuiPipeline()
{
  if (g_presenter->GetBackbufferFormat() == AbstractTextureFormat::Undefined)
  {
    // No backbuffer (nogui) means no imgui rendering will happen
    // Some backends don't like making pipelines with no render targets
    return true;
  }

  const bool linear_space_output =
      g_presenter->GetBackbufferFormat() == AbstractTextureFormat::RGBA16F;

  std::unique_ptr<AbstractShader> vertex_shader = g_gfx->CreateShaderFromSource(
      ShaderStage::Vertex, FramebufferShaderGen::GenerateImGuiVertexShader(), nullptr,
      "ImGui vertex shader");
  std::unique_ptr<AbstractShader> pixel_shader = g_gfx->CreateShaderFromSource(
      ShaderStage::Pixel, FramebufferShaderGen::GenerateImGuiPixelShader(linear_space_output),
      nullptr, "ImGui pixel shader");
  if (!vertex_shader || !pixel_shader)
  {
    PanicAlertFmt("Failed to compile ImGui shaders");
    return false;
  }

  // GS is used to render the UI to both eyes in stereo modes.
  std::unique_ptr<AbstractShader> geometry_shader;
  if (g_gfx->UseGeometryShaderForUI())
  {
    geometry_shader = g_gfx->CreateShaderFromSource(
        ShaderStage::Geometry, FramebufferShaderGen::GeneratePassthroughGeometryShader(1, 1),
        nullptr, "ImGui passthrough geometry shader");
    if (!geometry_shader)
    {
      PanicAlertFmt("Failed to compile ImGui geometry shader");
      return false;
    }
  }

  AbstractPipelineConfig pconfig = {};
  pconfig.vertex_format = m_imgui_vertex_format.get();
  pconfig.vertex_shader = vertex_shader.get();
  pconfig.geometry_shader = geometry_shader.get();
  pconfig.pixel_shader = pixel_shader.get();
  pconfig.rasterization_state = RenderState::GetNoCullRasterizationState(PrimitiveType::Triangles);
  pconfig.depth_state = RenderState::GetNoDepthTestingDepthState();
  pconfig.blending_state = RenderState::GetNoBlendingBlendState();
  pconfig.blending_state.blend_enable = true;
  pconfig.blending_state.src_factor = SrcBlendFactor::SrcAlpha;
  pconfig.blending_state.dst_factor = DstBlendFactor::InvSrcAlpha;
  pconfig.blending_state.src_factor_alpha = SrcBlendFactor::Zero;
  pconfig.blending_state.dst_factor_alpha = DstBlendFactor::One;
  pconfig.framebuffer_state.color_texture_format = g_presenter->GetBackbufferFormat();
  pconfig.framebuffer_state.depth_texture_format = AbstractTextureFormat::Undefined;
  pconfig.framebuffer_state.samples = 1;
  pconfig.framebuffer_state.per_sample_shading = false;
  pconfig.usage = AbstractPipelineUsage::Utility;
  m_imgui_pipeline = g_gfx->CreatePipeline(pconfig);
  if (!m_imgui_pipeline)
  {
    PanicAlertFmt("Failed to create imgui pipeline");
    return false;
  }

  return true;
}

void OnScreenUI::BeginImGuiFrame(u32 width, u32 height)
{
  std::unique_lock<std::mutex> imgui_lock(m_imgui_mutex);
  BeginImGuiFrameUnlocked(width, height);
}

void OnScreenUI::BeginImGuiFrameUnlocked(u32 width, u32 height)
{
  const u64 current_time_us = Common::Timer::NowUs();
  const u64 time_diff_us = current_time_us - m_imgui_last_frame_time;
  const float time_diff_secs = static_cast<float>(time_diff_us / 1000000.0);
  m_imgui_last_frame_time = current_time_us;

  ImGuiViewport* main_viewport = ImGui::GetMainViewport();
  // Update I/O with window dimensions.
  ImGuiIO& io = ImGui::GetIO();
  io.DisplaySize = main_viewport->Size;
  //float scale = main_viewport->DpiScale > 0.0f ? main_viewport->DpiScale : 1.0f;
  //io.DisplayFramebufferScale = ImVec2{ scale, scale};
  io.DeltaTime = time_diff_secs;

  m_backbuffer_width = width;
  m_backbuffer_height = height;

  ImGui::NewFrame();

  ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
  ImGui::PushStyleColor(ImGuiCol_DockingEmptyBg, ImVec4(0, 0, 0, 0));
  ImGui::DockSpaceOverViewport(ImGuiDockNodeFlags_PassthruCentralNode, ImGui::GetMainViewport());
  ImGui::PopStyleColor(2);
}

void OnScreenUI::DrawImGui(ImDrawData* draw_data)
{
  if (!draw_data)
    return;

  float backbuffer_width = draw_data->DisplaySize.x * draw_data->FramebufferScale.x;
  float backbuffer_height = draw_data->DisplaySize.y * draw_data->FramebufferScale.y;
  ImVec2 display_pos = draw_data->DisplayPos;

  g_gfx->SetViewport(0.0f, 0.0f, backbuffer_width,
                     backbuffer_height, 0.0f, 1.0f);

  // Uniform buffer for draws.
  struct ImGuiUbo
  {
    float u_rcp_viewport_size_mul2[2];
    float u_display_pos[2];
  };
  ImGuiUbo ubo = {{1.0f / draw_data->DisplaySize.x * 2.0f, 1.0f / draw_data->DisplaySize.y * 2.0f}, {display_pos.x, display_pos.y}};

  // Set up common state for drawing.
  g_gfx->SetPipeline(m_imgui_pipeline.get());
  g_gfx->SetSamplerState(0, RenderState::GetPointSamplerState());
  g_vertex_manager->UploadUtilityUniforms(&ubo, sizeof(ubo));

  for (int i = 0; i < draw_data->CmdListsCount; i++)
  {
    const ImDrawList* cmdlist = draw_data->CmdLists[i];
    if (cmdlist->VtxBuffer.empty() || cmdlist->IdxBuffer.empty())
      continue;

    u32 base_vertex, base_index;
    g_vertex_manager->UploadUtilityVertices(cmdlist->VtxBuffer.Data, sizeof(ImDrawVert),
                                            cmdlist->VtxBuffer.Size, cmdlist->IdxBuffer.Data,
                                            cmdlist->IdxBuffer.Size, &base_vertex, &base_index);

    for (const ImDrawCmd& cmd : cmdlist->CmdBuffer)
    {
      if (cmd.UserCallback)
      {
        cmd.UserCallback(cmdlist, &cmd);
        continue;
      }

      g_gfx->SetScissorRect(g_gfx->ConvertFramebufferRectangle(
          MathUtil::Rectangle<int>(
              static_cast<int>((cmd.ClipRect.x - display_pos.x)*draw_data->FramebufferScale.x), static_cast<int>((cmd.ClipRect.y - display_pos.y)*draw_data->FramebufferScale.y),
              static_cast<int>((cmd.ClipRect.z - display_pos.x)*draw_data->FramebufferScale.x), static_cast<int>((cmd.ClipRect.w - display_pos.y))*draw_data->FramebufferScale.y),
          g_gfx->GetCurrentFramebuffer()));
      g_gfx->SetTexture(0, reinterpret_cast<const AbstractTexture*>(cmd.GetTexID()));
      g_gfx->DrawIndexed(base_index, cmd.ElemCount, base_vertex);
      base_index += cmd.ElemCount;
    }
  }

  // Some capture software (such as OBS) hooks SwapBuffers and uses glBlitFramebuffer to copy our
  // back buffer just before swap. Because glBlitFramebuffer honors the scissor test, the capture
  // itself will be clipped to whatever bounds were last set by ImGui, resulting in a rather useless
  // capture whenever any ImGui windows are open. We'll reset the scissor rectangle to the entire
  // viewport here to avoid this problem.
  g_gfx->SetScissorRect(g_gfx->ConvertFramebufferRectangle(
      MathUtil::Rectangle<int>(0, 0, backbuffer_width, backbuffer_height),
      g_gfx->GetCurrentFramebuffer()));
}

// Create On-Screen-Messages
void OnScreenUI::DrawDebugText()
{
  if (Config::Get(Config::MAIN_MOVIE_SHOW_OSD))
  {
    // Position under the FPS display.
    ImGui::SetNextWindowPos(
      ImVec2(ImGui::GetMainViewport()->Pos.x - ImGui::GetFontSize(),
               80.f),
        ImGuiCond_FirstUseEver, ImVec2(1.0f, 0.0f));
    ImGui::SetNextWindowSizeConstraints(ImVec2(5.0f * ImGui::GetFontSize(),
                                               2.1f * ImGui::GetFontSize()),
                                        ImGui::GetIO().DisplaySize);
    if (ImGui::Begin("Movie", nullptr, ImGuiWindowFlags_NoFocusOnAppearing))
    {
      auto& movie = Core::System::GetInstance().GetMovie();
      if (movie.IsPlayingInput())
      {
        ImGui::Text("Frame: %" PRIu64 " / %" PRIu64, movie.GetCurrentFrame(),
                    movie.GetTotalFrames());
        ImGui::Text("Input: %" PRIu64 " / %" PRIu64, movie.GetCurrentInputCount(),
                    movie.GetTotalInputCount());
      }
      else if (Config::Get(Config::MAIN_SHOW_FRAME_COUNT))
      {
        ImGui::Text("Frame: %" PRIu64, movie.GetCurrentFrame());
        if (movie.IsRecordingInput())
          ImGui::Text("Input: %" PRIu64, movie.GetCurrentInputCount());
      }
      if (Config::Get(Config::MAIN_SHOW_LAG))
        ImGui::Text("Lag: %" PRIu64 "\n", movie.GetCurrentLagCount());
      if (Config::Get(Config::MAIN_MOVIE_SHOW_INPUT_DISPLAY))
        ImGui::TextUnformatted(movie.GetInputDisplay().c_str());
      if (Config::Get(Config::MAIN_MOVIE_SHOW_RTC))
        ImGui::TextUnformatted(movie.GetRTCDisplay().c_str());
      if (Config::Get(Config::MAIN_MOVIE_SHOW_RERECORD))
        ImGui::TextUnformatted(movie.GetRerecords().c_str());
    }
    ImGui::End();
  }

  if (g_ActiveConfig.bOverlayStats)
    g_stats.Display();

  if (Config::Get(Config::GFX_SHOW_NETPLAY_MESSAGES) && g_netplay_chat_ui)
    g_netplay_chat_ui->Display();

  if (Config::Get(Config::NETPLAY_GOLF_MODE_OVERLAY) && g_netplay_golf_ui)
    g_netplay_golf_ui->Display();

  if (g_ActiveConfig.bOverlayProjStats)
    g_stats.DisplayProj();

  if (g_ActiveConfig.bOverlayScissorStats)
    g_stats.DisplayScissor();

  const std::string profile_output = Common::Profiler::ToString();
  if (!profile_output.empty())
    ImGui::TextUnformatted(profile_output.c_str());
}

void OnScreenUI::DrawChallengesAndLeaderboards()
{
  if (!Config::Get(Config::MAIN_OSD_MESSAGES))
    return;
#ifdef USE_RETRO_ACHIEVEMENTS
  auto& instance = AchievementManager::GetInstance();
  std::lock_guard lg{instance.GetLock()};
  const bool challenge_indicators_enabled = Config::Get(Config::RA_CHALLENGE_INDICATORS_ENABLED);
  const bool challenges_updated = instance.AreChallengesUpdated();
  const auto& challenges = instance.GetActiveChallenges();

  if (!challenge_indicators_enabled)
  {
    if (challenges_updated)
      instance.ResetChallengesUpdated();
    if (!m_challenge_texture_map.empty())
      m_challenge_texture_map.clear();
  }
  else if (challenges_updated || m_challenge_texture_map.size() != challenges.size())
  {
    if (challenges_updated)
      instance.ResetChallengesUpdated();
    m_challenge_texture_map.clear();
    for (const auto& name : challenges)
    {
      const auto& icon = instance.GetAchievementBadge(name, false);
      const u32 width = icon.width;
      const u32 height = icon.height;
      TextureConfig tex_config(width, height, 1, 1, 1, AbstractTextureFormat::RGBA8, 0,
                               AbstractTextureType::Texture_2DArray);
      auto res = m_challenge_texture_map.insert_or_assign(name, g_gfx->CreateTexture(tex_config));
      res.first->second->Load(0, width, height, width, icon.data.data(),
                              sizeof(u32) * width * height);
    }
  }

  float leaderboard_y = ImGui::GetIO().DisplaySize.y;
  if (challenge_indicators_enabled && !m_challenge_texture_map.empty())
  {
    float scale = ImGui::GetIO().DisplaySize.y / 1024.0;
    ImGui::SetNextWindowSize(ImVec2(0, 0));
    ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x, ImGui::GetIO().DisplaySize.y), 0,
                            ImVec2(1, 1));
    if (ImGui::Begin("Challenges", nullptr,
                     ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoInputs |
                         ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                         ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoNav |
                         ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoFocusOnAppearing))
    {
      for (auto& [name, texture] : m_challenge_texture_map)
      {
        ImGui::Image(*texture.get(), ImVec2(static_cast<float>(texture->GetWidth()) * scale,
                                            static_cast<float>(texture->GetHeight()) * scale));
        ImGui::SameLine();
      }
    }
    leaderboard_y -= ImGui::GetWindowHeight();
    ImGui::End();
  }

  const auto& leaderboard_progress = instance.GetActiveLeaderboards();
  if (Config::Get(Config::RA_LEADERBOARD_TRACKER_ENABLED) && !leaderboard_progress.empty())
  {
    ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x, leaderboard_y), 0,
                            ImVec2(1.0, 1.0));
    ImGui::SetNextWindowSize(ImVec2(0.0f, 0.0f));
    if (ImGui::Begin("Leaderboards", nullptr,
                     ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoInputs |
                         ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                         ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoNav |
                         ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoFocusOnAppearing))
    {
      for (const auto& value : leaderboard_progress)
        ImGui::TextUnformatted(value.c_str());
    }
    ImGui::End();
  }
#endif  // USE_RETRO_ACHIEVEMENTS
}

ImDrawData* OnScreenUI::Finalize()
{
  ImDrawData* main_window_draw_data;
  {
  auto lock = GetImGuiLock();

  auto& perf_metrics = Core::System::GetInstance().GetPerfMetrics();
  perf_metrics.DrawImGuiStats(m_backbuffer_scale);
  DrawDebugText();
  OSD::DrawMessages();
  DrawChallengesAndLeaderboards();
  ImGui::Render();

  main_window_draw_data = ImGui::GetMainViewport()->DrawData;

  // Check for font changes
  ImGuiStyle& style = ImGui::GetStyle();
  const int size = Config::Get(Config::MAIN_OSD_FONT_SIZE);
  if (size != style.FontSizeBase)
    style.FontSizeBase = static_cast<float>(size);

  // Create or update fonts.
  for (ImTextureData* tex : ImGui::GetPlatformIO().Textures)
  {
      if (tex->Status != ImTextureStatus_OK)
      {
          UpdateImguiTexture(tex);
      }
  }
  }

  ImGuiIO& io = ImGui::GetIO();
  if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
  {
      // Synchronizes window state and triggers Platform_* callbacks
      ImGui::UpdatePlatformWindows();
  }

  return main_window_draw_data;
}

void OnScreenUI::UpdateImguiTexture(ImTextureData* tex)
{
  if (tex->Status == ImTextureStatus_WantCreate)
  {
    // Create new font texture.
    IM_ASSERT(tex->TexID == ImTextureID_Invalid);
    IM_ASSERT(tex->Format == ImTextureFormat_RGBA32);

    TextureConfig font_tex_config(tex->Width, tex->Height, 1, 1, 1, AbstractTextureFormat::RGBA8, 0,
                                  AbstractTextureType::Texture_2DArray);
    std::unique_ptr<AbstractTexture> font_tex =
        g_gfx->CreateTexture(font_tex_config, "ImGui font texture");

    if (!font_tex)
    {
      PanicAlertFmt("Failed to create ImGui texture");
      return;
    }

    font_tex->Load(0, tex->Width, tex->Height, tex->Width, tex->Pixels,
                   sizeof(u32) * tex->Width * tex->Height);

    tex->SetTexID(static_cast<ImTextureID>(*font_tex.get()));
    // Keeps the texture alive.
    m_imgui_textures.push_back(std::move(font_tex));
    tex->SetStatus(ImTextureStatus_OK);
  }
  else if (tex->Status == ImTextureStatus_WantUpdates)
  {
    AbstractTexture* font_tex = reinterpret_cast<AbstractTexture*>(tex->GetTexID());

    if (!font_tex || tex->TexID == ImTextureID_Invalid)
    {
      PanicAlertFmt("ImGui texture not created before update");
      return;
    }

    for (const ImTextureRect& r : tex->Updates)
    {
      // Rect of texture that will be updated.
      const int x_offset = static_cast<int>(r.x);
      const int y_offset = static_cast<int>(r.y);
      const int width = static_cast<int>(r.w);
      const int height = static_cast<int>(r.h);

      // Create a staging texture to update the font texture with.
      TextureConfig font_tex_config(width, height, 1, 1, 1, AbstractTextureFormat::RGBA8, 0,
                                    AbstractTextureType::Texture_2DArray);
      std::unique_ptr<AbstractStagingTexture> stage =
          g_gfx->CreateStagingTexture(StagingTextureType::Upload, font_tex_config);

      const int src_pitch = width * tex->BytesPerPixel;

      // Write to staging texture.
      for (int y = 0; y < height; y++)
      {
        const MathUtil::Rectangle<int> rect_line = {0, y, width, y + 1};
        stage->WriteTexels(rect_line, tex->GetPixelsAt(x_offset, y_offset + y), src_pitch);
      }

      // Copy to font texture.
      const MathUtil::Rectangle<int> rect_staging = {0, 0, width, height};
      const MathUtil::Rectangle<int> rect_target = {x_offset, y_offset, width + x_offset,
                                                    height + y_offset};

      stage->CopyToTexture(rect_staging, font_tex, rect_target, 0, 0);
    }

    tex->SetStatus(ImTextureStatus_OK);
  }
  else if (tex->Status == ImTextureStatus_WantDestroy && tex->UnusedFrames > 0)
  {
    AbstractTexture* font_tex = reinterpret_cast<AbstractTexture*>(tex->GetTexID());

    tex->SetTexID(ImTextureID_Invalid);

    m_imgui_textures.erase(
        std::find_if(m_imgui_textures.begin(), m_imgui_textures.end(),
                     [font_tex](auto& element) { return element.get() == font_tex; }));

    tex->Status = ImTextureStatus_Destroyed;
  }
}

std::unique_lock<std::mutex> OnScreenUI::GetImGuiLock()
{
  return std::unique_lock<std::mutex>(m_imgui_mutex);
}

void OnScreenUI::SetScale(float backbuffer_scale)
{
  ImGui::GetIO().DisplayFramebufferScale.x = backbuffer_scale;
  ImGui::GetIO().DisplayFramebufferScale.y = backbuffer_scale;

  // ScaleAllSizes scales in-place, so calling it twice will double-apply the scale
  // Reset the style first so that the scale is applied to the base style, not an already-scaled one
  ImGuiStyle& style = ImGui::GetStyle();
  style = {};
  style.FontScaleMain = backbuffer_scale;
  style.WindowRounding = 7.0f;
  style.ScaleAllSizes(backbuffer_scale);

  m_backbuffer_scale = backbuffer_scale;
}
void OnScreenUI::SetKeyMap(const DolphinKeyMap& key_map)
{
  static constexpr DolphinKeyMap dolphin_to_imgui_map = {
      ImGuiKey_Tab,       ImGuiKey_LeftArrow, ImGuiKey_RightArrow, ImGuiKey_UpArrow,
      ImGuiKey_DownArrow, ImGuiKey_PageUp,    ImGuiKey_PageDown,   ImGuiKey_Home,
      ImGuiKey_End,       ImGuiKey_Insert,    ImGuiKey_Delete,     ImGuiKey_Backspace,
      ImGuiKey_Space,     ImGuiKey_Enter,     ImGuiKey_Escape,     ImGuiKey_KeypadEnter,
      ImGuiKey_A,         ImGuiKey_C,         ImGuiKey_V,          ImGuiKey_X,
      ImGuiKey_Y,         ImGuiKey_Z,
  };

  auto lock = GetImGuiLock();

  if (!ImGui::GetCurrentContext())
    return;

  m_dolphin_to_imgui_map.clear();
  for (int dolphin_key = 0; dolphin_key <= static_cast<int>(DolphinKey::Z); dolphin_key++)
  {
    const int imgui_key = dolphin_to_imgui_map[DolphinKey(dolphin_key)];
    if (imgui_key >= 0)
    {
      const int mapped_key = key_map[DolphinKey(dolphin_key)];
      m_dolphin_to_imgui_map[mapped_key & 0x1FF] = imgui_key;
    }
  }
}

void OnScreenUI::SetKey(u32 key, bool is_down, const char* chars)
{
  auto lock = GetImGuiLock();
  if (auto iter = m_dolphin_to_imgui_map.find(key); iter != m_dolphin_to_imgui_map.end())
    ImGui::GetIO().AddKeyEvent((ImGuiKey)iter->second, is_down);

  if (chars)
    ImGui::GetIO().AddInputCharactersUTF8(chars);
}

void OnScreenUI::SetMousePos(float x, float y)
{
  auto lock = GetImGuiLock();

  ImGui::GetIO().AddMousePosEvent(x, y);
}

void OnScreenUI::SetMousePress(u32 button_mask)
{
  auto lock = GetImGuiLock();

  for (size_t i = 0; i < std::size(ImGui::GetIO().MouseDown); i++)
  {
    ImGui::GetIO().AddMouseButtonEvent(static_cast<int>(i), (button_mask & (1u << i)) != 0);
  }
}

void OnScreenUI::UpdateMainViewportPos(float x, float y)
{
  auto lock = GetImGuiLock();

  ImGuiViewport* main_viewport = ImGui::GetMainViewport();

  main_viewport->Pos = ImVec2{x, y};
  main_viewport->PlatformRequestMove = true;
}

void OnScreenUI::UpdateMainViewportSize(u32 width, u32 height, float scale)
{
  auto lock = GetImGuiLock();

  ImGuiViewport* main_viewport = ImGui::GetMainViewport();

  main_viewport->Size = ImVec2{static_cast<float>(width), static_cast<float>(height)};
  main_viewport->DpiScale = scale;
  main_viewport->PlatformRequestResize = true;
}

void OnScreenUI::OnScreenUI_CreateWindow(ImGuiViewport *vp)
{
  auto* data = new OnScreenUI_ViewportData();
  vp->RendererUserData = data;

  const float scale = vp->DpiScale > 0.0f ? vp->DpiScale : 1.0f;
  const u32 width = vp->Size.x * scale;
  const u32 height = vp->Size.y * scale;

  data->swapchain = g_video_backend->CreateSwapChain(vp->PlatformHandleRaw, width, height);
}

void OnScreenUI::OnScreenUI_DestroyWindow(ImGuiViewport *vp)
{
  if(auto* data = reinterpret_cast<OnScreenUI_ViewportData*>(vp->RendererUserData))
  {
    delete data;
    vp->RendererUserData = nullptr;
  }
}

void OnScreenUI::OnScreenUI_SetWindowSize(ImGuiViewport *vp, ImVec2 size)
{
  if(auto* data = reinterpret_cast<OnScreenUI_ViewportData*>(vp->RendererUserData)) {
    const float scale = vp->DpiScale > 0.0f ? vp->DpiScale : 1.0f;
    data->swapchain->Resize(size.x*scale, size.y*scale, scale);
  }
}

void OnScreenUI::OnScreenUI_RenderWindow(ImGuiViewport *vp, void *)
{
  if(auto* data = reinterpret_cast<OnScreenUI_ViewportData*>(vp->RendererUserData))
  {
    if(data->swapchain->BindBackBuffer())
    {
      this->DrawImGui(vp->DrawData);
      data->swapchain->Present();
      g_gfx->Flush();
    }
  }
}

}  // namespace VideoCommon
