#ifndef GAME_HPP
#define GAME_HPP

#include <deque>
#include <unordered_map>
#include <unordered_set>

#include "../utils/utils.hpp"
#include "player.hpp"

namespace game {

class game_manager {
public:
    std::unordered_map<uint16_t, std::shared_ptr<player>> active_players;
    std::unordered_set<uint16_t> pending_deletions;
    std::unordered_set<std::string> rooms;

    uint16_t add_player(std::shared_ptr<game::Player> player) {
        uint16_t id = utils::getUint16();
        while(active_players.find(id) != active_players.end()) {
          id = utils::getUint16();
        }
        active_players[id] = player;
        return id;
    }

    void delete_player(uint16_t id) {
        auto it = active_players.find(id);
        if (it == active_players.end()) return;

        auto playerPtr = it->second;

        if (playerPtr->session) {
            if (playerPtr->session->player) {
                playerPtr->session->player.reset();
            }
            playerPtr->session.reset();
         }

        active_players.erase(it);
    }


    void mark_for_deletion(uint16_t id) {
        pending_deletions.insert(id);
    }

    void delete_pending() {
        for (uint16_t id: pending_deletions) {
            delete_player(id);
        }
        pending_deletions.clear();
    }
}

}

#endif
