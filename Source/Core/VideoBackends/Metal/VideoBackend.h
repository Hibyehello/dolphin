// Copyright 2022 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <memory>
#include <string>
#include "VideoCommon/VideoBackendBase.h"

namespace Metal
{
class VideoBackend : public VideoBackendBase
{
public:
  bool Initialize(const WindowSystemInfo& wsi) override;
  void Shutdown() override;

  std::string GetConfigName() const override;
  std::string GetDisplayName() const override;
  std::optional<std::string> GetWarningMessage() const override;
  std::unique_ptr<AbstractSwapChain> CreateSwapChain(void* window_handle, int width, int height) override;
  bool SupportsViewports() override;

  void InitBackendInfo(const WindowSystemInfo& wsi) override;

  void PrepareWindow(WindowSystemInfo& wsi) override;

  static constexpr const char* CONFIG_NAME = "Metal";
};
}  // namespace Metal
