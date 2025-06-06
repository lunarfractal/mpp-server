#include <csignal>
#include <functional>

#include "network/network.hpp"
#include "utils/utils.hpp"
#include "game/game.hpp"

#define ASIO_STANDALONE

#include <websocketpp/config/asio_no_tls.hpp>
#include <websocketpp/server.hpp>


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
    }

    void start_loop() {}

    void process_message(std::string &payload, connection_hdl hdl) {

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
    game::game_manager game_world;

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


    std::unordered_map<connection_hdl, std::shared_ptr<network::session>, connection_hdl_hash, connection_hdl_equal> m_sessions;

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
