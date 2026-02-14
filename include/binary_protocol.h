#pragma once

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <functional>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>
#include <uuid/uuid.h>

// ── BinaryReader ─────────────────────────────────────────────────────
class BinaryReader {
public:
    explicit BinaryReader(const uint8_t* data, size_t size)
        : data_(data), size_(size), pos_(0) {}
    explicit BinaryReader(const std::vector<uint8_t>& v)
        : data_(v.data()), size_(v.size()), pos_(0) {}

    uint8_t read_byte() {
        if (pos_ >= size_) throw std::runtime_error("unexpected EOF");
        return data_[pos_++];
    }
    const uint8_t* take(size_t n) {
        if (pos_ + n > size_) throw std::runtime_error("unexpected EOF");
        const uint8_t* p = data_ + pos_;
        pos_ += n;
        return p;
    }
    uint64_t read_uleb() {
        uint64_t result = 0; int shift = 0;
        while (true) {
            uint8_t b = read_byte();
            result |= (uint64_t(b & 0x7f)) << shift;
            if ((b & 0x80) == 0) break;
            shift += 7;
        }
        return result;
    }
    int8_t   read_i8()  { return static_cast<int8_t>(read_byte()); }
    uint8_t  read_u8()  { return read_byte(); }
    uint16_t read_u16() { auto p=take(2); uint16_t v; memcpy(&v,p,2); return v; }
    uint32_t read_u32() { auto p=take(4); uint32_t v; memcpy(&v,p,4); return v; }
    int32_t  read_i32() { auto p=take(4); int32_t  v; memcpy(&v,p,4); return v; }
    uint64_t read_u64() { auto p=take(8); uint64_t v; memcpy(&v,p,8); return v; }
    int64_t  read_i64() { auto p=take(8); int64_t  v; memcpy(&v,p,8); return v; }
    float    read_f32() { uint32_t b=read_u32(); float v; memcpy(&v,&b,4); return v; }
    bool     read_bool(){ return read_byte()==1; }
    std::string read_string() {
        uint64_t len = read_uleb();
        auto p = take(static_cast<size_t>(len));
        return std::string(reinterpret_cast<const char*>(p), static_cast<size_t>(len));
    }
    std::string read_varchar(size_t max_len) {
        uint64_t len = read_uleb();
        if (len > max_len) throw std::runtime_error("string too long");
        auto p = take(static_cast<size_t>(len));
        return std::string(reinterpret_cast<const char*>(p), static_cast<size_t>(len));
    }

private:
    const uint8_t* data_;
    size_t size_, pos_;
};

// ── BinaryWriter ─────────────────────────────────────────────────────
class BinaryWriter {
public:
    explicit BinaryWriter(std::vector<uint8_t>& buf) : buf_(buf) {}

    void write_byte(uint8_t b)                       { buf_.push_back(b); }
    void write_bytes(const uint8_t* d, size_t n)     { buf_.insert(buf_.end(), d, d+n); }
    void write_uleb(uint64_t v) {
        do { uint8_t b=v&0x7f; v>>=7; if(v) b|=0x80; write_byte(b); } while(v);
    }
    void write_i8(int8_t v)    { write_byte(static_cast<uint8_t>(v)); }
    void write_u8(uint8_t v)   { write_byte(v); }
    void write_u16(uint16_t v) { uint8_t b[2]; memcpy(b,&v,2); write_bytes(b,2); }
    void write_u32(uint32_t v) { uint8_t b[4]; memcpy(b,&v,4); write_bytes(b,4); }
    void write_i32(int32_t v)  { uint8_t b[4]; memcpy(b,&v,4); write_bytes(b,4); }
    void write_u64(uint64_t v) { uint8_t b[8]; memcpy(b,&v,8); write_bytes(b,8); }
    void write_i64(int64_t v)  { uint8_t b[8]; memcpy(b,&v,8); write_bytes(b,8); }
    void write_f32(float v)    { uint32_t b; memcpy(&b,&v,4); write_u32(b); }
    void write_bool(bool v)    { write_byte(v?1:0); }
    void write_string(const std::string& s) {
        write_uleb(s.size());
        write_bytes(reinterpret_cast<const uint8_t*>(s.data()), s.size());
    }

private:
    std::vector<uint8_t>& buf_;
};

// ── UUID wrapper ─────────────────────────────────────────────────────
struct MpUuid {
    uint64_t high = 0, low = 0;

    bool operator==(const MpUuid& o) const { return high==o.high && low==o.low; }
    bool operator!=(const MpUuid& o) const { return !(*this==o); }

    static MpUuid new_v4() {
        uuid_t u; uuid_generate_random(u);
        MpUuid r;
        memcpy(&r.high, u, 8); memcpy(&r.low, u+8, 8);
        return r;
    }
    static MpUuid generate() { return new_v4(); }
    std::string to_string() const {
        uuid_t u; memcpy(u,&high,8); memcpy(u+8,&low,8);
        char s[37]; uuid_unparse_lower(u,s);
        return std::string(s);
    }
    std::string str() const { return to_string(); }
    void write_binary(BinaryWriter& w) const { w.write_u64(low); w.write_u64(high); }
    static MpUuid read_binary(BinaryReader& r) {
        MpUuid u; u.low=r.read_u64(); u.high=r.read_u64(); return u;
    }
};

struct MpUuidHash {
    size_t operator()(const MpUuid& u) const {
        return std::hash<uint64_t>()(u.high) ^ (std::hash<uint64_t>()(u.low)<<1);
    }
};

// ── Half-float (f16) ─────────────────────────────────────────────────
inline uint16_t f32_to_f16(float value) {
    uint32_t bits; memcpy(&bits, &value, 4);
    uint32_t sign = (bits >> 16) & 0x8000;
    int32_t exp = ((bits >> 23) & 0xFF) - 127;
    uint32_t man = bits & 0x7FFFFF;
    if (exp == 128) return man ? (uint16_t)(sign|0x7C00|(man>>13)) : (uint16_t)(sign|0x7C00);
    if (exp > 15)   return (uint16_t)(sign|0x7C00);
    if (exp > -15)  return (uint16_t)(sign|((exp+15)<<10)|(man>>13));
    if (exp >= -24) { man|=0x800000; return (uint16_t)(sign|(man>>(-1-exp+13))); }
    return (uint16_t)sign;
}

inline float f16_to_f32(uint16_t value) {
    uint32_t sign = (uint32_t(value) & 0x8000) << 16;
    uint32_t exp = (value >> 10) & 0x1F;
    uint32_t man = value & 0x3FF;
    uint32_t result;
    if (exp == 0) {
        if (man == 0) { result = sign; }
        else { exp=1; while(!(man&0x400)){man<<=1;exp--;} man&=0x3FF; result=sign|((exp+127-15)<<23)|(man<<13); }
    } else if (exp == 31) { result = sign|0x7F800000|(man<<13); }
    else { result = sign|((exp+127-15)<<23)|(man<<13); }
    float f; memcpy(&f, &result, 4); return f;
}
