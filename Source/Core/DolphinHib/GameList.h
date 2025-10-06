#include <nfd.h>
#include <string>
#include <vector>

#include "Core/TitleDatabase.h"
#include "UICommon/GameFile.h"
#include "UICommon/GameFileCache.h"

namespace HibUI
{

class GameList {

public:
  void ShowGameListWidget();

private:
  void SetIsoPath();
  void AddGame(const std::shared_ptr<const UICommon::GameFile>& game);
  void GameItem(const std::shared_ptr<const UICommon::GameFile>& game);

  std::vector<std::shared_ptr<const UICommon::GameFile>> m_games;
  Core::TitleDatabase m_title_database;
  UICommon::GameFileCache m_cache;
  bool all_games_added;
};

}