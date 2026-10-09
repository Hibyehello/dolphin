#include "MTLSwapChain.h"
#include "MTLTexture.h"
#include "MTLStateTracker.h"
#include "VideoCommon/TextureConfig.h"
#include "VideoBackends/Metal/MTLObjectCache.h"
#import <Metal/Metal.h>
#import <QuartzCore/QuartzCore.h>

namespace Metal
{
MTLSwapChain::MTLSwapChain(void* window_handle, u32 width, u32 height)
{
  id handle = (__bridge id)window_handle;

  if ([handle isKindOfClass:[CAMetalLayer class]])
  {
    m_layer = handle;
  }
  else if ([handle isKindOfClass:[NSView class]])
  {
    dispatch_async(dispatch_get_main_queue(), ^{
    NSView* view = handle;
    [view setWantsLayer:YES];
    [view setLayerContentsRedrawPolicy:NSViewLayerContentsRedrawNever];

    m_layer = [CAMetalLayer layer];
    [m_layer setDevice:g_device];
    [m_layer setOpaque:YES];
    [m_layer setFrame:view.bounds];

    [view.layer addSublayer:m_layer];
    });
  }

  TextureConfig config(height, width, 1, 1, 1, AbstractTextureFormat::BGRA8, AbstractTextureFlag_RenderTarget, AbstractTextureType::Texture_2DArray);

  [m_layer setDrawableSize:CGSizeMake(width, height)];
  m_backbuffer = std::make_unique<Metal::Texture>(nullptr, config);
  m_framebuffer = std::make_unique<Metal::Framebuffer>(m_backbuffer.get(), nullptr, std::vector<AbstractTexture*>{}, width, height, 1, 1);
}

void MTLSwapChain::Resize(u32 width, u32 height, float scale)
{
  if (width == 0 || height == 0)
    return;

  const float logical_width = static_cast<float>(width) / scale;
  const float logical_height = static_cast<float>(height) / scale;
  [m_layer setFrame:CGRectMake(0, 0, logical_width, logical_height)];

  [m_layer setContentsScale:scale];
  [m_layer setDrawableSize:CGSizeMake(width, height)];
}

bool MTLSwapChain::BindBackBuffer(const ClearColor clear_color)
{
  @autoreleasepool
  {
    m_drawable = MRCRetain([m_layer nextDrawable]);
    if (!m_drawable)
      return false;

    m_framebuffer->UpdateBackbufferTexture([m_drawable texture]);
    g_gfx->SetAndClearFramebuffer(m_framebuffer.get(), clear_color);

    return true;
  }
}

void MTLSwapChain::Present()
{
  if (!m_drawable)
    return;

  @autoreleasepool
  {
    id<MTLCommandBuffer> cmd_buffer = g_state_tracker->GetRenderCmdBuf();
    if (cmd_buffer)
    {
      [cmd_buffer presentDrawable:m_drawable];
    }
    else
    {
      [m_drawable present];
    }
  m_drawable = nullptr;
}
}
}
