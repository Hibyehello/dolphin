#pragma once

#include "VideoBackends/Metal/MRCHelpers.h"
#include "VideoBackends/Metal/MTLTexture.h"
#include "VideoCommon/AbstractFramebuffer.h"
#include "VideoCommon/AbstractGfx.h"
#include "VideoCommon/AbstractSwapChain.h"
#include "VideoCommon/AbstractTexture.h"
#include <memory>

#import <AppKit/AppKit.h>
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>

namespace Metal
{
class MTLSwapChain : public ::AbstractSwapChain
{
public:
  MTLSwapChain(void* window_handle, u32 width, u32 height);
  ~MTLSwapChain() override = default;

  void Resize(u32 width, u32 height, float scale=1.0f) override;
  bool BindBackBuffer(const ClearColor clear_color) override;
  void Present() override;

  CAMetalLayer* getLayer() const { return m_layer; }
private:
  CAMetalLayer* m_layer = nullptr;
  MRCOwned<id<CAMetalDrawable>> m_drawable = nullptr;

  std::unique_ptr<Metal::Texture> m_backbuffer;
  std::unique_ptr<Metal::Framebuffer> m_framebuffer;
};

}
