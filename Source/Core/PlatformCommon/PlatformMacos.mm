// Copyright 2023 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "Common/GL/GLInterface/AGL.h"
#include "PlatformCommon/Platform.h"

#include "Common/MsgHandler.h"
#include "Core/Config/MainSettings.h"
#include "Core/Core.h"
#include "Core/State.h"
#include "Core/System.h"
#include "VideoCommon/EFBInterface.h"
#include "VideoCommon/Present.h"
#include "imgui.h"

#include <AppKit/AppKit.h>
#include <Carbon/Carbon.h>
#include <Foundation/Foundation.h>
#include <array>
#include <chrono>
#include <climits>
#include <cstdio>
#include <cstring>
#include <thread>

struct ImGui_MacOS_Data;

@interface Application : NSApplication
@property Platform* platform;
- (void)shutdown;
- (void)togglePause;
- (void)saveScreenShot;
- (void)loadLastSaved;
- (void)undoLoadState;
- (void)undoSaveState;
- (void)loadState:(id)sender;
- (void)saveState:(id)sender;
@end

@implementation Application
- (void)shutdown;
{
  [self platform]->RequestShutdown();
  [self stop:nil];
}

- (void)togglePause
{
  auto& system = Core::System::GetInstance();
  if (Core::GetState(system) == Core::State::Running)
    Core::SetState(system, Core::State::Paused);
  else
    Core::SetState(system, Core::State::Running);
}

- (void)saveScreenShot
{
  Core::SaveScreenShot();
}

- (void)loadLastSaved
{
  State::LoadLastSaved(Core::System::GetInstance());
}

- (void)undoLoadState
{
  State::UndoLoadState(Core::System::GetInstance());
}

- (void)undoSaveState
{
  State::UndoSaveState(Core::System::GetInstance());
}

- (void)loadState:(id)sender
{
  State::Load(Core::System::GetInstance(), [sender tag]);
}

- (void)saveState:(id)sender
{
  State::Save(Core::System::GetInstance(), [sender tag]);
}
@end

@interface AppDelegate : NSObject <NSApplicationDelegate>

@property(readonly) Platform* platform;

- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication*)sender;
- (id)initWithPlatform:(Platform*)platform;
@end

@implementation AppDelegate
- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication*)sender
{
  return YES;
}

- (id)initWithPlatform:(Platform*)platform
{
  self = [super init];
  if (self)
  {
    _platform = platform;
  }
  return self;
}
@end

@interface WindowDelegate : NSObject <NSWindowDelegate>

- (void)windowDidResize:(NSNotification*)notification;
@end

@implementation WindowDelegate

- (void)windowDidResize:(NSNotification*)notification
{
  if (g_presenter)
    g_presenter->ResizeSurface();
}
@end

namespace
{
class PlatformMacOS final : public Platform
{
public:
  ~PlatformMacOS() override;

  bool Init() override;
  void SetTitle(const std::string& title) override;
  void MainLoop() override;

  WindowSystemInfo GetWindowSystemInfo() const override;

// Imgui Platform Functions
  bool ImGuiPlatformInit(void* view = nullptr) override;
  void InitMonitors(ImGuiPlatformIO& platform_io) override;
  void RegisterMainViewport() override;
  void CreateWindow(ImGuiViewport *vp) override;
  void DestroyWindow(ImGuiViewport* vp) override;
  ImVec2 GetWindowPos(ImGuiViewport* vp) override;
  void SetWindowPos(ImGuiViewport* vp, ImVec2 size) override;
  ImVec2 GetWindowSize(ImGuiViewport* vp) override;
  void SetWindowSize(ImGuiViewport* vp, ImVec2 size) override;
  void SetImGuiWindowTitle(ImGuiViewport* vp, const char* str) override;
  void ShowWindow(ImGuiViewport* vp) override;

private:
  void ProcessEvents();
  void UpdateWindowPosition();
  void HandleSaveStates(NSUInteger key, NSUInteger flags);
  void SetupMenu();
  bool PassEventToPresenter(NSEvent* event);

  NSRect m_window_rect;
  NSWindow* m_window;
  NSMenu* menuBar;
  AppDelegate* m_app_delegate;
  WindowDelegate* m_window_delegate;

