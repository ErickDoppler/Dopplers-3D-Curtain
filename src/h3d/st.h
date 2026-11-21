// Memory-mapped .safetensors reader (read-only).
#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <vector>

struct StTensor {
    std::string name, dtype;          // dtype: "F16", "F32", "BF16"
    std::vector<int64_t> shape;       // safetensors order: outermost first
    const uint8_t* data = nullptr; uint64_t bytes = 0;
    int64_t numel() const { int64_t n = 1; for (auto d : shape) n *= d; return n; }
};

class StFile {
public:
    ~StFile();
    bool open(const std::string& utf8Path, std::string& err);
    const StTensor* find(const std::string& name) const;
    const std::vector<StTensor>& tensors() const { return list_; }
    static void toFloat(const StTensor& t, float* out);        // F16 / BF16 / F32 -> float32 (numel values)
private:
    void* file_ = nullptr; void* map_ = nullptr; const uint8_t* base_ = nullptr;
    std::vector<StTensor> list_; std::map<std::string, size_t> idx_;
};
float f16ToF32(uint16_t v);
