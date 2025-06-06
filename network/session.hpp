#ifndef SESSION_HPP
#define SESSION_HPP

#include <memory>

#include <websocketpp/common/connection_hdl.hpp>
#include <../game/player.hpp>

namespace game { class player; }

namespace network {

class session {
public:
    session(connection_hdl hdl) hdl(hdl), type(0),
        received_ping(false), received_hello(false), 
        screen_width(0), screen_height(0) {}

    uint8_t type;
    connection_hdl hdl;
    bool received_ping, received_hello;
    uint16_t screen_width, screen_height;
    std::shared_ptr<game::player> player;

    bool did_enter_game() {
        return player != nullptr;
    }

    bool did_send_hello() {
        return received_hello && received_ping;
    }
};

typedef std::weak_ptr<session> session_ptr;

}

#endif