  int m_window_x = Config::Get(Config::MAIN_RENDER_WINDOW_XPOS);
  int m_window_y = Config::Get(Config::MAIN_RENDER_WINDOW_YPOS);
  unsigned int m_window_width = Config::Get(Config::MAIN_RENDER_WINDOW_WIDTH);
  unsigned int m_window_height = Config::Get(Config::MAIN_RENDER_WINDOW_HEIGHT);
  bool m_window_fullscreen = Config::Get(Config::MAIN_FULLSCREEN);
  bool m_key_map_set = false;
};

PlatformMacOS::~PlatformMacOS()
{
  [m_window close];
}

bool PlatformMacOS::Init()
{
  [Application sharedApplication];

  m_app_delegate = [[AppDelegate alloc] initWithPlatform:this];
  [NSApp setDelegate:m_app_delegate];

  [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
  [NSApp setPlatform:this];
  [Application.sharedApplication finishLaunching];

  unsigned long styleMask =
      NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskResizable;

  m_window_rect = CGRectMake(m_window_x, m_window_y, m_window_width, m_window_height);
  m_window = [NSWindow alloc];
  m_window = [m_window initWithContentRect:m_window_rect
                                 styleMask:styleMask
                                   backing:NSBackingStoreBuffered
                                     defer:NO];
  m_window_delegate = [[WindowDelegate alloc] init];
  [m_window setDelegate:m_window_delegate];

  NSNotificationCenter* c = [NSNotificationCenter defaultCenter];
  [c addObserver:NSApp
        selector:@selector(shutdown)
            name:NSWindowWillCloseNotification
          object:m_window];

  if (m_window == nil)
  {
    NSLog(@"Window is %@\n", m_window);
    return false;
  }

  if (Config::Get(Config::MAIN_SHOW_CURSOR) == Config::ShowCursor::Never)
    [NSCursor hide];

  if (Config::Get(Config::MAIN_FULLSCREEN))
  {
    m_window_fullscreen = true;
    [m_window toggleFullScreen:m_window];
  }

  [m_window makeKeyAndOrderFront:NSApp];
  [m_window makeMainWindow];
  [NSApp activateIgnoringOtherApps:YES];
  [m_window setTitle:@"Dolphin-emu-nogui"];

  SetupMenu();

  return true;
}

void PlatformMacOS::SetTitle(const std::string& title)
{
  @autoreleasepool
  {
    NSWindow* window = m_window;
    NSString* str = [NSString stringWithUTF8String:title.c_str()];
    dispatch_async(dispatch_get_main_queue(), ^{
      [window setTitle:str];
    });
  }
}

void PlatformMacOS::MainLoop()
{

  while (IsRunning())
  {
    UpdateRunningFlag();
    Core::HostDispatchJobs(Core::System::GetInstance());
    ProcessEvents();
    UpdateWindowPosition();
  }
}

WindowSystemInfo PlatformMacOS::GetWindowSystemInfo() const
{
  @autoreleasepool
  {
    WindowSystemInfo wsi;
    wsi.type = WindowSystemType::MacOS;
    wsi.render_window = (void*)CFBridgingRetain([m_window contentView]);
    wsi.render_surface = wsi.render_window;
    return wsi;
  }
}

void PlatformMacOS::ProcessEvents()
{
  @autoreleasepool
  {
    NSDate* expiration = [NSDate dateWithTimeIntervalSinceNow:1];
    NSEvent* event = [NSApp nextEventMatchingMask:NSEventMaskAny
                                        untilDate:expiration
                                           inMode:NSDefaultRunLoopMode
                                          dequeue:YES];

    if(!PassEventToPresenter(event))
      [NSApp sendEvent:event];

    // Need to update if m_window becomes fullscreen
    m_window_fullscreen = [m_window styleMask] & NSWindowStyleMaskFullScreen;

    if ([m_window isMainWindow])
    {
      m_window_focus = true;
      if (Config::Get(Config::MAIN_SHOW_CURSOR) == Config::ShowCursor::Never &&
          Core::GetState(Core::System::GetInstance()) != Core::State::Paused)
      {
        [NSCursor unhide];
      }
    }
    else
    {
      m_window_focus = false;
      if (Config::Get(Config::MAIN_SHOW_CURSOR) == Config::ShowCursor::Never)
        [NSCursor hide];
    }
  }
}

bool PlatformMacOS::PassEventToPresenter(NSEvent* event)
{
  if(!g_presenter) return false;

  if(!m_key_map_set)
  {
    static constexpr DolphinKeyMap key_map = {
      kVK_Tab, kVK_LeftArrow, kVK_RightArrow, kVK_UpArrow, kVK_DownArrow,
      kVK_PageUp, kVK_PageDown, kVK_Home, kVK_End, kVK_Help,
      kVK_ForwardDelete, kVK_Delete, kVK_Space, kVK_Return, kVK_Escape,
      kVK_ANSI_KeypadEnter,  // Keypad enter
      kVK_ANSI_A, kVK_ANSI_C, kVK_ANSI_V, kVK_ANSI_X, kVK_ANSI_Y,
      kVK_ANSI_Z,
    };

    g_presenter->SetKeyMap(key_map);
    m_key_map_set = true;
  }

  int width = [[NSScreen mainScreen] frame].size.width;
  int height = [[NSScreen mainScreen] frame].size.height;
  double contentHeight = [m_window contentRectForFrameRect:[m_window frame]].size.height;
  switch(event.type)
  {
    case NSEventTypeKeyDown:
    case NSEventTypeKeyUp:
    {
      const bool is_down = event.type == NSEventTypeKeyDown;
      const char* chars = nullptr;

      if (is_down)
      {
        chars = [event.characters UTF8String];
      }
      return g_presenter->SetKey((u32)event.keyCode, is_down, chars);
    }
    break;
    case NSEventTypeLeftMouseDragged:
    case NSEventTypeRightMouseDragged:
    case NSEventTypeMouseMoved:
    {
      CGPoint local_point = [[m_window contentView] convertPoint:[event locationInWindow] fromView:nil];

      local_point.y = (float)contentHeight - local_point.y; //Need to flip vertically
      g_presenter->SetMousePos(local_point.x, local_point.y);
    }
    break;
    case NSEventTypeLeftMouseDown:
    case NSEventTypeRightMouseDown:
      g_presenter->SetMousePress(event.buttonNumber+1);
    break;
    case NSEventTypeLeftMouseUp:
    case NSEventTypeRightMouseUp:
      g_presenter->SetMousePress(0);
    break;
    case NSEventTypeScrollWheel:
      g_presenter->SetMouseScroll(event.deltaX, event.deltaY);
    break;
    default: 
      return false;
  }
  return false;
}

void PlatformMacOS::UpdateWindowPosition()
{
  if (m_window_fullscreen)
    return;

  NSRect win = [m_window frame];
  m_window_x = win.origin.x;
  m_window_y = win.origin.y;
  m_window_width = win.size.width;
  m_window_height = win.size.height;
}

void PlatformMacOS::SetupMenu()
{
  @autoreleasepool
  {
    menuBar = [NSMenu new];

    NSMenu* appMenu = [NSMenu new];
    NSMenu* stateMenu = [[NSMenu alloc] initWithTitle:@"States"];
    NSMenu* loadStateMenu = [[NSMenu alloc] initWithTitle:@"Load"];
    NSMenu* saveStateMenu = [[NSMenu alloc] initWithTitle:@"Save"];
    NSMenu* miscMenu = [[NSMenu alloc] initWithTitle:@"Misc"];

    NSMenuItem* appMenuItem = [NSMenuItem new];
    NSMenuItem* miscMenuItem = [NSMenuItem new];
    NSMenuItem* stateMenuItem = [NSMenuItem new];
    NSMenuItem* loadStateItem = [[NSMenuItem alloc] initWithTitle:@"Load"
                                                           action:nil
                                                    keyEquivalent:@""];
    NSMenuItem* saveStateItem = [[NSMenuItem alloc] initWithTitle:@"Save"
                                                           action:nil
                                                    keyEquivalent:@""];
    [menuBar addItem:appMenuItem];
    [menuBar addItem:stateMenuItem];
    [menuBar addItem:miscMenuItem];

    // Quit
    NSString* quitTitle = [@"Quit " stringByAppendingString:@"dolphin-emu-nogui"];
    NSMenuItem* quitMenuItem = [[NSMenuItem alloc] initWithTitle:quitTitle
                                                          action:@selector(shutdown)
                                                   keyEquivalent:@"q"];

    // Fullscreen
    NSString* fullScreenItemTitle = @"Toggle Fullscreen";
    NSMenuItem* fullScreenItem = [[NSMenuItem alloc] initWithTitle:fullScreenItemTitle
                                                            action:@selector(toggleFullScreen:)
                                                     keyEquivalent:@"f"];
    [fullScreenItem setKeyEquivalentModifierMask:NSEventModifierFlagFunction];

    // Screenshot
    NSString* ScreenShotTitle = @"Take Screenshot";
    unichar c = NSF9FunctionKey;
    NSString* f9 = [NSString stringWithCharacters:&c length:1];
    NSMenuItem* ScreenShotItem = [[NSMenuItem alloc] initWithTitle:ScreenShotTitle
                                                            action:@selector(saveScreenShot)
                                                     keyEquivalent:f9];
    [ScreenShotItem setKeyEquivalentModifierMask:NSEventModifierFlagFunction];

    // Pause game
    NSString* pauseTitle = @"Toggle pause";
    c = NSF10FunctionKey;
    NSString* f10 = [NSString stringWithCharacters:&c length:1];
    NSMenuItem* pauseItem = [[NSMenuItem alloc] initWithTitle:pauseTitle
                                                       action:@selector(togglePause)
                                                keyEquivalent:f10];
    [pauseItem setKeyEquivalentModifierMask:NSEventModifierFlagFunction];

    // Load last save
    NSString* loadLastTitle = @"Load Last Saved";
    c = NSF11FunctionKey;
    NSString* f11 = [NSString stringWithCharacters:&c length:1];
    NSMenuItem* loadLastItem = [[NSMenuItem alloc] initWithTitle:loadLastTitle
                                                          action:@selector(loadLastSaved)
                                                   keyEquivalent:f11];
    [loadLastItem setKeyEquivalentModifierMask:NSEventModifierFlagFunction];

    // Undo Load State
    NSString* undoLoadTitle = @"Undo Load";
    c = NSF12FunctionKey;
    NSString* f12 = [NSString stringWithCharacters:&c length:1];
    NSMenuItem* undoLoadItem = [[NSMenuItem alloc] initWithTitle:undoLoadTitle
                                                          action:@selector(undoLoadState)
                                                   keyEquivalent:f12];
    [undoLoadItem setKeyEquivalentModifierMask:NSEventModifierFlagShift];

    // Undo Save State
    NSString* undoSaveTitle = @"Undo Save";
    NSMenuItem* undoSaveItem = [[NSMenuItem alloc] initWithTitle:undoSaveTitle
                                                          action:@selector(undoSaveState)
                                                   keyEquivalent:f12];
    [undoSaveItem setKeyEquivalentModifierMask:NSEventModifierFlagFunction];

    // Load and Save States
    for (unichar i = NSF1FunctionKey; i <= NSF8FunctionKey; i++)
    {
      NSInteger stateNum = i - NSF1FunctionKey + 1;
      NSString* lstateTitle = [NSString stringWithFormat:@"Load State %ld", (long)stateNum];
      c = i;
      NSString* t = [NSString stringWithCharacters:&c length:1];
      NSMenuItem* lstateItem = [[NSMenuItem alloc] initWithTitle:lstateTitle
                                                          action:@selector(loadState:)
                                                   keyEquivalent:t];
      [lstateItem setTag:stateNum];
      [lstateItem setKeyEquivalentModifierMask:NSEventModifierFlagFunction];
      [loadStateMenu addItem:lstateItem];

      NSString* sstateTitle = [NSString stringWithFormat:@"Save State %ld", (long)stateNum];
      c = i;
      NSMenuItem* sstateItem = [[NSMenuItem alloc] initWithTitle:sstateTitle
                                                          action:@selector(saveState:)
                                                   keyEquivalent:t];
      [sstateItem setKeyEquivalentModifierMask:NSEventModifierFlagShift];
      [sstateItem setTag:stateNum];
      [saveStateMenu addItem:sstateItem];
    }

    // App Main menu
    [appMenu addItem:quitMenuItem];

    // State Menu
    [loadStateItem setSubmenu:loadStateMenu];
    [saveStateItem setSubmenu:saveStateMenu];

    [stateMenu addItem:loadLastItem];
    [stateMenu addItem:undoLoadItem];
    [stateMenu addItem:undoSaveItem];
    [stateMenu addItem:loadStateItem];
    [stateMenu addItem:saveStateItem];

    // Misc Menu
    [miscMenu addItem:fullScreenItem];
    [miscMenu addItem:ScreenShotItem];
    [miscMenu addItem:pauseItem];

    [appMenuItem setSubmenu:appMenu];
    [stateMenuItem setSubmenu:stateMenu];
    [miscMenuItem setSubmenu:miscMenu];

    [NSApp setMainMenu:menuBar];
  }
}


// Imgui Platform Impl
struct ImGui_MacOS_Data
{
    CFTimeInterval              Time;
    NSCursor*                   MouseCursors[ImGuiMouseCursor_COUNT];
    bool                        MouseCursorHidden;
    NSTextInputContext*         InputContext;
    id                          Monitor;
    NSWindow*                   Window;

    ImGui_MacOS_Data()        { memset((void*)this, 0, sizeof(*this)); }
};

struct ImGui_MacOS_ViewportData
{
    NSWindow*        Window;
    bool             WindowOwned;

    ImGui_MacOS_ViewportData()  { WindowOwned = false; }
    ~ImGui_MacOS_ViewportData() { IM_ASSERT(Window == nil); }
};

// from ImGui/backends/imgui_impl_osx.mm
static void ConvertNSRect(NSRect* r)
{
    NSRect firstScreenFrame = NSScreen.screens[0].frame;
    IM_ASSERT(firstScreenFrame.origin.x == 0 && firstScreenFrame.origin.y == 0);
    r->origin.y = firstScreenFrame.size.height - r->origin.y - r->size.height;
}

bool PlatformMacOS::ImGuiPlatformInit(void* view) 
{
  ImGuiIO& io = ImGui::GetIO();
  ImGuiPlatformIO& platform_io = ImGui::GetPlatformIO();

  IM_ASSERT(io.BackendPlatformUserData == nullptr && "Already initialized a platform backend!");

  ImGui_MacOS_Data* bd = IM_NEW(ImGui_MacOS_Data)();
  io.BackendPlatformUserData = (void*)bd;
  io.BackendPlatformName = "Dolphin_MacOS";
  io.BackendFlags |= ImGuiBackendFlags_HasMouseCursors;
  io.BackendFlags |= ImGuiBackendFlags_PlatformHasViewports;

  bd->Window = ((__bridge NSView*)view).window ?: NSApp.orderedWindows.firstObject;
  ImGuiViewport* main_viewport = ImGui::GetMainViewport();
  main_viewport->PlatformHandle = main_viewport->PlatformHandleRaw = (__bridge_retained void*)bd->Window;

  return true;
}

void PlatformMacOS::InitMonitors(ImGuiPlatformIO& platform_io)
{
  @autoreleasepool
  {
  // from ImGui/backends/imgui_impl_osx.mm
  platform_io.Monitors.resize(0);

  NSRect firstScreenFrame = NSScreen.screens[0].frame;
  IM_ASSERT(firstScreenFrame.origin.x == 0 && firstScreenFrame.origin.y == 0);

  for (NSScreen* screen in NSScreen.screens)
    {
        NSRect frame = screen.frame;
        NSRect visibleFrame = screen.visibleFrame;
        ConvertNSRect(&frame);
        ConvertNSRect(&visibleFrame);

        ImGuiPlatformMonitor imgui_monitor;
        imgui_monitor.MainPos = ImVec2(frame.origin.x, frame.origin.y);
        imgui_monitor.MainSize = ImVec2(frame.size.width, frame.size.height);
        imgui_monitor.WorkPos = ImVec2(visibleFrame.origin.x, visibleFrame.origin.y);
        imgui_monitor.WorkSize = ImVec2(visibleFrame.size.width, visibleFrame.size.height);
        imgui_monitor.DpiScale = screen.backingScaleFactor;
        imgui_monitor.PlatformHandle = (__bridge_retained void*)screen;

        platform_io.Monitors.push_back(imgui_monitor);
    }
  }
}

void PlatformMacOS::RegisterMainViewport()
{
  ImGui_MacOS_Data* bd = (ImGui_MacOS_Data*)ImGui::GetIO().BackendPlatformUserData;

  ImGuiViewport* main_viewport = ImGui::GetMainViewport();
  ImGui_MacOS_ViewportData* vd = IM_NEW(ImGui_MacOS_ViewportData)();
  vd->Window = bd->Window;
  vd->WindowOwned = false;
  main_viewport->PlatformUserData = vd;
  main_viewport->PlatformHandle = main_viewport->PlatformHandleRaw = (__bridge void*)vd->Window;
}

void PlatformMacOS::CreateWindow(ImGuiViewport* vp)
{
  @autoreleasepool
  {
  ImGui_MacOS_Data* bd = (ImGui_MacOS_Data*)ImGui::GetIO().BackendPlatformUserData;
  ImGui_MacOS_ViewportData* vd = IM_NEW(ImGui_MacOS_ViewportData)();
  vp->PlatformUserData = vd;

  NSScreen* screen = bd->Window.screen;
  NSRect rect = NSMakeRect(vp->Pos.x, vp->Pos.y, vp->Size.x, vp->Size.y);
  ConvertNSRect(&rect);

  NSWindowStyleMask styleMask = 0;
  if (vp->Flags & ImGuiViewportFlags_NoDecoration)
      styleMask |= NSWindowStyleMaskBorderless;
  else
      styleMask |= NSWindowStyleMaskTitled | NSWindowStyleMaskResizable | NSWindowStyleMaskClosable | NSWindowStyleMaskMiniaturizable;

  __block NSWindow* window;
  dispatch_sync(dispatch_get_main_queue(), ^{window = [[NSWindow alloc] initWithContentRect:rect
                                                styleMask:styleMask
                                                backing:NSBackingStoreBuffered
                                                defer:YES
                                                screen:screen];});

  if (vp->Flags & ImGuiViewportFlags_TopMost)
    [window setLevel:NSFloatingWindowLevel];

  window.title = @"Untitled";
  window.opaque = YES;

  NSView* view = [[NSView alloc] initWithFrame:rect];

    window.contentView = view;

  vd->Window = window;
  vd->WindowOwned = true;
  vp->PlatformRequestResize = false;
  vp->PlatformHandle = vp->PlatformHandleRaw = (__bridge_retained void*)window;
  }
}

void PlatformMacOS::DestroyWindow(ImGuiViewport *vp)
{
  return;
}

ImVec2 PlatformMacOS::GetWindowPos(ImGuiViewport *vp)
{
  return {};
}

void PlatformMacOS::SetWindowPos(ImGuiViewport *vp, ImVec2 size)
{
  return;
}

ImVec2 PlatformMacOS::GetWindowSize(ImGuiViewport *vp)
{
  return {};
}

void PlatformMacOS::SetWindowSize(ImGuiViewport *vp, ImVec2 size)
{
  return;
}

void PlatformMacOS::SetImGuiWindowTitle(ImGuiViewport *vp, const char *str)
{
  @autoreleasepool
  {
    NSWindow* window = (__bridge NSWindow*)vp->PlatformHandle;
    NSString* title = [NSString stringWithUTF8String: str];
    dispatch_async(dispatch_get_main_queue(), ^{
      [window setTitle:title];
    });
  }
}

void PlatformMacOS::ShowWindow(ImGuiViewport *vp)
{
  ImGui_MacOS_ViewportData* vd = (ImGui_MacOS_ViewportData*)vp->PlatformUserData;
  IM_ASSERT(vd->Window != 0);

  if (vp->Flags & ImGuiViewportFlags_NoFocusOnAppearing)
      dispatch_sync(dispatch_get_main_queue(), ^{[vd->Window orderFront:nil]; });
  else
      dispatch_sync(dispatch_get_main_queue(), ^{[vd->Window makeKeyAndOrderFront:nil]; });

  dispatch_sync(dispatch_get_main_queue(), ^{[vd->Window setIsVisible:YES]; });
}

}  // namespace

std::unique_ptr<Platform> Platform::CreateMacOSPlatform()
{
  return std::make_unique<PlatformMacOS>();
}