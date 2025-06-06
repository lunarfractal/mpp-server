#ifndef PLAYER_HPP
#define PLAYER_HPP

#include <string>
#include <memory>
#include <cstdint>
#include <unordered_set>

namespace game { // this is gonna be a problem later but I can fix it

class player {
public:
    player() x(300), y(400), id(0),
        hue(226), red(0), green(60), blue(255),
        deletion_reason(0),
        room_id(""), nick(""), is_bot(false),
        is_player(false), is_dev(false) {}

    uint16_t x, y;
    uint16_t id;
    std::string room_id;
    std::string nick;
    network::session_ptr session;
    bool is_bot, is_player, is_dev;

    uint16_t hue;
    uint8_t red, green, blue;

    uint8_t deletion_reason;

    void updateCursor(uint16_t _x, uint16_t _y) {
        auto s = session.lock();
        x = (_x * 65535) / s->screen_width;
        y = (_y * 65535) / s->screen_height;
    }

    bool should_have_in_view(std::shared_ptr<player> p) {
        return p->room_id == room_id;
    }

    bool does_have_in_view(std::shared_ptr<player> p) {
        return view.find(p->id) != view.end();
    }
}

typedef std::shared_ptr<player> player_ptr;
}

#endif
