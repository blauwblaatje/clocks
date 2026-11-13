// Plain HTTP GET on BSD sockets (Linux and lwIP), as a upd::Fetch: for
// http:// release addresses (a release server on the local network, the
// simulator). HTTPS on the board goes through esp_http_client instead.
// Non-blocking after the name lookup; HTTP/1.0, so no chunked answers.
#pragma once
#include "update.h"

namespace upd {

class HttpGet : public Fetch {
  public:
    HttpGet(const std::string& url, uint32_t (*now)(), uint32_t timeoutMs = 5000);
    ~HttpGet() override;
    int poll(std::string& chunk) override;
    long length() const override { return length_; }
    std::string error() const override { return err_; }

  private:
    int failWith(const std::string& e);
    bool parseHead();
    int fd_ = -1;
    uint32_t (*now_)();
    uint32_t deadline_, timeoutMs_;
    enum class Step : uint8_t { Connecting, Head, Body, Done, Failed } step_ = Step::Connecting;
    std::string req_, in_, err_;
    long length_ = -1, got_ = 0;
};

// "http://host:port/path" -> parts; false if not http://
bool splitUrl(const std::string& url, std::string& host, int& port, std::string& path);

} // namespace upd
