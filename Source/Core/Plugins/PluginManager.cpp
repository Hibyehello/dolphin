#include "PluginManager.h"

#include <fmt/core.h>
#include <fmt/format.h>
#include <fstream>
#include <picojson.h>
#include <iostream>
#include <sstream>

/*
{
  "name": "Lua",
  "version": "1.0",
  "dependencies": [],
  "mainfile": "luaSciptHost.so" // <-- This is why I want my on library binary format, otherwise we need to ship all three, and then 3 more for ARM
  "scripthost": {
    "fileTypes": ["lua"],
    "sandbox": true
  }
}
*/

void PluginManager::FindAllPlugins()
{
  PluginDir = File::GetUserPath(D_LOAD_IDX) + "Plugins";
  for (const auto& entry : File::ScanDirectoryTree(PluginDir, true).children)
  {
    std::cerr << "running" << std::endl;
    if (entry.virtualName.find("json") != std::string::npos)
    {
      ParsePluginJSON(entry);
    }
  }
}

void PluginManager::ParsePluginJSON(const File::FSTEntry json_in)
{
  std::ifstream pluginJSON(json_in.physicalName);
  picojson::value json;
  std::stringstream ss;
  PluginHost plugin;

  if (!pluginJSON.is_open())
  {
    fmt::print("Failed to open {}", json_in.physicalName);
    return;
  }

  ss << pluginJSON.rdbuf();

  const auto error = picojson::parse(json, ss.str());

  for (const auto& [key, value] : json.get<picojson::object>()) 
  {
    if (key == "name" && value.is<std::string>())
    {
      plugin.name = value.get<std::string>();
    } 
    else if (key == "version" && value.is<double>()) 
    {
      plugin.version = value.get<double>();
    }
    else if (key == "dependencies" && value.is<picojson::array>())
    {
      for (const picojson::value& dependency : value.get<picojson::array>()) 
      {
        plugin.dependencies.push_back(dependency.get<std::string>());
      }
    }
    else if (key == "mainfile" && value.is<std::string>())
    {
      plugin.mainfile = PluginDir + "/" + value.get<std::string>();
    }
    else if (key == "scripthost") 
    {
      plugin.isScriptHost = true;
      if (key == "filesTypes" && value.is<picojson::array>())
      {
        for (const picojson::value& fileType : value.get<picojson::array>()) 
        {
          plugin.fileTypes.push_back(fileType.get<std::string>());
        }
      }
      else if (key == "sandbox" && value.get<bool>())
      {
        plugin.sandbox = value.get<bool>();
      }
    }
    plugin.Active = false;
  }

  bool exists = false;
  for(const auto& Checkplugin : plugins) 
  {
    exists = ((Checkplugin.mainfile == plugin.mainfile) && (Checkplugin.name == plugin.name));
  }

  std::cerr << "exists: " << exists << std::endl;

  if(!exists)
    plugins.push_back(plugin);
}

std::vector<PluginHost>* PluginManager::getPlugins()
{
  FindAllPlugins();
  return &plugins;
}
