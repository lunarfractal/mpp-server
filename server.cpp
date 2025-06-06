#define ASIO_STANDALONE

#include <websocketpp/config/asio_no_tls.hpp>
#include <websocketpp/server.hpp>

#include <unordered_map>

#include "utils/utils.hpp"
#include "utils/logger.hpp"
#include "game/world.hpp"

#include "net/session.hpp"
#include "net/opcodes.hpp"

using websocketpp::lib::bind;
using websocketpp::lib::placeholders::_1;
using websocketpp::lib::placeholders::_2;

typedef websocketpp::server<websocketpp::config::asio> server;
typedef websocketpp::connection_hdl connection_hdl;
typedef server::message_ptr message_ptr;

class WebSocketServer {
public:
    WebSocketServer() {
        m_server.init_asio();

        m_server.set_open_handler(bind(&WebSocketServer::on_open,this,::_1));
        m_server.set_close_handler(bind(&WebSocketServer::on_close,this,::_1));
        m_server.set_message_handler(bind(&WebSocketServer::on_message,this,::_1,::_2));

        m_server.clear_access_channels(websocketpp::log::alevel::all);
    }

    void processMessage(std::string &buffer, connection_hdl hdl) {
        auto it = m_sessions.find(hdl);
        if (it == m_sessions.end()) {
            logger::log("No session found for given connection", logger::Level::WARN);
            return;
        }
        auto s = it->second;

        if (buffer.empty()) {
            logger::log("received empty buffer", logger::Level::WARN);
            return;
        }

        uint8_t op = buffer[0];

        switch(op) {
            case net::opcode_ping:
            {
                pong(hdl);
                logger::log("Received ping", logger::Level::DEBUG);
                if (!s->sent_ping) {
                    s->sent_ping = true;
                    logger::log("first ping from session", logger::Level::DEBUG);
                }
                break;
            }

            case net::opcode_hi:
            {
                if (buffer.length() >= 5) {
                    std::memcpy(&s->screen_width, &buffer[1], 2);
                    std::memcpy(&s->screen_height, &buffer[3], 2);
                    // fix vulnerability
                    if(s->screen_width == 0x00) s->screen_width = 1;
                    if(s->screen_height == 0x00) s->screen_height = 1;
                    logger::log("Received hi: screen " + std::to_string((int)s->screen_width) + "x" + std::to_string((int)s->screen_height), logger::Level::DEBUG);
                } else {
                    logger::log("invalid hi packet (too short)", logger::Level::WARN);
                }

                if (!s->sent_hello) {
                    s->sent_hello = true;
                    logger::log("first hi from session", logger::Level::DEBUG);
                }
                break;
            }

            case net::opcode_hi_bot:
                if(!s->sent_hello && !s->sent_hello_bot) {
                    s->sent_hello = true;
                    s->sent_hello_bot = true;
                    logger::log("received hi from a bot");
                }
                break;

            case net::opcode_enter_game:
            {
                if(buffer.size() >= 39) {
                    logger::log("nick is too long!", logger::Level::WARN);
                    return;
                }

                if (s->did_enter_game() || !s->sent_ping || !s->sent_hello) {
                    logger::log("Received enter_game but session is not ready or already in game", logger::Level::WARN);
                    return;
                }

                auto player = std::make_shared<game::Player>();
                player->session = s;
                player->needs_init = true;

                player->hue = utils::getHue();

                int offset = 1;
                player->nick = utils::getU16String(buffer, offset);
                player->id = game_world.add_player(player);
                std::string room_id = s->orig_room_id;

                if (game_world.rooms.find(room_id) == game_world.rooms.end()) {
                    logger::log("Creating room: " + room_id + " for player " + std::to_string((int)player->id), logger::Level::INFO);
                    game_world.rooms.insert(room_id);
                }

                player->room_id = room_id;

                if(s->sent_hello_bot) player->isBot = true;
                sendId(hdl, player->id, player->hue);
                s->player = player;
                s->sent_nick_count++;

                logger::log(
                    "Player " + std::to_string((int)player->id) + 
                    " entered game in room: " + room_id +
                    " (" + std::to_string((int)s->sent_nick_count) + " times)", logger::Level::INFO
                );
                break;
            }

            case net::opcode_leave_game:
            {
                if (!s->did_enter_game()) {
                    logger::log("Received leave_game while not in game", logger::Level::WARN);
                    return;
                }

                logger::log("Player " + std::to_string((int)s->player->id) + " is leaving the game", logger::Level::INFO);
                s->player->deletion_reason = 0x03;
                game_world.mark_for_deletion(s->player->id);
                break;
            }

            case net::opcode_resize:
            {
                if (buffer.length() >= 5) {
                    std::memcpy(&s->screen_width, &buffer[1], 2);
                    std::memcpy(&s->screen_height, &buffer[3], 2);
                    // fix vulnerability
                    if(s->screen_width == 0x00) s->screen_width = 1;
                    if(s->screen_height == 0x00) s->screen_height = 1;
                    logger::log("Screen resized to: " + std::to_string((int)s->screen_width) + "x" + std::to_string((int)s->screen_height), logger::Level::DEBUG);
                } else {
                    logger::log("invalid resize packet (too short)", logger::Level::WARN);
                }
                break;
            }

            case net::opcode_cursor:
            {
                if (!s->did_enter_game()) {
                    logger::log("Received cursor update before entering game", logger::Level::WARN);
                    return;
                }

                if(buffer.size() < 5) {
                    logger::log("cursor packer is too short!", logger::Level::WARN);
                    return;
                }

                uint16_t x, y;
                std::memcpy(&x, &buffer[1], 2);
                std::memcpy(&y, &buffer[3], 2);

                s->player->updateCursor(x, y);
                break;
            }

            case net::opcode_cd:
            {
                if (!s->did_enter_game()) {
                    logger::log("Received cd before entering game", logger::Level::WARN);
                    return;
                }

                int offset = 1;
                std::string room_id = utils::getString(buffer, offset);

                if(room_id == "") {
                    logger::log("empty room id!", logger::Level::WARN);
                    return;
                }

                if (game_world.rooms.find(room_id) == game_world.rooms.end()) {
                    logger::log("Creating new room (cd): " + room_id + " for player " + std::to_string((int)s->player->id), logger::Level::INFO);
                    game_world.rooms.insert(room_id);
                }

                logger::log("Changing room to: " + room_id + " for player " + std::to_string((int)s->player->id), logger::Level::DEBUG);
                s->player->room_id = room_id;
                break;
            }

            case net::opcode_ls:
            {
                if (!s->did_enter_game()) {
                    logger::log("Received ls before entering game", logger::Level::WARN);
                    return;
                }
                logger::log("listing rooms", logger::Level::DEBUG);
                int bufferSize = 1;
                for(const std::string &id: game_world.rooms) {
                    bufferSize += id.length() + 1;
                }
                std::vector<uint8_t> buffer(bufferSize);
                buffer[0] = net::opcode_config;
                int offset = 1;
                for(const std::string &id: game_world.rooms) {
                    std::memcpy(&buffer[offset], id.data(), id.size());
                    offset += id.size();
                    buffer[offset++] = 0x00;
                }
                sendBuffer(hdl, buffer.data(), buffer.size());
                break;
            }

            case net::opcode_chat:
            {
                if(buffer.size() > 1 + 2 * 200 + 3) {
                    logger::log("message too long!", logger::Level::WARN);
                    return;
                }

                if (!s->did_enter_game()) {
                    logger::log("Received chat before entering game", logger::Level::WARN);
                    return;
                }
                int offset = 1;
                std::u16string chat_message = utils::getU16String(buffer, offset);
                if(chat_message == u"") {
                    logger::log("empty message!", logger::Level::WARN);
                    return;
                }
                logger::log("Player " + std::to_string((int)s->player->id) + " sent chat message", logger::Level::DEBUG);
                dispatch_message(chat_message, s->player->id, s->player->nick, s->player->room_id);
                game_world.add_message(s->player->room_id, {
                    chat_message,
                    s->player->nick,
                    s->player->hue,
                    s->player->id,
                    std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count()
                });
                break;
            }

            case net::opcode_ls_messages:
            {
                if (!s->did_enter_game()) {
                    logger::log("Received ls_messages before entering game", logger::Level::WARN);
                    return;
                }

                int bufferSize = 1;

                for(auto &msg: game_world.id2messages[s->player->room_id]) {
                    bufferSize += 2;
                    bufferSize += 2;
                    bufferSize += 8;
                    bufferSize += 2 * msg.owner_nick.length() + 2;
                    bufferSize += 2 * msg.content.length() + 2;
                }

                std::vector<uint8_t> buffer(bufferSize);

                buffer[0] = net::opcode_history;

                int offset = 1;
                for(auto &msg: game_world.id2messages[s->player->room_id]) {
                    std::memcpy(&buffer[offset], &msg.owner_id, 2);
                    offset += 2;
                    std::memcpy(&buffer[offset], &msg.owner_hue, 2);
                    offset += 2;
                    std::memcpy(&buffer[offset], &msg.timestamp, 8);
                    offset += 8;
                    std::memcpy(&buffer[offset], msg.owner_nick.data(), msg.owner_nick.length() * 2);
                    offset += msg.owner_nick.length() * 2;
                    buffer[offset++] = 0x00;
                    buffer[offset++] = 0x00;
                    std::memcpy(&buffer[offset], msg.content.data(), msg.content.length() * 2);
                    offset += msg.content.length() * 2;
                    buffer[offset++] = 0x00;
                    buffer[offset++] = 0x00;
                }

                sendBuffer(hdl, buffer.data(), buffer.size());
                break;
            }

            default:
                logger::log("unknown opcode received: " + std::to_string(op), logger::Level::WARN);
                break;
        }
    }


