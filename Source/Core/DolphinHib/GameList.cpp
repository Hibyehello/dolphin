#include "GameList.h"
#include "Core/TitleDatabase.h"
#include "UICommon/GameFile.h"
#include "imgui_internal.h"
#include "nfd.h"
#include <cstddef>
#include <cstdio>
#include <imgui.h>

#include <filesystem>
#include <memory>
#include <type_traits>
#include <vector>

#include "Core/Config/MainSettings.h"
#include "Common/Contains.h"

namespace HibUI
{
void GameList::ShowGameListWidget()
{
  static bool first_time = true;
  if(first_time)
  {
    // Currently unused since imgui isn't event based
    const auto game_loaded = [this](const std::shared_ptr<const UICommon::GameFile>& game) {
      return;
    };
    const auto game_updated = [this](const std::shared_ptr<const UICommon::GameFile>& game) {
      return;
    };
    const auto game_removed = [this](const std::string& path) {
      return;
    };

    bool cache_updated =
      m_cache.Update(Config::GetIsoPaths(), game_loaded, game_removed, false);
    cache_updated |= m_cache.UpdateAdditionalMetadata(game_updated, false);

    if(cache_updated)
      m_cache.Save();

    m_title_database = Core::TitleDatabase();
    first_time = false;
  }

  m_cache.Load();

  ImGui::Begin("Games");
  if(m_cache.GetSize() == 0)
  {
    ImGui::Text("No Games found!");
    if(ImGui::Button("Add a Game Directory"))
    {
        SetIsoPath();
    }
  }
  else
  {
    ImGui::BeginChild("GameList", {0, 0}, ImGuiChildFlags_AutoResizeY);
    m_cache.ForEach([this](const std::shared_ptr<const UICommon::GameFile>& game) { GameItem(game);});
    ImGui::EndChild();
  }

    if(ImGui::Button("Refresh Games List"))
    {
      m_title_database = Core::TitleDatabase();
      m_games.clear();
    }

  ImGui::End();
}

void GameList::SetIsoPath()
{
  nfdu8char_t *game_path;
  nfdresult_t res = NFD_PickFolder(&game_path, "~/");

  auto paths = Config::GetIsoPaths();

  if (Common::Contains(paths, game_path))
    return;

  if(res == NFD_OKAY)
  {
    fprintf(stderr, "added game path: %s\n", game_path);
    paths.emplace_back(game_path);
    Config::SetIsoPaths(paths);
  }
}

void GameList::GameItem(const std::shared_ptr<const UICommon::GameFile>& game)
{
  const char* text = game->GetName(Core::TitleDatabase()).c_str();

  ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, {0.0f, 0.5f});
  // Avoid duplicate ids
  ImGui::PushID(game->GetFileName().c_str());
  if(ImGui::Button(text, {ImGui::GetItemRectMax().x, 20}))
  {
    fprintf(stderr, "Should boot: %s\n", game->GetFilePath().c_str());
  }
  ImGui::PopID();
  ImGui::PopStyleVar();
}

void GameList::AddGame(const std::shared_ptr<const UICommon::GameFile>& game)
{
  m_games.emplace_back(game);
}


} // HibUI