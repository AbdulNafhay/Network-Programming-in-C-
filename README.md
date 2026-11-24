# Computer Networks — TCP Socket Programming

A multi-user TCP chat application developed in C++ for the Computer Networks assignment.

## Repository Structure

```text
Network-Programming-in-C-/
├── src/
│   ├── chat_client.cpp
│   └── www/
├── screenshots/
├── bonus/
└── report/
```

## Chat Client

The client connects to the server and uses two threads:
- **Reader thread:** receives messages from the server and prints them.
- **Main thread:** reads commands from the terminal and sends them to the server.

Default connection:
- Host: `127.0.0.1`
- Port: `6000`

Commands supported by the chat server include `/nick NAME`, `/who`, and `/quit`.

## Build

The client uses the shared `net.hpp` helper. Place that header in `src/` before compiling.

### Linux

```bash
cd src
g++ -std=c++17 -Wall -Wextra -pthread -o chat_client chat_client.cpp
./chat_client
```

### Windows (MinGW-w64)

```bash
cd src
g++ -std=c++17 -Wall -Wextra -o chat_client.exe chat_client.cpp -lws2_32
chat_client.exe
```

To connect to another host or port, pass them as arguments:

```bash
chat_client 192.168.1.10 6000
```

## Team Contributions

Add each member's name, assigned requirements, and source files here as the group completes integration.

## Testing Evidence

Store screenshots of the required local and cross-platform tests in `screenshots/`.

## Report

Place the final assignment report in `report/`.
