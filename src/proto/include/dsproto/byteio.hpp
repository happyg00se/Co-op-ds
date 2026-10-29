#pragma once
// Little-endian bounded byte writer/reader used by every DSCoop packet.
// No exceptions: a reader that runs past the end sets `failed()` and returns zeros.
#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace dsproto {

class ByteWriter {
public:
    ByteWriter() = default;
    explicit ByteWriter(size_t reserve) { buf_.reserve(reserve); }

    void u8(uint8_t v) { buf_.push_back(v); }
    void i8(int8_t v) { u8(static_cast<uint8_t>(v)); }
    void u16(uint16_t v) { put(&v, 2); }
    void i16(int16_t v) { put(&v, 2); }
    void u32(uint32_t v) { put(&v, 4); }
    void i32(int32_t v) { put(&v, 4); }
    void u64(uint64_t v) { put(&v, 8); }
    void f32(float v) { put(&v, 4); }
    void boolean(bool v) { u8(v ? 1 : 0); }
    void bytes(std::span<const uint8_t> b) { buf_.insert(buf_.end(), b.begin(), b.end()); }

    /// Length-prefixed (u8) string, truncated to 255 bytes.
    void str8(std::string_view s) {
        const auto n = static_cast<uint8_t>(s.size() > 255 ? 255 : s.size());
        u8(n);
        buf_.insert(buf_.end(), s.begin(), s.begin() + n);
    }
    /// Length-prefixed (u16) string.
    void str16(std::string_view s) {
        const auto n = static_cast<uint16_t>(s.size() > 65535 ? 65535 : s.size());
        u16(n);
        buf_.insert(buf_.end(), s.begin(), s.begin() + n);
    }

    size_t size() const { return buf_.size(); }
    const std::vector<uint8_t>& data() const { return buf_; }
    std::vector<uint8_t> take() { return std::move(buf_); }

    /// Patch a previously written u8 (used for counters known only after the loop).
    void patchU8(size_t pos, uint8_t v) { buf_[pos] = v; }

private:
    void put(const void* p, size_t n) {
        // Host order on every supported target (x86-64 / ARM64 LE) is little-endian.
        const auto* b = static_cast<const uint8_t*>(p);
        buf_.insert(buf_.end(), b, b + n);
    }
    std::vector<uint8_t> buf_;
};

class ByteReader {
public:
    ByteReader(const uint8_t* data, size_t size) : p_(data), end_(data + size) {}
    explicit ByteReader(std::span<const uint8_t> s) : ByteReader(s.data(), s.size()) {}

    uint8_t u8() { uint8_t v = 0; get(&v, 1); return v; }
    int8_t i8() { return static_cast<int8_t>(u8()); }
    uint16_t u16() { uint16_t v = 0; get(&v, 2); return v; }
    int16_t i16() { int16_t v = 0; get(&v, 2); return v; }
    uint32_t u32() { uint32_t v = 0; get(&v, 4); return v; }
    int32_t i32() { int32_t v = 0; get(&v, 4); return v; }
    uint64_t u64() { uint64_t v = 0; get(&v, 8); return v; }
    float f32() { float v = 0; get(&v, 4); return v; }
    bool boolean() { return u8() != 0; }

    std::string str8() { return strN(u8()); }
    std::string str16() { return strN(u16()); }

    bool bytes(uint8_t* out, size_t n) { return get(out, n); }

    size_t remaining() const { return failed_ ? 0 : static_cast<size_t>(end_ - p_); }
    bool failed() const { return failed_; }
    /// True when everything was consumed and nothing failed.
    bool ok() const { return !failed_ && p_ == end_; }
    const uint8_t* cursor() const { return p_; }

private:
    std::string strN(size_t n) {
        if (failed_ || remaining() < n) { failed_ = true; return {}; }
        std::string s(reinterpret_cast<const char*>(p_), n);
        p_ += n;
        return s;
    }
    bool get(void* out, size_t n) {
        if (failed_ || remaining() < n) { failed_ = true; std::memset(out, 0, n); return false; }
        std::memcpy(out, p_, n);
        p_ += n;
        return true;
    }
    const uint8_t* p_;
    const uint8_t* end_;
    bool failed_ = false;
};

}  // namespace dsproto
