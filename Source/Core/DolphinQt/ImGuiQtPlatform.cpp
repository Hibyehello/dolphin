#include "ImguiQtPlatform.h"

#include <QApplication>
#include <QEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QWindow>
#include <QWidget>
#include <QScreen>
#include <QtCore/qcoreevent.h>
#include <QtCore/qnamespace.h>
#include <imgui.h>

#include "Core/Core.h"
#include "Core/System.h"
#include "VideoCommon/Present.h"
#include "Common/Logging/Log.h"

class ImGuiEventFilter : public QObject
{
public:
  explicit ImGuiEventFilter(QObject* parent = nullptr) : QObject(parent) {}

protected:
  bool eventFilter(QObject* watched, QEvent* event) override
  {
    if (!Core::IsRunning(Core::System::GetInstance()))
      return false;

    switch(event->type())
    {
      case QEvent::KeyPress:
      case QEvent::KeyRelease:
      {
        // As the imgui KeysDown array is only 512 elements wide, and some Qt keys which
        // we need to track (e.g. alt) are above this value, we mask the lower 9 bits.
        // Even masked, the key codes are still unique, so conflicts aren't an issue.
        // The actual text input goes through AddInputCharactersUTF8().
        const QKeyEvent* key_event = static_cast<const QKeyEvent*>(event);
        const bool is_down = event->type() == QEvent::KeyPress;
        const u32 key = static_cast<u32>(key_event->key() & 0x1FF);

        const char* chars = nullptr;
        QByteArray utf8;

        if (is_down)
        {
          utf8 = key_event->text().toUtf8();

          if (utf8.size())
            chars = utf8.constData();
        }

        // Pass the key onto Presenter (for the imgui UI)
        g_presenter->SetKey(key, is_down, chars);
      }
      break;
      case QEvent::MouseMove:
      {
        // Qt multiplies all coordinates by the scaling factor in highdpi mode, giving us "scaled" mouse
        // coordinates (as if the screen was standard dpi). We need to update the mouse position in
        // native coordinates, as the UI (and game) is rendered at native resolution.
        float x = static_cast<const QMouseEvent*>(event)->globalPosition().x();
        float y = static_cast<const QMouseEvent*>(event)->globalPosition().y();
        g_presenter->SetMousePos(x, y);
      }
      break;
      case QEvent::MouseButtonPress:
      case QEvent::MouseButtonRelease:
      {
        const u32 button_mask = static_cast<u32>(static_cast<const QMouseEvent*>(event)->buttons());
        g_presenter->SetMousePress(button_mask);
      }
      break;
      default:
    }
    return QObject::eventFilter(watched, event);
  }
};

static ImGuiEventFilter* s_event_filter;

void SetupImguiViewport(void* parent_window, void* onscreen_ui)
{
  ImGuiPlatformIO& platform_io = ImGui::GetPlatformIO();
  platform_io.Platform_CreateWindow = ImguiQt_CreateWindow;
  platform_io.Platform_DestroyWindow = ImguiQt_DestroyWindow;
  platform_io.Platform_ShowWindow = ImguiQt_ShowWindow;
  platform_io.Platform_SetWindowPos = ImguiQt_SetWindowPos;
  platform_io.Platform_GetWindowPos = ImguiQt_GetWindowPos;
  platform_io.Platform_SetWindowSize = ImguiQt_SetWindowSize;
  platform_io.Platform_GetWindowSize = ImguiQt_GetWindowSize;
  platform_io.Platform_SetWindowFocus = ImguiQt_SetWindowFocus;
  platform_io.Platform_GetWindowFocus = ImguiQt_GetWindowFocus;
  platform_io.Platform_GetWindowMinimized = ImguiQt_GetWindowMinimized;
  platform_io.Platform_SetWindowTitle = ImguiQt_SetWindowTitle;
  platform_io.Platform_GetWindowDpiScale = ImguiQt_GetWindowDpiScale;

  ImGuiIO& io = ImGui::GetIO();
  io.BackendFlags |= ImGuiBackendFlags_PlatformHasViewports;

  QMetaObject::invokeMethod(qApp, [parent_window, onscreen_ui]() {
    ImGuiViewport* main_viewport = ImGui::GetMainViewport();
    main_viewport->PlatformHandleRaw = parent_window;
    QWindow* parent_wrapper = QWindow::fromWinId(reinterpret_cast<WId>(main_viewport->PlatformHandleRaw));
    main_viewport->PlatformHandle = parent_wrapper;
    main_viewport->PlatformUserData = onscreen_ui;

    QPoint global_top_left = parent_wrapper->position();
    main_viewport->Pos = ImVec2(static_cast<float>(global_top_left.x()), static_cast<float>(global_top_left.y()));
    main_viewport->Size = ImVec2(static_cast<float>(parent_wrapper->width()), static_cast<float>(parent_wrapper->height()));

    ImGuiPlatformIO& platform_io = ImGui::GetPlatformIO();
      platform_io.Monitors.resize(0);

      const auto screens = QGuiApplication::screens();
      for (QScreen* screen : screens)
      {
          ImGuiPlatformMonitor monitor;

          QRect geom = screen->geometry();
          monitor.MainPos = ImVec2(static_cast<float>(geom.x()), static_cast<float>(geom.y()));
          monitor.MainSize = ImVec2(static_cast<float>(geom.width()), static_cast<float>(geom.height()));

          QRect work_geom = screen->availableGeometry();
          monitor.WorkPos = ImVec2(static_cast<float>(work_geom.x()), static_cast<float>(work_geom.y()));
          monitor.WorkSize = ImVec2(static_cast<float>(work_geom.width()), static_cast<float>(work_geom.height()));
          monitor.DpiScale = static_cast<float>(screen->devicePixelRatio());
          monitor.PlatformHandle = static_cast<void*>(screen);

          platform_io.Monitors.push_back(monitor);
        }

        s_event_filter = new ImGuiEventFilter(qApp);
  }, Qt::BlockingQueuedConnection);
}

