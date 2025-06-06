#ifndef OPCODES_HPP
#define OPCODES_HPP

#include <cstdint>

namespace network {

namespace opcode {

// Client -> Server
constexpr uint8_t ping = 0x00;
constexpr uint8_t hello = 0x01;
constexpr uint8_t hello_bot = 0x02;
constexpr uint8_t hello_debug = 0x03;
constexpr uint8_t enter_game = 0x04;
constexpr uint8_t leave_game = 0x05;
constexpr uint8_t resize = 0x06;
constexpr uint8_t input = 0x07;
constexpr uint8_t nick = 0x08;
constexpr uint8_t color = 0x09;
constexpr uint8_t chat = 0x10;
constexpr uint8_t change_room = 0x11;
constexpr uint8_t update_room = 0x12;
constexpr uint8_t delete_room = 0x13;
constexpr uint8_t note = 0x14;
constexpr uint8_t debug_ban = 0x17
constexpr uint8_t debug_mute = 0x15;
constexpr uint8_t debug_kick = 0x16;

// Server -> Client
constexpr uint8_t pong = 0x00;
constexpr uint8_t entered_game = 0xA1;
constexpr uint8_t cursors_v1 = 0xA3;
constexpr uint8_t cursors_v2 = 0xA4;
constexpr uint8_t events = 0xB1;
constexpr uint8_t history = 0xB2;
constexpr uint8_t config = 0xB3;
constexpr uint8_t notes = 0xA7;

} // opcode

namespace session_type {

constexpr uint8_t player = 0xD1;
constexpr uint8_t dev = 0xD2;
constexpr uint8_t bot = 0xD7;

} // session_type

namespace cursor_flag {

constexpr uint8_t full = 0xC0;
constexpr uint8_t full_bot = 0xC1;
constexpr uint8_t full_dev = 0xC4;
constexpr uint8_t partial = 0xC2;
constexpr uint8_t del = 0xC3;

} // cursor_flag

namespace note_flag {

constexpr uint8_t up = 0x01;
constexpr uint8_t down = 0x00;

} // note_flag

namespace event {

constexpr uint8_t sent_message = 0x11;
constexpr uint8_t entered_room = 0x12;
constexpr uint8_t left_room = 0x13;
constexpr uint8_t entered_game = 0x14;
constexpr uint8_t left_game = 0x15;
constexpr uint8_t updated_nick = 0x16;
constexpr uint8_t updated_hue = 0x17;
constexpr uint8_t updated_color = 0x18;
constexpr uint8_t created_room = 0x19;
constexpr uint8_t updated_room = 0x20;
constexpr uint8_t deleted_room = 0x21;

} // event

} // network

#endif
