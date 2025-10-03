#include "GameList.h"
#include "nfd.h"

#include "Core/Config/MainSettings.h"
#include "Common/Contains.h"

namespace HibUI
{
    void GameList::SetIsoPath() {
        nfdu8char_t *game_path;
        nfdresult_t res = NFD_PickFolder(&game_path, "~/");

        auto paths = Config::GetIsoPaths();

          if (Common::Contains(paths, game_path))
            return;

        if(res == NFD_OKAY)
        {
            fprintf(stderr, "added game path: %s", game_path);
            paths.emplace_back(game_path);
            Config::SetIsoPaths(paths);
        }
    }
}