    void cycle_s() {
        std::thread([this]() {
            while(true) {
                auto then = std::chrono::steady_clock::now() + std::chrono::milliseconds(1000 / 30);

                if(game_world.active_players.empty()) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
                   /* std::cout << "Sleeping... " << gameWorld.activePlayers.size()
                        << std::endl; */
                    continue;
                }

                // update to every player
                for(auto &[id, player]: game_world.active_players) {
                    if(player->needs_init && player->deletion_reason == 0) {
                        int bufferSize = 1;

                        for(auto &pair: game_world.active_players) {
                            if(pair.second->id == id) {
                                continue;
                            }
                            bufferSize += 2; // id
                            bufferSize += 1; // flag
                            bufferSize += 4; // 2+2 xy
                            bufferSize += 2; // hue
                            bufferSize += 2 * pair.second->nick.length() + 2;
                        }

                        bufferSize += 2;

                        std::vector<uint8_t> buffer(bufferSize);
                        buffer[0] = net::opcode_cycle_s;

                        int offset = 1;

                        for(auto &pair: game_world.active_players) {
                            if(pair.second->id == id) {
                              /*  std::cout << "it's my id" << std::endl;*/
                                continue;
                            }
                            std::memcpy(&buffer[offset], &pair.first, 2);
                            offset += 2;
                            buffer[offset++] = pair.second->isBot ? 0x3 : 0x0;
                            std::memcpy(&buffer[offset], &pair.second->x, 2);
                            offset += 2;
                            std::memcpy(&buffer[offset], &pair.second->y, 2);
                            offset += 2;
                            std::memcpy(&buffer[offset], &pair.second->hue, 2);
                            offset += 2;
                            std::memcpy(&buffer[offset], pair.second->nick.data(), 2 * pair.second->nick.length());
                            offset += 2 * pair.second->nick.length();
                            buffer[offset++] = 0x00;
                            buffer[offset++] = 0x00;
                            player->view.insert(pair.first);
                        }

                        buffer[offset++] = 0x00;
                        buffer[offset++] = 0x00;

                        sendBuffer(player->session->hdl, buffer.data(), buffer.size());

                        player->needs_init = false;
                      /*  std::cout << "Sent init packets" << std::endl;*/
                        continue;
                    }

                    int bufferSize = 1; // opcode

                    for(auto &pair: game_world.active_players) {
                      /*  std::cout << "trying to size normal packet" << std::endl;*/
                        if(pair.second->id == id) {
                          /*  std::cout << "its my id" << std::endl;*/
                            continue;
                        }
                        if(player->hasInView(pair.second)) {
                            if(pair.second->deletion_reason > 0) {
                                bufferSize += 2; // id
                                bufferSize += 1; // flag
                                continue;
                            }
                            uint8_t creation = player->view.find(pair.second->id) == player->view.end() ? 0x0 : 0x1;

                            bufferSize += 2; // id
                            bufferSize += 1; // flag
                            bufferSize += 4; // 2+2 uint16's
                            if(creation == 0x0) bufferSize += 2 + 2 * pair.second->nick.length() + 2;
                        }
                        else {
                            if(player->view.find(pair.second->id) != player->view.end()) {
                                bufferSize += 3;
                            }
                        }
                    }

                    bufferSize += 2;

                    std::vector<uint8_t> buffer(bufferSize);

                    buffer[0] = net::opcode_cycle_s;

                    int offset = 1;
                    
                    for(auto &pair: game_world.active_players) {
                      /*  std::cout << "trying to encode normal packet" << std::endl;*/
                        if(pair.first == id) {
                            continue;
                        }
                        if(pair.second->deletion_reason == 0x02) {
                            std::memcpy(&buffer[offset], &pair.first, 2);
                            offset += 2;
                            buffer[offset++] = 0x2;
                            player->view.erase(pair.first);
                            continue;
                        } else if(pair.second->deletion_reason == 0x03) {
                            std::memcpy(&buffer[offset], &pair.first, 2);
                            offset += 2;
                            buffer[offset++] = 0x2;
                            player->view.erase(pair.first);
                            continue;
                        }

                        if(player->hasInView(pair.second)) { // check if they're in the same room (then it should be visible
                            bool isNew = player->view.find(pair.second->id) == player->view.end(); // check if it's present
                            uint8_t creation;
                            if(isNew) {
                                creation = pair.second->isBot ? 0x3 : 0x0;
                            } else {
                                creation = 0x1;
                            }
                            std::memcpy(&buffer[offset], &pair.first, 2);
                            offset += 2;
                            buffer[offset++] = creation;
                            std::memcpy(&buffer[offset], &pair.second->x, 2);
                            offset += 2;
                            std::memcpy(&buffer[offset], &pair.second->y, 2);
                            offset += 2;
                            if(isNew) {
                                player->view.insert(pair.second->id);
                                std::memcpy(&buffer[offset], &pair.second->hue, 2);
                                offset += 2;
                                std::memcpy(&buffer[offset], pair.second->nick.data(), 2 * pair.second->nick.length());
                                offset += 2 * pair.second->nick.length();
                                buffer[offset++] = 0x00;
                                buffer[offset++] = 0x00;
                            }
                        } else {
                            if(player->view.find(pair.second->id) != player->view.end()) {
                                player->view.erase(pair.second->id);
                                std::memcpy(&buffer[offset], &pair.first, 2);
                                offset += 2;
                                buffer[offset++] = 0x2;
                            }
                        }
                   }

                   buffer[offset++] = 0x00;
                   buffer[offset++] = 0x00;

                   if(player->deletion_reason == 0x00)
                       sendBuffer(player->session->hdl, buffer.data(), buffer.size());
                }

                for (uint16_t id: game_world.pending_deletions) {
                    game_world.delete_player(id);
                }

                game_world.pending_deletions.clear();

                std::this_thread::sleep_until(then);
            }
        }).detach();
    }

    void on_open(connection_hdl hdl) {
        logger::log("Connection opened", logger::Level::INFO);

        server::connection_ptr con = m_server.get_con_from_hdl(hdl);
        std::string path = con->get_resource();
        logger::log("path: " + path, logger::Level::DEBUG);

        std::string room_id = "lobby";
        std::unordered_map<std::string, std::string> query = utils::parse_query(path);

        auto it = query.find("id");
        if (it != query.end()) {
            room_id = it->second;
            logger::log("id: " + room_id, logger::Level::DEBUG);
        } else {
            logger::log("default connection, connecting to lobby", logger::Level::DEBUG);
        }

        auto s = std::make_shared<net::session>();
        s->hdl = hdl;
        s->orig_room_id = room_id == "" ? "lobby" : room_id;
        m_sessions[hdl] = s;

        logger::log("Added session, number of sessions: " + std::to_string(m_sessions.size()), logger::Level::INFO);
    }

    void on_close(connection_hdl hdl) {
        auto it = m_sessions.find(hdl);
        if (it == m_sessions.end()) {
            logger::log("on_close called but no session for hdl", logger::Level::WARN);
            return;
        }

        auto s = it->second;
        if (s->did_enter_game()) {
            logger::log("Player " + std::to_string((int)s->player->id) + " disconnected 0x02", logger::Level::INFO);
            s->player->deletion_reason = 0x02;
            game_world.mark_for_deletion(s->player->id);
        } else {
            logger::log("Some person disconnected before entering the game", logger::Level::DEBUG);
        }

        m_sessions.erase(it);
        logger::log("session deleted. remaining: " + std::to_string(m_sessions.size()), logger::Level::INFO);
    }


    void on_message(connection_hdl hdl, message_ptr msg) {
        if(msg->get_opcode() == websocketpp::frame::opcode::binary) {
            std::string payload = msg->get_payload();
            processMessage(payload, hdl);
        }
    }

    void run(uint16_t port) {
        m_server.listen(port);
        m_server.start_accept();
        m_server.run();
    }

    void stop() {
        m_server.stop_listening();
    }

