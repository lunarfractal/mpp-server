#ifndef SESSION_HPP
#define SESSION_HPP

#include <websocketpp/common/connection_hdl.hpp>
#include <../mpp/player.hpp>

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
    player_ptr player;

    bool did_enter_game() {
        return player != nullptr;
    }
};

}

#endif
