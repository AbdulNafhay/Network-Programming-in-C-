#include <atomic>
#include <iostream>
#include <string>
#include <thread>
#include "net.hpp"

int main(int argc, char** argv) {
    const char* host = (argc > 1) ? argv[1] : "127.0.0.1";
    const char* port = (argc > 2) ? argv[2] : "6000";

    try {
        net::Runtime runtime;
        net::Socket sock = net::tcp_connect(host, port);

        std::cout << "Connected to " << host << ":" << port
                  << ". Commands: /nick NAME, /who, /quit\n";

        std::atomic<bool> running{true};

        std::thread reader([&]() {
            char buf[1024];

            while (running) {
                long long n = net::recv_some(sock.get(), buf, sizeof(buf));

                if (n <= 0) {
                    running = false;
                    std::cout << "\n[Disconnected from server]\n" << std::flush;
                    break;
                }

                std::cout.write(buf, static_cast<std::streamsize>(n));
                std::cout << std::flush;
            }
        });

        std::string line;

        while (running && std::getline(std::cin, line)) {
            line += "\n";

            if (!net::send_all(sock.get(), line.data(), line.size())) {
                running = false;
                break;
            }

            if (line == "/quit\n" || line == "/quit\r\n") {
                running = false;
                break;
            }
        }

        running = false;
        net::shutdown_send(sock.get());

        if (reader.joinable()) {
            reader.join();
        }
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }

    return 0;
}
