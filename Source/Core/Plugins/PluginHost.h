// Copyright 2021 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <string>
#include <functional>
#include <vector>

#include "Common/FileUtil.h"

#include "PluginCpp/BasicTypes.h"
#include "PluginManager.h"
#include "ModuleManager.h"

namespace Plugins {

std::vector<PluginHost>* pluginsList;

void Init();
void LoadPlugin(u32 id);
void ShutdownPlugin(u32 id);

} // Plugins