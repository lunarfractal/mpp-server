#ifndef UTILS_HPP
#define UTILS_HPP

#include <cstdint>
#include <string>
#include <cstdlib>
#include <ctime>

namepsace utils {

uint16_t getHue() {
    std::srand(static_cast<unsigned>(std::time(nullptr)));
    uint16_t value = static_cast<uint16_t>(std::rand() % 361);
    return value;
}

std::string getString(const std::string& data, int &offset) {
    std::string result;

    if (offset >= data.size()) {
        throw std::out_of_range("offset is out of bounds");
    }

    while (offset < data.size()) {
        char ch = data[offset++];
        if (ch == '\0') {
            return result;
        }
        result.push_back(ch);
    }

    throw std::out_of_range("no null terminator");
}

}

#endif