void ImguiQt_CreateWindow(ImGuiViewport* vp)
{
  QMetaObject::invokeMethod(qApp, [vp]() {
    QWindow* window = new QWindow();
    window->installEventFilter(s_event_filter);
    window->resize(vp->Size.x, vp->Size.y);
    window->setPosition(vp->Pos.x, vp->Pos.y);
    window->setSurfaceType(QSurface::MetalSurface);
    vp->PlatformHandle = reinterpret_cast<void*>(window);
    window->setFlags(Qt::Tool | Qt::FramelessWindowHint);

    const QSize size = QSize(vp->Size.x, vp->Size.y);
    window->resize(size);
    window->setMinimumSize(size);
    window->setMaximumSize(size);

    //ImGuiViewport* main_viewport = ImGui::GetMainViewport();

    // if(main_viewport && main_viewport->PlatformHandle)
    // {
    //   window->setTransientParent(reinterpret_cast<QWindow*>(main_viewport->PlatformHandle));
    // }

    vp->PlatformHandleRaw = reinterpret_cast<void*>(window->winId());
  }, Qt::BlockingQueuedConnection);
}

void ImguiQt_DestroyWindow(ImGuiViewport *vp)
{
    QWindow* window = static_cast<QWindow*>(vp->PlatformHandle);
    vp->PlatformHandleRaw = nullptr;
    vp->PlatformHandle = nullptr;
    vp->PlatformUserData = nullptr;

    if(window)
    {
      QMetaObject::invokeMethod(qApp, [window]
      {
        window->hide();
        window->deleteLater();
      }, Qt::QueuedConnection);
    }
}

void ImguiQt_ShowWindow(ImGuiViewport *vp)
{
  if(QWindow* window = static_cast<QWindow*>(vp->PlatformHandle))
    QMetaObject::invokeMethod(qApp, [window]
    {
      window->show();
    }, Qt::BlockingQueuedConnection);
}

void ImguiQt_SetWindowPos(ImGuiViewport *vp, ImVec2 pos)
{
  if(QWindow* window = static_cast<QWindow*>(vp->PlatformHandle))
  {
    QMetaObject::invokeMethod(qApp, [window, pos]
    {
      window->setPosition(pos.x, pos.y);

    }, Qt::BlockingQueuedConnection);
  }
}

ImVec2 ImguiQt_GetWindowPos(ImGuiViewport *vp)
{
  if(QWindow* window = static_cast<QWindow*>(vp->PlatformHandle))
  {
    float x = window->position().x();
    float y = window->position().y();
    return {x, y};
  }

  return {0,0};
}

void ImguiQt_SetWindowSize(ImGuiViewport *vp, ImVec2 size)
{
  if(QWindow* window = static_cast<QWindow*>(vp->PlatformHandle))
  {
    QMetaObject::invokeMethod(qApp, [window, size]
    {
      const QSize qsize = QSize(size.x, size.y);
      window->resize(qsize);
      window->setMinimumSize(qsize);
      window->setMaximumSize(qsize);
    }, Qt::BlockingQueuedConnection);
  }
}

ImVec2 ImguiQt_GetWindowSize(ImGuiViewport *vp)
{
  if(QWindow* window = static_cast<QWindow*>(vp->PlatformHandle))
  {
    float width = window->width();
    float height = window->height();
    return {width, height};
  }

  return {0, 0};
}

void ImguiQt_SetWindowFocus(ImGuiViewport *vp)
{
  if(QWindow* window = static_cast<QWindow*>(vp->PlatformHandle))
  {
    QMetaObject::invokeMethod(qApp, [window]
    {
      window->raise();
      window->requestActivate();
    }, Qt::BlockingQueuedConnection);
  }
}

bool ImguiQt_GetWindowFocus(ImGuiViewport *vp)
{
  if(QWindow* window = static_cast<QWindow*>(vp->PlatformHandle))
  {
    return window->isActive();
  }

  return false;
}

bool ImguiQt_GetWindowMinimized(ImGuiViewport *vp)
{
  if(QWindow* window = static_cast<QWindow*>(vp->PlatformHandle))
  {
    return window->windowState() & Qt::WindowMinimized;
  }

  return false;
}

void ImguiQt_SetWindowTitle(ImGuiViewport *vp, const char *str)
{
  if(QWindow* window = static_cast<QWindow*>(vp->PlatformHandle))
  {
    QMetaObject::invokeMethod(qApp, [window, str]
    {
      window->setTitle(QString::fromUtf8(str));
    }, Qt::BlockingQueuedConnection);
  }
}

float ImguiQt_GetWindowDpiScale(ImGuiViewport *vp)
{
  if(QWindow* window = static_cast<QWindow*>(vp->PlatformHandle))
  {
    return window->devicePixelRatio();
  }

  return 1.0f;
}
