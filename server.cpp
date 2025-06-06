#include <csignal>
#include <functional>

#define ASIO_STANDALONE

#include <websocketpp/config/asio_no_tls.hpp>
#include <websocketpp/server.hpp>

#include "network/network.hpp"
#include "utils/utils.hpp"
#include "game/game.hpp"


using websocketpp::lib::bind;
using websocketpp::lib::placeholders::_1;
using websocketpp::lib::placeholders::_2;

typedef websocketpp::server<websocketpp::config::asio> server;
typedef websocketpp::connection_hdl connection_hdl;
typedef server::message_ptr message_ptr;


class mpp_server {
public:
    mpp_server() {
        m_server.init_asio();

        m_server.set_open_handler(bind(&mpp_server::on_open,this,::_1));
        m_server.set_close_handler(bind(&mpp_server::on_close,this,::_1));
        m_server.set_message_handler(bind(&mpp_server::on_message,this,::_1,::_2));

        m_server.clear_access_channels(websocketpp::log::alevel::all);

        alog = m_server.get_alog();
        elog = m_server.get_elog();
    }

    void start_loop() {}

    void process_message(std::string &buffer, connection_hdl hdl) {
        auto s = m_sessions[hdl];

        switch(buffer[0]) {
            case network::opcode::ping:
            {
                alog.write(websocketpp::log::alevel::app, "ping!");
                m_server.send(hdl, {0}, 1, websocketpp::frame::opcode::binary);
                alog.write(websocketpp::log::alevel::app, "I sent a pong as a response.");
                
                if(!s->received_ping) s->received_ping = true;
                
                break;
            }

            case network::opcode::hello:
            {
                if(buffer.length() >= 5) {
                    alog.write(websocketpp::log::alevel::app, "hello!");
                    std::memcpy(&s->screen_width, &buffer[1], 2);
                    std::memcpy(&s->screen_height, &buffer[3], 2);

                    if(!s->received_hello) s->received_hello = true;

                    s->type = network::session_type::player;

                    if(s->screen_width == 0 || s->screen_height == 0) {
                        alog.write(websocketpp::log::alevel::app, "screen 0x0, closing the connection");
                        m_server.close(hdl, websocketpp::close::status::normal, "");
                    }
                } else {
                    alog.write(websocketpp::log::alevel::app, "hello packet is too short... closing the connection.");
                    m_server.close(hdl, websocketpp::close::status::normal, "");
                }
                break;
            }

            case network::opcode::hello_bot:
            {
                if(buffer.length() >= 5) {
                    alog.write(websocketpp::log::alevel::app, "hello bot!");
                    std::memcpy(&s->screen_width, &buffer[1], 2);
                    std::memcpy(&s->screen_height, &buffer[3], 2);

                    if(!s->received_hello) s->received_hello = true;

                    s->type = network::session_type::bot;
                    
                    if(s->screen_width == 0 || s->screen_height == 0) {
                        alog.write(websocketpp::log::alevel::app, "screen 0x0, closing the connection");
                        m_server.close(hdl, websocketpp::close::status::normal, "");
                    }
                } else {
                    alog.write(websocketpp::log::alevel::app, "hello packet is too short... closing the connection.");
                    m_server.close(hdl, websocketpp::close::status::normal, "");
                }
                break;
            }

            case network::opcode::hello_debug:
            {
                if(buffer.length() >= 5) {
                    alog.write(websocketpp::log::alevel::app, "hello debug!");
                    std::memcpy(&s->screen_width, &buffer[1], 2);
                    std::memcpy(&s->screen_height, &buffer[3], 2);

                    int offset = 5;
                    try {
                        std::string pass = utils::getString(buffer, offset);
                        if(pass == "plsadmin")
                            s->type = network::session_type:dev;
                        else
                            s->type = network::session_type::player;
                    } catch(std::out_of_range &e) {
                        alog.write(websocketpp::log::alevel::app, "invalid debug packet, closing the connection");
                        return;
                    }

                    if(!s->received_hello) s->received_hello = true;

                    if(s->screen_width == 0 || s->screen_height == 0) {
                        alog.write(websocketpp::log::alevel::app, "screen 0x0, closing the connection");
                        m_server.close(hdl, websocketpp::close::status::normal, "");
                    }
                } else {
                    alog.write(websocketpp::log::alevel::app, "hello packet is too short... closing the connection.");
                    m_server.close(hdl, websocketpp::close::status::normal, "");
                }
                break;
            }

            case network::opcode::enter_game:
            {
                if(buffer.size() < 8) {
                    alog.write(websocketpp::log::alevel::app, "invalid enter game packet! (too short), closing the connection");
                    m_server.close(hdl, websocketpp::close::status::normal, "");
                    
                    return;
                }

                if(s->did_enter_game() || !s->did_send_hello()) {
                    alog.write(websocketpp::log::alevel::app, "not the right time to enter the game! closing the connection");
                    m_server.close(hdl, websocketpp::close::status::normal, "");
                    
                    return;
                }
                
                auto p = std::make_shared<game::player>();
                p->session = std::weak_ptr(s);
                s->player = p;

                switch(s->type) {
                    case network::session_type::player:
                    s->player->is_player = true;
                    break;

                    case network::session_type::bot:
                    s->player->is_bot = true;
                    break;

                    case network::session_type::dev:
                    s->player->is_dev = true;
                    break;
                }

                p->id = game_world.add_player(p);

                uint8_t data[3];
                data[0] = network::opcode::entered_game;
                std::memcpy(&data[1], &p->id, 2);

                m_server.send(hdl, data, sizeof(data), websocketpp::frame::opcode::binary);

                try {
                    int offset = 1;
                    
                    p->red = buffer[offset++];
                    p->green = buffer[offset++];
                    p->blue = buffer[offset++];
                    
                    p->nick = utils::getString(buffer, offset);
                    p->room_id = utils::getString(buffer, offset);

                    if(p->room_id == "") p->room_id = "lobby";

                    dispatch_entered_game(p->id, p->room_id);
                    
                } catch(std::out_of_range &e) {
                    alog.write(websocketpp::log::alevel::app, "invalid enter game packet! closing the connection");
                    m_server.close(hdl, websocketpp::close::status::normal, "");
                }
                
                break;
            }

            case network::opcode::leave_game:
            {
                if(!s->did_enter_game()) {
                    alog.write(websocketpp::log::alevel::app, "can't leave the game without entering it first");
                    return;
                }

                s->player->deletion_reason = 0x03;
                game_world.mark_for_deletion(s->player->id);
                dispatch_left_game(p->id, p->room_id);
                break;
            }

            case network::opcode::resize:
            {
                if(buffer.length() >= 5) {
                    alog.write(websocketpp::log::alevel::app, "resize!");
                    std::memcpy(&s->screen_width, &buffer[1], 2);
                    std::memcpy(&s->screen_height, &buffer[3], 2);

                    if(s->screen_width == 0 || s->screen_height == 0) {
                        alog.write(websocketpp::log::alevel::app, "screen 0x0, closing the connection");
                        m_server.close(hdl, websocketpp::close::status::normal, "");
                    }
                } else {
                    alog.write(websocketpp::log::alevel::app, "resize packet is too short... closing the connection.");
                    m_server.close(hdl, websocketpp::close::status::normal, "");
                }
                break;
            }

            case network::opcode::input:
            {
                if(!s->did_enter_game()) {
                    alog.write(websocketpp::log::alevel::app, "can't move your cursor without entering the game first");
                    return;
                }
                
                if(buffer.length() >= 5) {
                    std::memcpy(&s->player->x, &buffer[1], 2);
                    std::memcpy(&s->player->y, &buffer[3], 2);
                } else {
                    alog.write(websocketpp::log::alevel::app, "cursor packet is too short... closing the connection.");
                    m_server.close(hdl, websocketpp::close::status::normal, "");
                }
                
                break;
            }

            case network::opcode::nick:
            {
                if(!s->did_enter_game()) {
                    alog.write(websocketpp::log::alevel::app, "can't change your nick without entering the game first");
                    return;
                }

                if(buffer.size() > 1 + 2 && buffer.size() < 1 + 15 + 3) {
                    try {
                        int offset = 1;
                        p->nick = utils::getString(buffer, offset);
                        dispatch_nick(p->id, p->nick, p->room_id);
                    } catch(std::out_of_range &e) {
                        alog.write(websocketpp::log::alevel::app, "invalid nick packet! closing the connection");
                        m_server.close(hdl, websocketpp::close::status::normal, "");
                    }
                } else {
                    alog.write(websocketpp::log::alevel::app, "invalid nick packet (too short).");
                    m_server.close(hdl, websocketpp::close::status::normal, "");
                }
                
                break;
            }

            case network::opcode::color:
            {
                if(!s->did_enter_game()) {
                    alog.write(websocketpp::log::alevel::app, "can't change your color without entering the game first");
                    return;
                }

                if(buffer.size() == 1 + 3) {
                    int offset = 1;
                    s->player->red = buffer[offset++];
                    s->player->green = buffer[offset++];
                    s->player->blue = buffer[offset++];

                    dispatch_color(p->id, s->player->red, s->player->green, s->player->blue, s->room_id);
                }
                
                break;
            }

            case network::opcode::chat:
            {
                if(!s->did_enter_game()) {
                    alog.write(websocketpp::log::alevel::app, "can't chat without entering the game first");
                    return;
                }

                if(buffer.size() > 1 + 500 + 3) {
                    alog.write(websocketpp::log::alevel::app, "message too long!");
                    return;
                }

                try {
                    std::string chat_message = utils::getString(buffer, offset);
                    if(chat_message == "") {
                        alog.write(websocketpp::log::alevel::app, "null message!");
                        return;
                    }


                    dispatch_message(chat_message, s->player->id, s->player->nick, s->player->room_id);

                    game_world.add_message(s->player->room_id, {
                        chat_message,
                        s->player->nick,
                        s->player->hue,
                        s->player->id,
                        std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count()
                    });
                } catch(std::out_of_range &e) {
                    alog.write(websocketpp::log::alevel::app, "Invalid message! closing connection");
                    m_server.close(hdl, websocketpp::close::status::normal, "");
                }
                
                break;
            }

            case network::opcode::change_room:
            {
                if(!s->did_enter_game()) {
                    alog.write(websocketpp::log::alevel::app, "can't change your room without entering the game first");
                    return;
                }

                try {
                    int offset = 1;
                    std::string room_id = utils::getString(buffer, offset);
                    if(room_id == "") {
                        alog.write(websocketpp::log::alevel::app, "null room id! closing connection");
                        m_server.close(hdl, websocketpp::close::status::normal, "");
                        return;
                    } else {
                        s->player->room_id = room_id;
                    }
                } 
                catch(std::out_of_range &e) {
                    alog.write(websocketpp::log::alevel::app, "Invalid message! closing connection");
                    m_server.close(hdl, websocketpp::close::status::normal, "");
                }
                
                break;
            }

            case network::opcode::update_room:
            {
                if(!s->did_enter_game()) {
                    alog.write(websocketpp::log::alevel::app, "can't update your room without entering the game first");
                    return;
                }
                
                break;
            }

            case network::opcode::delete_room:
            {
                if(!s->did_enter_game()) {
                    alog.write(websocketpp::log::alevel::app, "can't delete your room without entering the game first");
                    return;
                }
                break;
            }

            case network::opcode::note:
            {
                if(!s->did_enter_game()) {
                    alog.write(websocketpp::log::alevel::app, "can't play notes without entering the game first");
                    return;
                }
                
                break;
            }

            case network::opcode::debug_ban:
            {
                if(!s->did_enter_game()) {
                    alog.write(websocketpp::log::alevel::app, "can't ban without entering the game first");
                    return;
                }
                
                break;
            }

            case network::opcode::debug_mute:
            {
                if(!s->did_enter_game()) {
                    alog.write(websocketpp::log::alevel::app, "can't mute without entering the game first");
                    return;
                }
                break;
            }

            case network::opcode::debug_kick:
            {
                if(!s->did_enter_game()) {
                    alog.write(websocketpp::log::alevel::app, "can't kick without entering the game first");
                    return;
                }
                
                break;
            }
            
            default:
            {
                
                break;
            }
        }
    }

