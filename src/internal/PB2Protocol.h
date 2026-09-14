// PB Control Protocol v2. Independent of BLE and Arduino for host tests.
#ifndef PB2_PROTOCOL_H
#define PB2_PROTOCOL_H
#include <stdint.h>
#include <stddef.h>
#include <string>
#include <algorithm>
#include <cstring>

class PB2Protocol {
public:
    typedef bool (*Sha)(const uint8_t *, size_t, uint8_t *);
    typedef bool (*Hmac)(const uint8_t *, const uint8_t *, size_t, uint8_t *);
    typedef bool (*Random)(uint8_t *, size_t);
    PB2Protocol(Sha sha, Hmac hmac, Random random)
        : sha_(sha), hmac_(hmac), random_(random) {}
    ~PB2Protocol() { reset(); erase(secret_, sizeof(secret_)); }
    static void erase(uint8_t *p, size_t n) {
        volatile uint8_t *v = p;
        while (n--) *v++ = 0;
    }
    bool configure(const std::string &id, const std::string &code,
                   const std::string &model) {
        reset();
        configured_ = false;
        erase(secret_, sizeof(secret_));
        if (id.empty() || id.size() > 32) return false;
        for (char c : id)
            if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                  (c >= '0' && c <= '9'))) return false;
        if (model != "ESP32" && model != "ESP32-C3") return false;
        std::string normalized;
        for (char c : code) {
            if (c >= 'a' && c <= 'z') c -= 'a' - 'A';
            if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')))
                return false;
            normalized += c;
        }
        if (normalized.size() < 8 || normalized.size() > 16) return false;
        std::string material = "PB2|PAIR|" + normalized;
        bool ok = sha_(reinterpret_cast<const uint8_t *>(material.data()),
                       material.size(), secret_);
        erase(reinterpret_cast<uint8_t *>(&material[0]), material.size());
        erase(reinterpret_cast<uint8_t *>(&normalized[0]), normalized.size());
        if (!ok) return false;
        id_ = id; model_ = model; configured_ = true;
        return true;
    }
    void reset() {
        ready_ = false; pending_ = false; sequence_ = 0;
        appNonce_.clear(); boardNonce_.clear();
        erase(key_, sizeof(key_));
    }
    bool ready() const { return ready_; }
    // Input is one complete ASCII line, without newline.
    std::string line(const std::string &line) {
        if (!configured_) return "PB2:ERROR:CONFIG_INVALID";
        if (line.compare(0, 10, "PB2:HELLO:") == 0) {
            reset();
            appNonce_ = line.substr(10);
            if (!hexString(appNonce_, 32)) return "PB2:ERROR:PROTOCOL_ERROR";
            uint8_t nonce[16];
            if (!random_(nonce, sizeof(nonce))) return "PB2:ERROR:PROTOCOL_ERROR";
            boardNonce_ = hex(nonce, sizeof(nonce));
            const std::string material = "PB2|CAPS|" + id_ + "|" + model_ +
                "|2.0.0|" + appNonce_ + "|" + boardNonce_;
            std::string tag;
            if (!tagHex(secret_, material, tag)) return "PB2:ERROR:PROTOCOL_ERROR";
            pending_ = true;
            return "PB2:CAPS:2:" + id_ + ":" + model_ + ":2.0.0:" +
                   boardNonce_ + ":" + tag;
        }
        if (line.compare(0, 9, "PB2:AUTH:") == 0) {
            std::string expected;
            const std::string material = "PB2|AUTH|" + id_ + "|" +
                                        appNonce_ + "|" + boardNonce_;
            if (!pending_ || !hexString(line.substr(9), 64) ||
                !tagHex(secret_, material, expected) ||
                !equal(expected, line.substr(9))) {
                reset(); return "PB2:ERROR:AUTH_FAILED";
            }
            uint8_t nonce[16];
            if (!random_(nonce, sizeof(nonce))) {
                reset(); return "PB2:ERROR:PROTOCOL_ERROR";
            }
            const std::string session = hex(nonce, sizeof(nonce));
            const std::string context = id_ + "|" + appNonce_ + "|" +
                                       boardNonce_ + "|" + session;
            const std::string materialKey = "PB2|SESSION|" + context;
            std::string ack;
            if (!hmac_(secret_, reinterpret_cast<const uint8_t *>(materialKey.data()),
                       materialKey.size(), key_) ||
                !tagHex(secret_, "PB2|OK|" + context, ack)) {
                reset(); return "PB2:ERROR:PROTOCOL_ERROR";
            }
            ready_ = true; pending_ = false; sequence_ = 0;
            return "PB2:OK:" + session + ":" + ack;
        }
        // Legacy commands never update control state.
        return "";
    }
    // Returns 1 for joystick, 2 for stop, 0 for rejected input.
    int packet(const uint8_t *p, size_t n, uint8_t payload[6]) {
        if (!ready_ || (n != 17 && n != 23) ||
            p[0] != 0xAA || p[1] != 0x55 || p[2] != 2) return 0;
        if (!((p[3] == 1 && p[8] == 6 && n == 23) ||
              (p[3] == 2 && p[8] == 0 && n == 17))) return 0;
        uint32_t seq = (uint32_t(p[4]) << 24) | (uint32_t(p[5]) << 16) |
                       (uint32_t(p[6]) << 8) | p[7];
        if (seq == 0 || seq <= sequence_) return 0;
        uint8_t tag[32];
        if (!hmac_(key_, p, n - 8, tag)) return 0;
        uint8_t difference = 0;
        for (size_t i = 0; i < 8; ++i) difference |= tag[i] ^ p[n - 8 + i];
        erase(tag, sizeof(tag));
        if (difference) return 0;
        if (p[3] == 1) {
            for (size_t i = 0; i < 4; ++i)
                if (int8_t(p[9 + i]) < -100 || int8_t(p[9 + i]) > 100) return 0;
            std::memcpy(payload, p + 9, 6);
        }
        sequence_ = seq;
        return p[3];
    }
