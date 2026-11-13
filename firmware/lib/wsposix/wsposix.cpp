#include "wsposix.h"

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <strings.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/select.h>
#include <random>
#include <sys/socket.h>
#include <unistd.h>

static std::string base64(const unsigned char* d, size_t n) {
    static const char* t = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string o;
    for (size_t i = 0; i < n; i += 3) {
        uint32_t v = d[i] << 16 | (i + 1 < n ? d[i + 1] << 8 : 0) | (i + 2 < n ? d[i + 2] : 0);
        o += t[v >> 18 & 63];
        o += t[v >> 12 & 63];
        o += i + 1 < n ? t[v >> 6 & 63] : '=';
        o += i + 2 < n ? t[v & 63] : '=';
    }
    return o;
}

// Wait until fd is readable (or writable), at most ms. select() rather than
// poll(): lwIP on the ESP32 has select() but no poll.h.
static bool waitFd(int fd, bool write, int ms) {
    fd_set set;
    FD_ZERO(&set);
    FD_SET(fd, &set);
    timeval tv{ms / 1000, (ms % 1000) * 1000};
    int r = write ? select(fd + 1, nullptr, &set, nullptr, &tv) : select(fd + 1, &set, nullptr, nullptr, &tv);
    return r == 1;
}

static std::mt19937& rng() {
    static std::mt19937 r{std::random_device{}()};
    return r;
}

bool WsConn::startOpen(const std::string& host, int port, const std::string& path, const std::string& cookieHeader,
                       uint32_t nowMs, std::string& err, int timeoutMs) {
    close();
    addrinfo hints{}, *res = nullptr;
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    // NOTE: a host name blocks here while it is looked up; an IP address does not.
    if (getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &res) != 0 || !res) {
        err = "cannot resolve " + host;
        return false;
    }
    int fd = socket(res->ai_family, SOCK_STREAM, 0);
    if (fd < 0) {
        freeaddrinfo(res);
        err = strerror(errno);
        return false;
    }
    fcntl(fd, F_SETFL, O_NONBLOCK);
    int rc = connect(fd, res->ai_addr, res->ai_addrlen);
    freeaddrinfo(res);
    if (rc != 0 && errno != EINPROGRESS) {
        err = strerror(errno);
        ::close(fd);
        return false;
    }
    int one = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
    unsigned char key[16];
    for (auto& b : key) b = (unsigned char)rng()();
    req_ = "GET " + path + " HTTP/1.1\r\nHost: " + host + ":" + std::to_string(port) +
           "\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: " + base64(key, 16) +
           "\r\nSec-WebSocket-Version: 13\r\n";
    if (!cookieHeader.empty()) req_ += "Cookie: " + cookieHeader + "\r\n";
    req_ += "\r\n";
    pfd_ = fd;
    opening_ = true;
    connected_ = false;
    deadline_ = nowMs + (uint32_t)timeoutMs;
    in_.clear();
    return true;
}