    void on_open(connection_hdl hdl) {
        m_sessions[hdl] = std::make_shared<network::session>(hdl);
    }

    void on_close(connection_hdl hdl) {
        auto it = m_sessions.find(hdl);
        if (it == m_sessions.end()) {
            return;
        }
        m_sessions.erase(it);
    }


    void on_message(connection_hdl hdl, message_ptr msg) {
        if(msg->get_opcode() == websocketpp::frame::opcode::binary) {
            std::string payload = msg->get_payload();
            process_message(payload, hdl);
        }
    }

    void run(uint16_t port) {
        m_server.listen(port);
        m_server.start_accept();
        m_server.run();
    }

    void shutdown() {
        m_server.stop_listening();
        m_sessions.clear();
    }

private:
    server m_server;

    websocketpp::log::logger<websocketpp::log::alevel::log> alog;
    websocketpp::log::logger<websocketpp::log::elevel::log> elog;
    
    game::game_manager game_world;

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

    std::unordered_map<connection_hdl, std::shared_ptr<network::session>, connection_hdl_hash, connection_hdl_equal> m_sessions;

    void dispatch_entered_game(uint16_t id, std::string &room_id) {
        uint8_t buffer[4];
        buffer[0] = network::opcode::events;
        buffer[1] = network::event::entered_game;
        std::memcpy(&buffer[2], &id, 2);
        send_dispatch(buffer, 4, room_id);
    }