private:
    static bool hexString(const std::string &s, size_t length) {
        if (s.size() != length) return false;
        for (char c : s)
            if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
        return true;
    }
    static std::string hex(const uint8_t *p, size_t n) {
        const char *digits = "0123456789abcdef";
        std::string s;
        for (size_t i = 0; i < n; ++i) {
            s += digits[p[i] >> 4]; s += digits[p[i] & 15];
        }
        return s;
    }
    static bool equal(const std::string &a, const std::string &b) {
        if (a.size() != b.size()) return false;
        uint8_t difference = 0;
        for (size_t i = 0; i < a.size(); ++i) difference |= a[i] ^ b[i];
        return difference == 0;
    }
    bool tagHex(const uint8_t *key, const std::string &s, std::string &out) {
        uint8_t tag[32];
        if (!hmac_(key, reinterpret_cast<const uint8_t *>(s.data()), s.size(), tag))
            return false;
        out = hex(tag, sizeof(tag)); erase(tag, sizeof(tag)); return true;
    }
    Sha sha_; Hmac hmac_; Random random_;
    uint8_t secret_[32] = {}, key_[32] = {};
    bool configured_ = false, pending_ = false, ready_ = false;
    uint32_t sequence_ = 0;
    std::string id_, model_, appNonce_, boardNonce_;
};

// Bounded stream framing shared by firmware and host tests.
class PB2Stream {
public:
    void reset() { index_ = 0; text_.clear(); discard_ = false; started_ = 0; }
    template<class Line, class Packet>
    void feed(uint8_t b, uint32_t received, Line line, Packet packet) {
        if ((index_ || !text_.empty() || discard_) &&
            uint32_t(received - started_) >= 400) reset();
        if (index_) {
            bytes_[index_++] = b;
            if ((index_ == 2 && b != 0x55) ||
                (index_ == 3 && b != 2) ||
                (index_ == 4 && b != 1 && b != 2) ||
                (index_ == 9 && b != (bytes_[3] == 1 ? 6 : 0))) {
                reset(); return;
            }
            if (index_ >= 9 && index_ == size_t(17 + bytes_[8])) {
                const size_t length = index_;
                const uint32_t started = started_;
                index_ = 0;
                packet(bytes_, length, started);
            }
            return;
        }
        if (b == 0xAA) {
            reset(); started_ = received; bytes_[0] = b; index_ = 1; return;
        }
        if (b == '\r') return;
        if (b == '\n') {
            std::string complete;
            if (!discard_) complete = text_;
            reset();
            if (!complete.empty()) line(complete);
            return;
        }
        if (text_.empty() && !discard_) started_ = received;
        if (b < 32 || b > 126 || text_.size() >= 96) {
            text_.clear(); discard_ = true; return;
        }
        if (!discard_) text_ += char(b);
    }
private:
    uint8_t bytes_[23] = {};
    size_t index_ = 0;
    uint32_t started_ = 0;
    bool discard_ = false;
    std::string text_;
};
#endif


