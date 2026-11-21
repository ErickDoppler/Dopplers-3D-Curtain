#include "h3d/st.h"
#include <windows.h>
#include <cstring>
#include "json.h"
#include "sys.h"

StFile::~StFile() {
    if (base_) UnmapViewOfFile(base_);
    if (map_) CloseHandle((HANDLE)map_);
    if (file_ && file_ != INVALID_HANDLE_VALUE) CloseHandle((HANDLE)file_);
}

bool StFile::open(const std::string& path, std::string& err) {
    HANDLE f = CreateFileW(sys::widen(path).c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) { err = "cannot open " + path; return false; }
    file_ = f; LARGE_INTEGER sz; GetFileSizeEx(f, &sz);
    HANDLE m = CreateFileMappingW(f, nullptr, PAGE_READONLY, 0, 0, nullptr);
    if (!m) { err = "cannot map " + path; return false; }
    map_ = m; base_ = (const uint8_t*)MapViewOfFile(m, FILE_MAP_READ, 0, 0, 0);
    if (!base_ || sz.QuadPart < 16) { err = "cannot map " + path; return false; }
    uint64_t hl; memcpy(&hl, base_, 8);
    if (hl == 0 || 8 + hl > (uint64_t)sz.QuadPart) { err = "not a safetensors file: " + path; return false; }
    bool ok; Json j = Json::parse(std::string((const char*)base_ + 8, (size_t)hl), &ok);
    if (!ok) { err = "damaged safetensors header"; return false; }
    const uint8_t* data = base_ + 8 + hl;
    for (auto& kv : j.o) {
        if (kv.first == "__metadata__") continue;
        StTensor t; t.name = kv.first; t.dtype = kv.second.str("dtype");
        for (auto& d : kv.second.get("shape").a) t.shape.push_back((int64_t)d.num());
        const auto& off = kv.second.get("data_offsets").a;
        if (off.size() != 2) { err = "bad offsets for " + kv.first; return false; }
        const uint64_t a = (uint64_t)off[0].num(), b = (uint64_t)off[1].num();
        if (8 + hl + b > (uint64_t)sz.QuadPart) { err = "the file is cut off (" + kv.first + ")"; return false; }
        t.data = data + a; t.bytes = b - a; idx_[t.name] = list_.size(); list_.push_back(std::move(t));
    }
    return true;
}

const StTensor* StFile::find(const std::string& name) const { auto it = idx_.find(name); return it == idx_.end() ? nullptr : &list_[it->second]; }

float f16ToF32(uint16_t h) {
    const uint32_t s = (h & 0x8000u) << 16; uint32_t e = (h >> 10) & 0x1F, m = h & 0x3FF, r;
    if (e == 0) { if (m == 0) r = s; else { e = 127 - 15 + 1; while (!(m & 0x400)) { m <<= 1; e--; } m &= 0x3FF; r = s | (e << 23) | (m << 13); } }
    else if (e == 31) r = s | 0x7F800000u | (m << 13);
    else r = s | ((e + 112) << 23) | (m << 13);
    float f; memcpy(&f, &r, 4); return f;
}

void StFile::toFloat(const StTensor& t, float* out) {
    const int64_t n = t.numel();
    if (t.dtype == "F32") memcpy(out, t.data, (size_t)n * 4);
    else if (t.dtype == "F16") { const uint16_t* p = (const uint16_t*)t.data; for (int64_t i = 0; i < n; i++) out[i] = f16ToF32(p[i]); }
    else if (t.dtype == "BF16") { const uint16_t* p = (const uint16_t*)t.data; for (int64_t i = 0; i < n; i++) { uint32_t u = (uint32_t)p[i] << 16; memcpy(&out[i], &u, 4); } }
}
