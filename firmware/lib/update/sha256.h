// SHA-256 (FIPS 180-4), small and portable: the update check runs it on the
// board and in host tests alike.
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string>

namespace upd {

class Sha256 {
  public:
    Sha256() { reset(); }
    void reset();
    void update(const uint8_t* data, size_t len);
    std::string hex(); // finishes; call reset() before reusing

  private:
    void block(const uint8_t* p);
    uint32_t h_[8];
    uint8_t buf_[64];
    size_t used_ = 0;
    uint64_t bytes_ = 0;
};

} // namespace upd
