#pragma once

#include <picojson.h>
#include <string>
#include <vector>

#include "Common/FileUtil.h"
#include "PluginCpp/BasicTypes.h"

extern FunctorOwner PluginHostOwner;


// TODO: Sub-classing a functor is currently way too complex. We need a c++ wrapper to make this simple
class GetFnPtrFunctor : public Functor<void*(String*, int64_t, String*)>
{
    struct GetFnPtrFunctorData : public Functor<void*(String*, int64_t, String*)>::FunctorData
    {
        u32 plugin_idx;
        std::function<void*(String*, int64_t, String*)> GetFunction;
    };

    GetFnPtrFunctorData data_instance;

    static void* GetFunction(void* data_void, String* module_name, int64_t version, String* function);

public:
    GetFnPtrFunctor() = default;

    GetFnPtrFunctor(u32 plugin_idx) {
        data_instance.FnPtr = &GetFunction;
        data_instance.Owner = nullptr;
        data_instance.plugin_idx = plugin_idx;

        Data = &data_instance;
    }

    GetFnPtrFunctor(const GetFnPtrFunctor& other) {
        data_instance = other.data_instance;
        Data = &data_instance;
    }
};

struct PluginHost
{
  // From Plugin json
  std::string name;
  float version;
  std::vector<std::string> dependencies;
  std::string mainfile;
  bool isScriptHost;
  std::vector<std::string> fileTypes;
  bool sandbox;
  // Used Internally
  int ModuleId;
  bool Active;
  void (*plugin_init)(void*);
  void (*plugin_requestShutdown)(uint64_t);
  GetFnPtrFunctor FnPtrFunctor;
};

static std::vector<PluginHost> plugins;

namespace PluginManager
{
void FindAllPlugins();
void ParsePluginJSON(const File::FSTEntry json);
std::vector<PluginHost>* getPlugins();
}  // namespace PluginManager