int WsConn::pollOpen(uint32_t nowMs, std::string& setCookie, std::string& err) {
    if (!opening_) return fd_ >= 0 ? 1 : -1;
    auto fail = [&](const std::string& e) {
        err = e;
        ::close(pfd_);
        pfd_ = -1;
        opening_ = false;
        return -1;
    };
    if ((int32_t)(nowMs - deadline_) >= 0) return fail(connected_ ? "no upgrade response" : "connect timeout");
    if (!connected_) {
        if (!waitFd(pfd_, true, 0)) return 0; // still connecting
        int soerr = 0;
        socklen_t sl = sizeof soerr;
        getsockopt(pfd_, SOL_SOCKET, SO_ERROR, &soerr, &sl);
        if (soerr) return fail(strerror(soerr));
        connected_ = true;
    }
    // send the upgrade request (normally in one go; the rest on a later call)
    while (!req_.empty()) {
        ssize_t w = ::send(pfd_, req_.data(), req_.size(), MSG_NOSIGNAL);
        if (w > 0) {
            req_.erase(0, w);
            continue;
        }
        if (w < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return 0;
        return fail("send failed");
    }
    char buf[2048];
    for (;;) {
        ssize_t n = recv(pfd_, buf, sizeof buf, 0);
        if (n > 0) {
            in_.append(buf, n);
            continue;
        }
        if (n == 0) return fail("closed during upgrade");
        if (errno == EAGAIN || errno == EWOULDBLOCK) break;
        return fail(strerror(errno));
    }
    size_t hdrEnd = in_.find("\r\n\r\n");
    if (hdrEnd == std::string::npos) return 0;
    if (in_.compare(0, 12, "HTTP/1.1 101") != 0) return fail("upgrade refused: " + in_.substr(0, in_.find("\r\n")));
    setCookie.clear();
    size_t pos = 0;
    while ((pos = in_.find("\r\n", pos)) != std::string::npos && pos < hdrEnd) {
        pos += 2;
        size_t eol = in_.find("\r\n", pos);
        std::string line = in_.substr(pos, eol - pos);
        if (strncasecmp(line.c_str(), "set-cookie:", 11) == 0) {
            if (!setCookie.empty()) setCookie += ", ";
            setCookie += line.substr(line.find_first_not_of(' ', 11));
        }
    }
    fd_ = pfd_;
    pfd_ = -1;
    opening_ = false;
    in_.erase(0, hdrEnd + 4); // frames that came right after the upgrade
    partial_.clear();
    return 1;
}

bool WsConn::open(const std::string& host, int port, const std::string& path, const std::string& cookieHeader,
                  std::string& setCookie, std::string& err, int timeoutMs) {
    // Blocking version on top of startOpen/pollOpen (tools and tests).
    uint32_t t = 0;
    if (!startOpen(host, port, path, cookieHeader, t, err, timeoutMs)) return false;
    for (;;) {
        int r = pollOpen(t, setCookie, err);
        if (r != 0) return r == 1;
        waitFd(pfd_, !connected_, 10);
        t += 10;
    }
}

bool WsConn::sendFrame(int opcode, const std::string& payload) {
    if (fd_ < 0) return false;
    std::string f;
    f += (char)(0x80 | opcode);
    size_t n = payload.size();
    if (n < 126) {
        f += (char)(0x80 | n);
    } else if (n < 65536) {
        f += (char)(0x80 | 126);
        f += (char)(n >> 8);
        f += (char)(n & 255);
    } else {
        f += (char)(0x80 | 127);
        for (int i = 7; i >= 0; i--) f += (char)((uint64_t)n >> (8 * i) & 255);
    }
    unsigned char mask[4];
    for (auto& b : mask) b = (unsigned char)rng()();
    f.append((const char*)mask, 4);
    for (size_t i = 0; i < n; i++) f += (char)(payload[i] ^ mask[i & 3]);
    size_t off = 0;
    while (off < f.size()) {
        ssize_t w = ::send(fd_, f.data() + off, f.size() - off, MSG_NOSIGNAL);
        if (w < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            waitFd(fd_, true, 100);
            continue;
        }
        if (w <= 0) return false;
        off += w;
    }
    return true;
}

bool WsConn::send(const std::string& text) { return sendFrame(1, text); }

bool WsConn::poll(std::vector<std::string>& messages) {
    if (fd_ < 0) return false;
    char buf[8192];
    for (;;) {
        ssize_t n = recv(fd_, buf, sizeof buf, 0);
        if (n > 0) {
            in_.append(buf, n);
            continue;
        }
        if (n == 0) {
            close();
            return false;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK) break;
        close();
        return false;
    }
    for (;;) {
        if (in_.size() < 2) break;
        const unsigned char* d = (const unsigned char*)in_.data();
        bool fin = d[0] & 0x80;
        int op = d[0] & 15;
        bool masked = d[1] & 0x80;
        uint64_t len = d[1] & 127;
        size_t h = 2;
        if (len == 126) {
            if (in_.size() < 4) break;
            len = d[2] << 8 | d[3];
            h = 4;
        } else if (len == 127) {
            if (in_.size() < 10) break;
            len = 0;
            for (int i = 0; i < 8; i++) len = len << 8 | d[2 + i];
            h = 10;
        }
        size_t mh = masked ? 4 : 0;
        if (in_.size() < h + mh + len) break;
        std::string payload = in_.substr(h + mh, len);
        if (masked) {
            for (size_t i = 0; i < len; i++) payload[i] ^= d[h + (i & 3)];
        }
        in_.erase(0, h + mh + len);
        if (op == 8) {
            close();
            return false;
        } else if (op == 9) {
            sendFrame(10, payload);
        } else if (op == 1 || op == 0) {
            partial_ += payload;
            if (fin) {
                messages.push_back(partial_);
                partial_.clear();
            }
        }
    }
    return true;
}

void WsConn::close() {
    if (pfd_ >= 0) ::close(pfd_);
    pfd_ = -1;
    opening_ = false;
    if (fd_ >= 0) {
        sendFrame(8, "");
        ::close(fd_);
        fd_ = -1;
    }
}
