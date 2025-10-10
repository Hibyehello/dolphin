// Copyright 2025 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <AppKit/AppKit.h>
#include <Carbon/Carbon.h>
#include <Foundation/Foundation.h>

extern "C" void* getContentView(void* window) {
  @autoreleasepool
  {
    NSWindow* n_window = (__bridge NSWindow*)window;

    return (void*)CFBridgingRetain([n_window contentView]);
  }
}