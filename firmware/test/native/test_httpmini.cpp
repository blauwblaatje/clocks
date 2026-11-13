// Host test for the mini HTTP server, over real sockets on localhost.
// Build: g++ -std=c++17 -Wall -pthread -Ilib/httpmini test/native/test_httpmini.cpp lib/httpmini/httpmini.cpp -o /tmp/th && /tmp/th
#include "httpmini.h"
#include <arpa/inet.h>
#include <atomic>
#include <chrono>
#include <cstdio>
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

static std::string fetch(int port, const std::string& req) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_port = htons(port);
    inet_pton(AF_INET, "127.0.0.1", &a.sin_addr);
    if (connect(fd, (sockaddr*)&a, sizeof a) != 0) return "connect failed";
    // send in two pieces, as a slow client would
    size_t half = req.size() / 2;
    send(fd, req.data(), half, 0);
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    send(fd, req.data() + half, req.size() - half, 0);
    std::string out;
    char buf[2048];
    ssize_t n;
    while ((n = recv(fd, buf, sizeof buf, 0)) > 0) out.append(buf, n);
    close(fd);
    return out;
}

int main() {
    http::Server s;
    std::string err, lastBody, lastPath;
    int port = 18000 + (getpid() % 1000);
    bool ok = s.start(port, [&](const http::Request& r) {
        lastPath = r.path;
        http::Response resp;
        if (r.method == "POST" && r.path == "/save") {
            lastBody = r.body;
            resp.status = 303;
            resp.location = "/?saved=1";
            return resp;
        }
        if (r.path == "/") {
            resp.body = std::string(20000, 'x') + "<p>" + r.query + "</p>"; // bigger than one send
            return resp;
        }
        resp.status = 404;
        resp.body = "no";
        return resp;
    }, err);
    CHECK(ok);
    // Streamed upload on /update
    std::string got, aborted, refuse;
    size_t announced = 0;
    int chunks = 0;
    http::Upload up;
    up.path = "/update";
    up.begin = [&](size_t len) {
        announced = len;
        got.clear();
        chunks = 0;
        return refuse;
    };
    up.chunk = [&](const char* d, size_t n) {
        got.append(d, n);
        chunks++;
        return got.find("STOP") != std::string::npos ? std::string("bad data") : std::string();
    };
    up.end = [&] {
        http::Response r;
        r.contentType = "text/plain";
        r.body = "installed " + std::to_string(got.size());
        return r;
    };
    up.abort = [&](const std::string& why) { aborted = why; };
    s.setUpload(up);
    std::atomic<bool> run{true};
    std::thread loop([&] {
        uint32_t t = 0;
        while (run) {
            s.poll(t += 2);
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    });
    std::string r1 = fetch(port, "GET /?saved=1 HTTP/1.1\r\nHost: x\r\n\r\n");
    CHECK(r1.compare(0, 15, "HTTP/1.0 200 OK") == 0);
    CHECK(r1.find("Content-Length: 20014") != std::string::npos);
    CHECK(r1.size() > 20000 && r1.find("<p>saved=1</p>") != std::string::npos);
    std::string body = "role=jt&net0.ssid=Gym+Zuid";
    std::string r2 = fetch(port, "POST /save HTTP/1.1\r\nHost: x\r\nContent-Type: application/x-www-form-urlencoded\r\n"
                                 "Content-Length: " + std::to_string(body.size()) + "\r\n\r\n" + body);
    CHECK(r2.find("303 See Other") != std::string::npos && r2.find("Location: /?saved=1") != std::string::npos);
    CHECK(lastBody == body);
    std::string r3 = fetch(port, "GET /nope HTTP/1.1\r\n\r\n");
    CHECK(r3.find("404") != std::string::npos && lastPath == "/nope");
    // upload: 300 KB, far above the 16 KB request limit
    std::string fw;
    for (int i = 0; i < 300000; i++) fw += (char)('a' + i % 23);
    std::string r4 = fetch(port, "POST /update HTTP/1.1\r\nHost: x\r\nContent-Type: application/octet-stream\r\n"
                                 "Content-Length: " + std::to_string(fw.size()) + "\r\n\r\n" + fw);
    CHECK(r4.find("200 OK") != std::string::npos && r4.find("installed 300000") != std::string::npos);
    CHECK(got == fw && announced == fw.size() && chunks > 1 && aborted.empty());
    // refused at the start
    refuse = "Not while a clock runs";
    std::string r5 = fetch(port, "POST /update HTTP/1.1\r\nContent-Length: 5\r\n\r\nabcde");
    CHECK(r5.find("409") != std::string::npos && r5.find("Not while a clock runs") != std::string::npos);
    refuse.clear();
    // stopped by the receiver
    std::string r6 = fetch(port, "POST /update HTTP/1.1\r\nContent-Length: 10\r\n\r\nabSTOPdefg");
    CHECK(r6.find("400") != std::string::npos && r6.find("bad data") != std::string::npos);
    // no Content-Length
    std::string r7 = fetch(port, "POST /update HTTP/1.1\r\n\r\n");
    CHECK(r7.find("411") != std::string::npos);
    // the client goes away halfway
    {
        int fd = socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in a{};
        a.sin_family = AF_INET;
        a.sin_port = htons(port);
        inet_pton(AF_INET, "127.0.0.1", &a.sin_addr);
        connect(fd, (sockaddr*)&a, sizeof a);
        std::string h = "POST /update HTTP/1.1\r\nContent-Length: 100000\r\n\r\n" + std::string(5000, 'x');
        send(fd, h.data(), h.size(), 0);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        close(fd);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        CHECK(aborted == "Upload interrupted" && got.size() == 5000);
    }
    // normal pages still work after uploads
    std::string r8 = fetch(port, "GET /nope HTTP/1.1\r\n\r\n");
    CHECK(r8.find("404") != std::string::npos);
    run = false;
    loop.join();
    s.stop();
    if (failures) {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("all httpmini tests passed\n");
    return 0;
}
