// A very small HTTP/1.0 server on BSD sockets (Linux and lwIP), for the setup
// page. Non-blocking: call poll() from the main loop. One client at a time,
// request bodies up to 16 KB, connection closed after each response. One
// path can take a large POST body as a stream (the firmware upload).
#pragma once
#include <functional>
#include <stdint.h>
#include <string>

namespace http {

struct Request {
    std::string method, path, query, body;
};

struct Response {
    int status = 200;
    std::string contentType = "text/html; charset=utf-8";
    std::string body;
    std::string location; // for 303 redirects
};

using Handler = std::function<Response(const Request&)>;

// A POST to `path` is streamed: begin(Content-Length) ("" = accept, else the
// 409 answer), chunk() per piece ("" = go on, else stop with a 400 answer),
// end() once the whole body is in; abort() if the client goes away first.
struct Upload {
    std::string path;
    std::function<std::string(size_t length)> begin;
    std::function<std::string(const char* data, size_t len)> chunk;
    std::function<Response()> end;
    std::function<void(const std::string& why)> abort;
};

class Server {
  public:
    ~Server() { stop(); }
    bool start(int port, Handler h, std::string& err); // listens on all addresses
    void stop();
    bool running() const { return listen_ >= 0; }
    int port() const { return port_; }
    void poll(uint32_t nowMs); // accept, read, answer; never blocks
    void setUpload(const Upload& u) { upload_ = u; }

  private:
    void closeClient();
    bool parse(Request& r) const; // true once the whole request is in
    bool startUpload();           // headers in, and they are for the upload path
    void answer(const Response& resp);
    int listen_ = -1, client_ = -1, port_ = 0;
    uint32_t clientSince_ = 0;
    std::string in_, out_;
    Handler h_;
    Upload upload_;
    bool uploading_ = false;
    size_t remaining_ = 0;
};

} // namespace http
