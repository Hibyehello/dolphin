#pragma once

#include <memory>
#include <Common/CommonTypes.h>
#include "VideoCommon/AbstractGfx.h"
#include "VideoCommon/AbstractFramebuffer.h"

class AbstractSwapChain
{
public:
  virtual ~AbstractSwapChain() = default;

  AbstractFramebuffer* GetFrameBuffer() const;

  virtual void Resize(u32 width, u32 height, float scale=1.0f) = 0;
  virtual bool BindBackBuffer(const ClearColor clear_color = {}) = 0;
  virtual void Present() = 0;
protected:
  std::unique_ptr<AbstractFramebuffer> m_framebuffer;
};
