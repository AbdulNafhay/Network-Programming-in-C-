// daytime_server.cpp - simple daytime server (for bonus crash demo)
#include <cstdio>
#include <cstring>
#include <ctime>
#include "net.hpp"

int main(int argc, char** argv) {
    unsigned short port = (argc > 1) ? static_cast<unsigned short>(atoi(argv[1])) : 1313;
    try {
        net::Runtime runtime;
        char port_str[16];
        snprintf(port_str, sizeof port_str, "%u", port);
        net::Socket listener = net::tcp_listen(port_str);
        std::printf("Daytime server on port %u. Press Ctrl+C to stop.\n", port);

        for (;;) {
            net::Socket client(accept(listener.get(), nullptr, nullptr));
            if (!client.valid()) continue;

            std::time_t now = std::time(nullptr);
            std::tm local = net::local_time(now);
            char text[64];
            std::strftime(text, sizeof text, "%a %b %d %H:%M:%S %Y\r\n", &local);

            // send() directly (not send_all / net:: wrapper) so a broken pipe can surface
            send(client.get(), text, static_cast<int>(std::strlen(text)), 0);
        }
    } catch (const std::exception& e) {
        std::fprintf(stderr, "Error: %s\n", e.what());
        return 1;
    }
}