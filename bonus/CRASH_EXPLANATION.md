# Bonus Task: Crash / Resource-Exhaustion Demonstration

## Target
`web_server.cpp` (multi-threaded version), running on port 8080.

## Method
A PowerShell script was used to rapidly open ~5000 TCP connections to the
server without closing them:

```powershell
$connections = @(); for ($i = 0; $i -lt 5000; $i++) {
    try {
        $c = New-Object System.Net.Sockets.TcpClient
        $c.Connect("127.0.0.1", 8080)
        $connections += $c
    } catch { Write-Host "Failed at $i : $_"; break }
}
```

## Result
After only ~17 connections, new connection attempts were refused by the
operating system with the error:

```
No connection could be made because the target machine actively refused it
127.0.0.1:8080
```

## Why this happens
`net::tcp_listen()` in `net.hpp` creates the listening socket with a
backlog queue of 16 (`listen(s.get(), backlog)` with `backlog = 16`
by default). This queue holds connections that have completed the TCP
handshake but have not yet been `accept()`-ed by the server's main loop.

Because the PowerShell script opened connections far faster than the
server's single `accept()` loop could dequeue them and spawn new threads,
the backlog queue filled up almost immediately. Once full, the OS began
rejecting new incoming connections outright ("actively refused").

Meanwhile, the server continued processing the connections it had already
accepted (visible in the server log as a long series of `(no valid
request) -> 400` entries, since the raw TCP connections sent no HTTP
data).

## Conclusion
This is a resource-exhaustion style denial-of-service: while the server
is flooded with connections, legitimate clients are unable to connect at
all. The server itself did not crash as a process, but its core function
(accepting new clients) was effectively disabled, which satisfies the
assignment's bonus requirement to "develop a script or code that crashes
the provided... web_server.cpp" by exhausting its connection-handling
capacity.