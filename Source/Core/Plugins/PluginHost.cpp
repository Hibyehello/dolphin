// Copyright 2021 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "Plugins/PluginHost.h"
#include "Common/Logging/Log.h"
#include "Plugins/ModuleManager.h"

#include <dlfcn.h>
#include <fmt/core.h>
#include <fmt/format.h>
#include <vector>

static uint64_t Module_id = 0x1000;

void* GetFnPtrFunctor::GetFunction(void* data_void, String* module_name, int64_t version, String* function) {
    auto data = reinterpret_cast<GetFnPtrFunctorData*>(data_void);

    auto plugin = &plugins[data->plugin_idx];


    // Because we give one of these Functors to each plugin, we now know which plugin called us
    uint64_t plugin_id [[maybe_unused]] = plugin->ModuleId;

    // In future versions of this API, we can do fancy things based on which plugin called us
    // and create per-plugin wrappers for every API function

    // For now, we just call return the function pointer
    return GetFnPtr(module_name->Data, version, function->Data);
}

void Plugins::Init()
{
    InitDiscoveryModule();
    InitLoggingModule();
    InitBasicGuiModule();
    InitCPUModule();
}

void Plugins::LoadPlugin(u32 id)
{
        fmt::print("Loading plugin {}\n", plugins.at(id).mainfile);
        // Attempt to load the .so file
        void* handle = dlopen(plugins.at(id).mainfile.c_str(), RTLD_NOW);

        if (!handle)
        {
            fmt::print("dlopen of {} failed: {}\n", plugins.at(id).mainfile, dlerror());
            return;
        }



        // Locate the plugin_init function
        plugins.at(id).plugin_init = reinterpret_cast<void (*)(void*)>(dlsym(handle, "plugin_init"));
        plugins.at(id).plugin_requestShutdown = reinterpret_cast<void (*)(uint64_t)>(dlsym(handle, "plugin_requestShutdown"));

        if (!plugins.at(id).plugin_init)
        {
            fmt::print("{} did not contain plugin_init function: {}\n", plugins.at(id).mainfile, dlerror());
            return;
        }

        if (!plugins.at(id).plugin_requestShutdown)
        {
            fmt::print("{} did not contain plugin_requestShutdown function: {}\n", plugins.at(id).mainfile, dlerror());
            return;
        }

        plugins.at(id).Active = true;
        plugins.at(id).ModuleId = Module_id++;

        plugins.at(id).FnPtrFunctor = GetFnPtrFunctor(id);

        // Actually call the plugin_init method
        plugins.at(id).plugin_init(plugins.at(id).FnPtrFunctor);
}

void Plugins::ShutdownPlugin(u32 id)
{
    fmt::print("Requesting shutdown of {}\n", plugins.at(id).name);
    plugins.at(id).Active = false;
    plugins.at(id).plugin_requestShutdown(Module_id);
}
