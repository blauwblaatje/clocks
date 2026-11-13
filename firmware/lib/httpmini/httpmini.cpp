#include "httpmini.h"

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

namespace http {

static const size_t kMaxRequest = 16384;
static const uint32_t kClientTimeoutMs = 3000; // without data (10 s during an upload)
static const uint32_t kUploadTimeoutMs = 10000;

static void nonBlocking(int fd) { fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK); }

bool Server::start(int port, Handler h, std::string& err) {
    stop();
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        err = strerror(errno);
        return false;
    }
    int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_port = htons(port);
    a.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(fd, (sockaddr*)&a, sizeof a) != 0 || listen(fd, 2) != 0) {
        err = strerror(errno);
        ::close(fd);
        return false;
    }
    nonBlocking(fd);
    listen_ = fd;
    port_ = port;
    h_ = h;
    return true;
}

void Server::stop() {
    closeClient();
    if (listen_ >= 0) ::close(listen_);
    listen_ = -1;
}

void Server::closeClient() {
    if (uploading_ && upload_.abort) upload_.abort("Upload interrupted");
    uploading_ = false;
    if (client_ >= 0) ::close(client_);
    client_ = -1;
    in_.clear();
    out_.clear();
}

bool Server::parse(Request& r) const {
    size_t end = in_.find("\r\n\r\n");
    if (end == std::string::npos) return false;
    size_t sp1 = in_.find(' '), sp2 = in_.find(' ', sp1 + 1);
    if (sp1 == std::string::npos || sp2 == std::string::npos || sp2 > end) return false;
    r.method = in_.substr(0, sp1);
    std::string target = in_.substr(sp1 + 1, sp2 - sp1 - 1);
    size_t q = target.find('?');
    r.path = target.substr(0, q);
    r.query = q == std::string::npos ? "" : target.substr(q + 1);
    size_t len = 0;
    std::string head = in_.substr(0, end);
    for (size_t p = 0; (p = head.find("\r\n", p)) != std::string::npos;) {
        p += 2;
        if (strncasecmp(head.c_str() + p, "content-length:", 15) == 0) len = strtoul(head.c_str() + p + 15, nullptr, 10);
    }
    if (in_.size() < end + 4 + len) return false;
    r.body = in_.substr(end + 4, len);
    return true;
}

bool Server::startUpload() {
    if (upload_.path.empty()) return false;
    size_t end = in_.find("\r\n\r\n");
    if (end == std::string::npos) return false;
    if (in_.compare(0, 5, "POST ") != 0) return false;
    size_t sp2 = in_.find(' ', 5);
    std::string target = in_.substr(5, sp2 - 5);
    if (target.substr(0, target.find('?')) != upload_.path) return false;
    long len = -1;
    std::string head = in_.substr(0, end);
    for (size_t p = 0; (p = head.find("\r\n", p)) != std::string::npos;) {
        p += 2;
        if (strncasecmp(head.c_str() + p, "content-length:", 15) == 0) len = strtol(head.c_str() + p + 15, nullptr, 10);
    }
    Response r;
    r.contentType = "text/plain; charset=utf-8";
    if (len <= 0) {
        r.status = 411;
        r.body = "Content-Length missing";
        answer(r);
        return true;
    }
    std::string e = upload_.begin((size_t)len);
    if (!e.empty()) {
        r.status = 409;
        r.body = e;
        answer(r);
        return true;
    }
    uploading_ = true;
    remaining_ = (size_t)len;
    in_.erase(0, end + 4);
    return true;
}

void Server::answer(const Response& resp) {
    const char* reason = resp.status == 200   ? "OK"
                         : resp.status == 303 ? "See Other"
                         : resp.status == 404 ? "Not Found"
                         : resp.status == 409 ? "Conflict"
                                              : "Error";
    out_ = "HTTP/1.0 " + std::to_string(resp.status) + " " + reason + "\r\nContent-Type: " + resp.contentType +
           "\r\nContent-Length: " + std::to_string(resp.body.size()) +
           "\r\nCache-Control: no-store\r\nConnection: close\r\n";
    if (!resp.location.empty()) out_ += "Location: " + resp.location + "\r\n";
    out_ += "\r\n" + resp.body;
}

void Server::poll(uint32_t now) {
    if (listen_ < 0) return;
    if (client_ < 0) {
        int c = accept(listen_, nullptr, nullptr);
        if (c < 0) return;
        nonBlocking(c);
        client_ = c;
        clientSince_ = now;
    }
    if (now - clientSince_ > (uploading_ ? kUploadTimeoutMs : kClientTimeoutMs)) {
        closeClient();
        return;
    }
    if (out_.empty()) {
        char buf[1460];
        for (int reads = 0; reads < 32; reads++) { // a budget, so the screen keeps running
            ssize_t n = recv(client_, buf, sizeof buf, 0);
            if (n > 0) {
                clientSince_ = now;
                if (uploading_) {
                    size_t take = (size_t)n < remaining_ ? (size_t)n : remaining_;
                    std::string e = upload_.chunk(buf, take);
                    remaining_ -= take;
                    if (!e.empty()) {
                        uploading_ = false;
                        Response r;
                        r.status = 400;
                        r.contentType = "text/plain; charset=utf-8";
                        r.body = e;
                        answer(r);
                        break;
                    }
                    if (remaining_ == 0) {
                        uploading_ = false;
                        answer(upload_.end());
                        break;
                    }
                    continue;
                }
                in_.append(buf, n);
                if (startUpload()) {
                    if (!out_.empty()) break;
                    if (!in_.empty()) { // body bytes that came with the headers
                        std::string first;
                        first.swap(in_);
                        size_t take = first.size() < remaining_ ? first.size() : remaining_;
                        std::string e = upload_.chunk(first.data(), take);
                        remaining_ -= take;
                        if (!e.empty() || remaining_ == 0) {
                            uploading_ = false;
                            Response r;
                            if (e.empty()) r = upload_.end();
                            else {
                                r.status = 400;
                                r.contentType = "text/plain; charset=utf-8";
                                r.body = e;
                            }
                            answer(r);
                            break;
                        }
                    }
                    continue;
                }
                if (in_.size() > kMaxRequest) {
                    closeClient();
                    return;
                }
                continue;
            }
            if (n == 0 || (errno != EAGAIN && errno != EWOULDBLOCK)) {
                closeClient();
                return;
            }
            break;
        }
        if (out_.empty()) {
            if (uploading_) return;
            Request r;
            if (!parse(r)) return;
            answer(h_(r));
        }
    }
    while (!out_.empty()) {
        ssize_t w = ::send(client_, out_.data(), out_.size(), MSG_NOSIGNAL);
        if (w > 0) {
            out_.erase(0, w);
            continue;
        }
        if (w < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return; // rest on the next poll
        break;
    }
    closeClient();
}

} // namespace http
