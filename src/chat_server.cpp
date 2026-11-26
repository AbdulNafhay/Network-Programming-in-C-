// chat_server.cpp - multi-user chat server
// One thread, select(), up to 60 clients. Uses net.hpp only (no #ifdef here).
//
// Protocol: text lines ending in \n (\r\n also accepted), max 1024 bytes.
//   client -> server:  <text> | /nick NAME | /who | /quit
//   server -> client:  "Name: text" | "Name joined" | "Name left" |
//                      "old is now new" | "Users: ..." | "Error: ..."
#include <cctype>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <vector>
#include "net.hpp"

constexpr size_t MAX_CLIENTS = 60;   // R1 (also fits the Windows FD_SETSIZE of 64)
constexpr size_t MAX_LINE    = 1024; // longest accepted line
constexpr size_t MAX_NAME    = 16;   // R5

struct Client {
    net::Socket sock;
    std::string name;
    std::string inbuf;   // bytes received but not yet a full line (R9)
    std::string addr;
};

class ChatServer {
public:
    explicit ChatServer(const char* port) : listener_(net::tcp_listen(port)) {
        std::cout << "Chat server on port " << port << " (max " << MAX_CLIENTS
                  << " clients). Press Ctrl+C to stop." << std::endl;
    }

    void run() {
        for (;;) {
            fd_set readset;
            FD_ZERO(&readset);
            FD_SET(listener_.get(), &readset);
            socket_t maxfd = listener_.get();
            for (auto& kv : clients_) {
                FD_SET(kv.first, &readset);
                if (kv.first > maxfd) maxfd = kv.first;
            }

            int ready = select(static_cast<int>(maxfd) + 1, &readset, nullptr, nullptr, nullptr);
            if (ready < 0) {
                std::cerr << "select failed: " << net::error_text(net::last_error()) << "\n";
                continue;
            }

            if (FD_ISSET(listener_.get(), &readset)) accept_new();

            // Collect first: handling a client must not change the map we loop over.
            std::vector<socket_t> readable;
            for (auto& kv : clients_)
                if (FD_ISSET(kv.first, &readset)) readable.push_back(kv.first);

            for (socket_t fd : readable) {
                auto it = clients_.find(fd);
                if (it == clients_.end() || dead_.count(fd)) continue;
                read_from(it->second, fd);
            }

            remove_dead();
        }
    }

private:
    net::Socket listener_;
    std::map<socket_t, Client> clients_;  // ordered by socket number
    std::set<socket_t> dead_;             // clients to remove after this round
    int next_id_ = 0;

    // ---- helpers ----------------------------------------------------------
    static bool valid_name(const std::string& n) {
        if (n.empty() || n.size() > MAX_NAME) return false;
        for (unsigned char c : n)
            if (!(std::isalnum(c) || c == '_')) return false;
        return true;
    }

    static std::string lower(std::string s) {
        for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return s;
    }

    bool name_taken(const std::string& n, socket_t except) const {
        for (auto& kv : clients_)
            if (kv.first != except && lower(kv.second.name) == lower(n)) return true;
        return false;
    }

    std::string make_default_name() {
        std::string n;
        do { n = "user" + std::to_string(++next_id_); } while (name_taken(n, INVALID_SOCK));
        return n;
    }

    // Send one line to one client. A failed send marks the client as dead.
    void send_to(socket_t fd, const std::string& line) {
        auto it = clients_.find(fd);
        if (it == clients_.end()) return;
        std::string data = line + "\n";
        if (!net::send_all(fd, data.data(), data.size())) dead_.insert(fd);
    }

    void broadcast(const std::string& line, socket_t except = INVALID_SOCK) {
        for (auto& kv : clients_)
            if (kv.first != except && !dead_.count(kv.first)) send_to(kv.first, line);
    }

