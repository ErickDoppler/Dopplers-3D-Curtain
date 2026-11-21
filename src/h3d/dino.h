// DINOv2-giant image encoder of Hunyuan3D 2.0 (40 layers, 24 heads, width 1536, SwiGLU, layer scale): picture -> 1370 x 1536 tokens.
#pragma once
#include <memory>
#include <string>
#include <vector>
#include "h3d/gg.h"

class DinoEncoder {
public:
    bool load(const std::string& checkpoint, std::string& err);
    // pixels: 3 x 518 x 518 floats (channel-major), already resized and normalised like ComfyUI's clip_preprocess; out: 1370 x 1536
    bool encode(const float* pixels, std::vector<float>& out, std::string& err);
    void unload() { w_.reset(); }
    bool loaded() const { return w_ != nullptr; }
    static constexpr int kSize = 518, kTokens = 1370, kWidth = 1536;
private:
    struct Layer { ggml_tensor *n1w, *n1b, *qw, *qb, *kw, *kb, *vw, *vb, *ow, *ob, *ls1, *n2w, *n2b, *win, *bin, *wout, *bout, *ls2; };
    std::unique_ptr<Weights> w_; std::vector<Layer> L_;
    ggml_tensor *convW_ = nullptr, *convB_ = nullptr, *cls_ = nullptr, *pos_ = nullptr, *lnW_ = nullptr, *lnB_ = nullptr;
};
