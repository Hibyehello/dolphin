#include <memory>
#include <nfd.h>
#include <string>
#include <vector>

#include "Common/WindowSystemInfo.h"
#include "Core/Boot/Boot.h"
#include "Core/TitleDatabase.h"
#include "UICommon/GameFile.h"
#include "UICommon/GameFileCache.h"

namespace HibUI
{

class GameList {

public:
  void ShowGameListWidget();
  
  bool GameSelected() { return ready_to_launch; }
  std::string& GameToLaunch() { return game_to_launch; }

private:
  void SetIsoPath();
  void AddGame(const std::shared_ptr<const UICommon::GameFile>& game);
  void GameItem(const std::shared_ptr<const UICommon::GameFile>& game);

  std::vector<std::shared_ptr<const UICommon::GameFile>> m_games;
  Core::TitleDatabase m_title_database;
  UICommon::GameFileCache m_cache;
  std::string game_to_launch;
  bool ready_to_launch;
};

}