private:
    game::GameWorld game_world;

    server m_server;

    typedef struct {
        std::size_t operator()(const websocketpp::connection_hdl& hdl) const {
            return std::hash<std::uintptr_t>()(reinterpret_cast<std::uintptr_t>(hdl.lock().get()));
        }
    } connection_hdl_hash;

    typedef struct {
        bool operator()(const websocketpp::connection_hdl& lhs, const websocketpp::connection_hdl& rhs) const {
            return !lhs.owner_before(rhs) && !rhs.owner_before(lhs);
        }
    } connection_hdl_equal;


    std::unordered_map<connection_hdl, std::shared_ptr<net::session>, connection_hdl_hash, connection_hdl_equal> m_sessions;


    void dispatch_message(const std::u16string &value, uint16_t id, std::u16string &nick, std::string &room_id) {
        const int size = 1 + 1 + 2 + 2 * nick.length() + 2 + 2 * value.length() + 2;
        std::vector<uint8_t> buffer(size);
        buffer[0] = net::opcode_events;
        int offset = 1;
        buffer[offset++] = 0x1;
        std::memcpy(&buffer[offset], &id, 2);
        offset += 2;
        std::memcpy(&buffer[offset], nick.data(), 2 * nick.length());
        offset += 2 * nick.length();
        buffer[offset++] = 0x00;
        buffer[offset++] = 0x00;
        std::memcpy(&buffer[offset], value.data(), 2 * value.length());
        offset += 2 * value.length();
        buffer[offset++] = 0x00;
        buffer[offset++] = 0x00;

        send_dispatch(buffer.data(), buffer.size(), room_id);
    }

    void pong(connection_hdl hdl) {
        uint8_t buffer[] = {net::opcode_pong};
        try {
            m_server.send(hdl, buffer, sizeof(buffer), websocketpp::frame::opcode::binary);
        } catch (websocketpp::exception const & e) {
            std::cout << "Pong failed because: "
                << "(" << e.what() << ")" << std::endl;
        }
    }

    void sendId(connection_hdl hdl, uint16_t id, uint16_t hue) {
        uint8_t buffer[5];
        buffer[0] = net::opcode_entered_game;
        std::memcpy(&buffer[1], &id, sizeof(uint16_t));
        std::memcpy(&buffer[3], &hue, 2);
        try {
            m_server.send(hdl, buffer, sizeof(buffer), websocketpp::frame::opcode::binary);
        } catch (websocketpp::exception const & e) {
            std::cout << "Send failed because: "
                << "(" << e.what() << ")" << std::endl;
        }
    }

    void send_dispatch(uint8_t* buffer, size_t size, std::string &room_id) {
        for (auto &pair: m_sessions) {
            try {
                if (
                    m_server.get_con_from_hdl(pair.first)->get_state() == websocketpp::session::state::open
                    && pair.second->did_enter_game()
                    && pair.second->player->room_id == room_id
                ) {
                    m_server.send(pair.first, buffer, size, websocketpp::frame::opcode::binary);
                }
            } catch (websocketpp::exception const & e) {
                std::cout << "Send failed because: "
                    << "(" << e.what() << ")" << std::endl;
            }
        }
    }

    void sendBuffer(connection_hdl hdl, uint8_t *buffer, size_t size) {
        try {
            if (m_server.get_con_from_hdl(hdl)->get_state() == websocketpp::session::state::open)
                m_server.send(hdl, buffer, size, websocketpp::frame::opcode::binary);
        } catch (websocketpp::exception const & e) {
            std::cout << "Send failed because: "
                << "(" << e.what() << ")" << std::endl;
        }
    }
};


int main() {
    WebSocketServer wsServer;
    wsServer.cycle_s();
    wsServer.run(8081);
    wsServer.stop();
    return 0;
}