    void dispatch_left_game(uint16_t id, std::string &room_id) {
        uint8_t buffer[4];
        buffer[0] = network::opcode::events;
        buffer[1] = network::event::left_game;
        std::memcpy(&buffer[2], &id, 2);
        send_dispatch(buffer, 4, room_id);
    }

    void dispatch_message(const std::u16string &value, uint16_t id, std::u16string &nick, std::string &room_id) {
        const int size = 1 + 1 + 2 + 2 * nick.length() + 2 + 2 * value.length() + 2;
        std::vector<uint8_t> buffer(size);
        buffer[0] = network::opcode::events;
        int offset = 1;
        buffer[offset++] = network::event::sent_message;
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

    void dispatch_nick(uint16_t id, std::string &nick, std::string &room_id) {
        uint8_t buffer[1+1+2+nick.length()+1];
        buffer[0] = network::opcode::events;
        buffer[1] = network::event::updated_nick;
        std::memcpy(&buffer[2], &id, 2);
        std::memcpy(&buffer[4], nick.data(), nick.length());
        buffer[4+nick.length()] = 0x00;
        send_dispatch(buffer, 1+1+2+nick.length()+1, room_id);
    }

    void dispatch_color(uint16_t id, uint8_t red, uint8_t green, uint8_t blue, std::string &room_id) {
        uint8_t buffer[1+1+2+1+1+1];
        buffer[0] = network::opcode::events;
        buffer[1] = network::event::updated_color;
        std::memcpy(&buffer[2], &id, 2);
        buffer[4] = red;
        buffer[5] = green;
        buffer[6] = blue;

        send_dispatch(buffer, 1+1+2+1+1+1, room_id);
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
};


int main() {
    mpp_server wsServer;

    // this should fix the "Address already in use" exception
    auto shutdown = [&wsServer](int signum) {
        std::cout << "Signal " << signum << " received. Exiting cleanly." << std::endl;
        wsServer.shutdown();
        exit(signum);
    }

    signal(SIGINT, shutdown);
    signal(SIGTERM, shutdown);
    
    wsServer.start_loop();
    wsServer.run(8081);
    return 0;
}
