#ifndef MESSAGE_HPP
#define MESSAGE_HPP

#include <string>

namespace game {

struct message {
    std::string content;
    std::string owner_nick;
    uint16_t owner_hue;
    uint16_t owner_id;
    double timestamp;
};

}

#endif
