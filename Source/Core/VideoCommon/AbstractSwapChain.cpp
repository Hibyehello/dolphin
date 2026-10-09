#include "AbstractSwapChain.h"

AbstractFramebuffer* AbstractSwapChain::GetFrameBuffer() const {
  return m_framebuffer.get();
}
