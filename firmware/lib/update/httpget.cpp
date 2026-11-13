#include "httpget.h"

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

namespace upd {

bool splitUrl(const std::string& url, std::string& host, int& port, std::string& path) {
    if (url.compare(0, 7, "http://") != 0) return false;
    size_t hs = 7, pe = url.find('/', hs);
    std::string hp = url.substr(hs, pe == std::string::npos ? std::string::npos : pe - hs);
    path = pe == std::string::npos ? "/" : url.substr(pe);
    size_t c = hp.rfind(':');
    port = 80;
    if (c != std::string::npos) {
        port = atoi(hp.c_str() + c + 1);
        hp = hp.substr(0, c);
    }
    host = hp;
    return !host.empty() && port > 0 && port < 65536;
}

HttpGet::HttpGet(const std::string& url, uint32_t (*now)(), uint32_t timeoutMs) : now_(now), timeoutMs_(timeoutMs) {
    deadline_ = now_() + timeoutMs_;
    std::string host, path;
    int port;
    if (!splitUrl(url, host, port, path)) {
        failWith(url.compare(0, 8, "https://") == 0 ? "https is not available here" : "not an http:// address");
        return;
    }
    addrinfo hints{}, *res = nullptr;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &res) != 0 || !res) {
        failWith("unknown host " + host);
        return;
    }
    fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (fd_ < 0) {
        freeaddrinfo(res);
        failWith(strerror(errno));
        return;
    }
    fcntl(fd_, F_SETFL, fcntl(fd_, F_GETFL, 0) | O_NONBLOCK);
    int r = connect(fd_, res->ai_addr, res->ai_addrlen);
    freeaddrinfo(res);
    if (r != 0 && errno != EINPROGRESS) {
        failWith(strerror(errno));
        return;
    }
    req_ = "GET " + path + " HTTP/1.0\r\nHost: " + host + (port == 80 ? "" : ":" + std::to_string(port)) +
           "\r\nUser-Agent: CLOCK\r\nConnection: close\r\n\r\n";
}

HttpGet::~HttpGet() {
    if (fd_ >= 0) ::close(fd_);
}

int HttpGet::failWith(const std::string& e) {
    err_ = e;
    step_ = Step::Failed;
    if (fd_ >= 0) ::close(fd_);
    fd_ = -1;
    return -1;
}

bool HttpGet::parseHead() {
    size_t end = in_.find("\r\n\r\n");
    if (end == std::string::npos) return false;
    // "HTTP/1.1 200 OK"
    size_t sp = in_.find(' ');
    int status = sp == std::string::npos ? 0 : atoi(in_.c_str() + sp + 1);
    std::string head = in_.substr(0, end);
    for (size_t p = 0; (p = head.find("\r\n", p)) != std::string::npos;) {
        p += 2;
        if (strncasecmp(head.c_str() + p, "content-length:", 15) == 0) length_ = strtol(head.c_str() + p + 15, nullptr, 10);
        if (strncasecmp(head.c_str() + p, "transfer-encoding:", 18) == 0 &&
            head.find("chunked", p) < head.find("\r\n", p)) {
            failWith("chunked answers are not supported");
            return true;
        }
    }
    if (status != 200) {
        failWith("server answered " + std::to_string(status));
        return true;
    }
    in_.erase(0, end + 4);
    step_ = Step::Body;
    return true;
}

int HttpGet::poll(std::string& chunk) {
    chunk.clear();
    if (step_ == Step::Failed) return -1;
    if (step_ == Step::Done) return 0;
    if ((int32_t)(now_() - deadline_) >= 0) return failWith(step_ == Step::Connecting ? "connect timed out" : "timed out");
    if (step_ == Step::Connecting) {
        fd_set w;
        FD_ZERO(&w);
        FD_SET(fd_, &w);
        timeval tv{0, 0};
        if (select(fd_ + 1, nullptr, &w, nullptr, &tv) <= 0) return 1;
        int e = 0;
        socklen_t l = sizeof e;
        getsockopt(fd_, SOL_SOCKET, SO_ERROR, &e, &l);
        if (e) return failWith(strerror(e));
        if (::send(fd_, req_.data(), req_.size(), MSG_NOSIGNAL) != (ssize_t)req_.size()) return failWith("send failed");
        step_ = Step::Head;
        deadline_ = now_() + timeoutMs_;
    }
    char buf[4096];
    ssize_t n = recv(fd_, buf, sizeof buf, 0);
    if (n < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) return 1;
        return failWith(strerror(errno));
    }
    deadline_ = now_() + timeoutMs_; // a timeout without data, not for the whole download
    if (n == 0) { // closed by the server
        if (step_ == Step::Head) return failWith("no answer");
        if (!in_.empty()) {
            chunk.swap(in_);
            got_ += (long)chunk.size();
        }
        if (length_ >= 0 && got_ != length_) return failWith("connection closed early");
        step_ = Step::Done;
        ::close(fd_);
        fd_ = -1;
        return 0;
    }
    in_.append(buf, (size_t)n);
    if (step_ == Step::Head) {
        if (!parseHead()) {
            if (in_.size() > 8192) return failWith("answer header too long");
            return 1;
        }
        if (step_ == Step::Failed) return -1;
    }
    chunk.swap(in_);
    in_.clear();
    got_ += (long)chunk.size();
    if (length_ >= 0 && got_ >= length_) {
        if (got_ > length_) return failWith("more data than announced");
        step_ = Step::Done;
        ::close(fd_);
        fd_ = -1;
        return 0;
    }
    return 1;
}

} // namespace upd