    // ---- events -----------------------------------------------------------
    void accept_new() {
        sockaddr_storage peer{};
        sock_len peer_len = sizeof peer;
        net::Socket s(accept(listener_.get(), reinterpret_cast<sockaddr*>(&peer), &peer_len));
        if (!s.valid()) return;

        if (clients_.size() >= MAX_CLIENTS) {
            const std::string msg = "Error: server is full\n";
            net::send_all(s.get(), msg.data(), msg.size());
            return;  // Socket destructor closes it
        }

        socket_t fd = s.get();
        Client c;
        c.name = make_default_name();
        c.addr = net::peer_name(reinterpret_cast<sockaddr*>(&peer), peer_len);
        c.sock = std::move(s);
        std::string name = c.name;
        clients_.emplace(fd, std::move(c));

        std::cout << name << " connected from " << clients_[fd].addr
                  << " (" << clients_.size() << " online)" << std::endl;

        send_to(fd, "Welcome, " + name + "! Commands: /nick NAME, /who, /quit");
        broadcast(name + " joined", fd);  // R8
    }

    void read_from(Client& c, socket_t fd) {
        char buf[2048];
        long long n = net::recv_some(fd, buf, sizeof buf);
        if (n <= 0) { dead_.insert(fd); return; }  // closed or error

        c.inbuf.append(buf, static_cast<size_t>(n));

        // R9: handle every complete line, keep the incomplete rest in inbuf.
        size_t pos;
        while (!dead_.count(fd) && (pos = c.inbuf.find('\n')) != std::string::npos) {
            std::string line = c.inbuf.substr(0, pos);
            c.inbuf.erase(0, pos + 1);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.size() > MAX_LINE) { send_to(fd, "Error: line too long"); continue; }
            handle_line(c, fd, line);
        }

        // A "line" that never ends: protect the server's memory.
        if (!dead_.count(fd) && c.inbuf.size() > MAX_LINE) {
            send_to(fd, "Error: line too long");
            dead_.insert(fd);
        }
    }

    void handle_line(Client& c, socket_t fd, const std::string& line) {
        if (line.empty()) return;

        if (line[0] != '/') {                       // R4: plain chat text
            broadcast(c.name + ": " + line, fd);
            return;
        }

        // Split "/cmd rest"
        size_t sp = line.find(' ');
        std::string cmd = lower(line.substr(0, sp));
        std::string arg;
        if (sp != std::string::npos) {
            size_t start = line.find_first_not_of(' ', sp);
            if (start != std::string::npos) {
                size_t end = line.find_last_not_of(' ');
                arg = line.substr(start, end - start + 1);
            }
        }

        if (cmd == "/nick") {                       // R5
            if (!valid_name(arg)) {
                send_to(fd, "Error: name must be 1-16 letters, digits or _");
            } else if (name_taken(arg, fd)) {
                send_to(fd, "Error: that name is in use");
            } else if (arg == c.name) {
                send_to(fd, "Error: that is already your name");
            } else {
                std::string old = c.name;
                c.name = arg;
                broadcast(old + " is now " + arg);  // R8 (also tells the sender)
            }
        } else if (cmd == "/who") {                 // R6
            std::string list = "Users:";
            for (auto& kv : clients_) list += " " + kv.second.name;
            send_to(fd, list);
        } else if (cmd == "/quit") {                // R7
            send_to(fd, "Goodbye!");
            dead_.insert(fd);
        } else {
            send_to(fd, "Error: unknown command");
        }
    }

    // Remove closed clients and tell the others (R8).
    void remove_dead() {
        while (!dead_.empty()) {
            std::set<socket_t> batch;
            batch.swap(dead_);  // broadcast below may add new dead clients
            for (socket_t fd : batch) {
                auto it = clients_.find(fd);
                if (it == clients_.end()) continue;
                std::string name = it->second.name;
                clients_.erase(it);  // Socket destructor closes the socket
                std::cout << name << " left (" << clients_.size() << " online)" << std::endl;
                broadcast(name + " left");
            }
        }
    }
};

int main(int argc, char** argv) {
    const char* port = (argc > 1) ? argv[1] : "6000";
    try {
        net::Runtime runtime;
        ChatServer server(port);
        server.run();
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
