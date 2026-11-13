// Minimal WebSocket client over POSIX sockets (ws:// only, text frames), for
// the simulator. Reads the upgrade response's Set-Cookie and sends a Cookie
// header, as the device transport must (docs/protocol.md section 2).
#pragma once
#include <stdint.h>
#include <string>
#include <vector>

class WsConn {
  public:
    ~WsConn() { close(); }
    // Non-blocking connect and upgrade: startOpen() begins (false = failed at
    // once), then call pollOpen() from the main loop until it returns 1 (open)
    // or -1 (failed, err says why); 0 = still busy. It never blocks, except
    // for the name lookup of a host name (an IP address does not block).
    // setCookie gets the Set-Cookie header values joined with ", ".
    bool startOpen(const std::string& host, int port, const std::string& path, const std::string& cookieHeader,
                   uint32_t nowMs, std::string& err, int timeoutMs = 3000);
    int pollOpen(uint32_t nowMs, std::string& setCookie, std::string& err);
    bool opening() const { return opening_; }
    // Blocking connect and upgrade, at most timeoutMs (tools and tests).
    bool open(const std::string& host, int port, const std::string& path, const std::string& cookieHeader,
              std::string& setCookie, std::string& err, int timeoutMs = 1500);
    // Non-blocking read; appends complete text messages. False when closed.
    bool poll(std::vector<std::string>& messages);
    bool send(const std::string& text);
    void close();
    bool isOpen() const { return fd_ >= 0; }

  private:
    bool sendFrame(int opcode, const std::string& payload);
    int fd_ = -1;
    int pfd_ = -1; // socket while opening
    bool opening_ = false, connected_ = false;
    uint32_t deadline_ = 0;
    std::string req_;
    std::string in_, partial_;
};
