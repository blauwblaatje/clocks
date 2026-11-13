// Host test for the WebSocket client's non-blocking connect, with a fake
// server on localhost.
// Build: g++ -std=c++17 -Wall -pthread -Ilib/wsposix test/native/test_wsposix.cpp lib/wsposix/wsposix.cpp -o /tmp/tw && /tmp/tw
#include "wsposix.h"
#include <arpa/inet.h>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <netinet/in.h>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>

static int failures = 0;
#define CHECK(c)                                                              \
    do {                                                                      \
        if (!(c)) {                                                           \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c);          \
            failures++;                                                       \
        }                                                                     \
    } while (0)

using Clock = std::chrono::steady_clock;
static uint32_t ms() {
    static auto t0 = Clock::now();
    return (uint32_t)std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - t0).count();
}

// A server that accepts, waits `delay` ms, answers the upgrade and sends one text frame.
static int listenOn(int& port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    bind(fd, (sockaddr*)&a, sizeof a);
    socklen_t l = sizeof a;
    getsockname(fd, (sockaddr*)&a, &l);
    port = ntohs(a.sin_port);
    listen(fd, 1);
    return fd;
}

static void serve(int lfd, int delay, std::string* request) {
    int c = accept(lfd, nullptr, nullptr);
    char buf[4096];
    std::string req;
    while (req.find("\r\n\r\n") == std::string::npos) {
        ssize_t n = recv(c, buf, sizeof buf, 0);
        if (n <= 0) break;
        req.append(buf, n);
    }
    *request = req;
    std::this_thread::sleep_for(std::chrono::milliseconds(delay));
    std::string resp = "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                       "Set-Cookie: CRG_SCOREBOARD=node0abc.node0; Path=/; HttpOnly\r\n\r\n";
    std::string msg = "{\"Pong\":\"\"}";
    resp += (char)0x81;
    resp += (char)msg.size();
    resp += msg; // a frame right behind the upgrade response
    send(c, resp.data(), resp.size(), 0);
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    close(c);
}

int main() {
    // 1. A slow server: every pollOpen call returns at once; the upgrade and
    //    the frame behind it arrive.
    int port;
    int lfd = listenOn(port);
    std::string request;
    std::thread srv(serve, lfd, 400, &request);
    WsConn ws;
    std::string err, cookie;
    CHECK(ws.startOpen("127.0.0.1", port, "/WS/?source=test", "CRG_SCOREBOARD=old", ms(), err));
    CHECK(ws.opening());
    int r = 0;
    uint32_t longest = 0;
    while (r == 0) {
        uint32_t t0 = ms();
        r = ws.pollOpen(ms(), cookie, err);
        longest = std::max(longest, ms() - t0);
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    CHECK(r == 1 && ws.isOpen() && !ws.opening());
    CHECK(longest <= 5); // never blocks
    CHECK(cookie.find("CRG_SCOREBOARD=node0abc.node0") == 0);
    CHECK(request.find("GET /WS/?source=test HTTP/1.1") == 0 && request.find("Cookie: CRG_SCOREBOARD=old") != std::string::npos);
    std::vector<std::string> msgs;
    for (int i = 0; i < 20 && msgs.empty(); i++) {
        ws.poll(msgs);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    CHECK(msgs.size() == 1 && msgs[0] == "{\"Pong\":\"\"}");
    srv.join();
    close(lfd);
    ws.close();

    // 2. Nobody listening: fails quickly (connection refused).
    int lfd2 = listenOn(port);
    close(lfd2);
    r = 0;
    if (ws.startOpen("127.0.0.1", port, "/WS/", "", ms(), err)) {
        while ((r = ws.pollOpen(ms(), cookie, err)) == 0) std::this_thread::sleep_for(std::chrono::milliseconds(5));
    } else {
        r = -1;
    }
    CHECK(r == -1 && !err.empty());

    // 3. A server that accepts but never answers: the deadline ends it, and
    //    pollOpen still never blocks on the way.
    int lfd3 = listenOn(port);
    uint32_t start = ms();
    CHECK(ws.startOpen("127.0.0.1", port, "/WS/", "", ms(), err, 300));
    longest = 0;
    while (true) {
        uint32_t t0 = ms();
        r = ws.pollOpen(ms(), cookie, err);
        longest = std::max(longest, ms() - t0);
        if (r != 0) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    CHECK(r == -1 && err == "no upgrade response");
    CHECK(ms() - start >= 300 && ms() - start < 400 && longest <= 5);
    close(lfd3);

    if (failures) {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("all wsposix tests passed\n");
    return 0;
}
