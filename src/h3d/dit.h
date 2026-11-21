// The Hunyuan3D 2.0 shape DiT as ComfyUI runs it: 16 double-stream blocks (latent and image tokens with joint attention), 32 single-stream blocks,
// width 1024, 16 heads, no rotary embedding, flow-matching velocity output.
#pragma once
#include <memory>
#include <string>
#include <vector>
#include "h3d/gg.h"

class Hunyuan3DiT {
public:
    bool load(const std::string& checkpoint, std::string& err);
    // latent: 64 x 3072 (channel-major, like ComfyUI's [1,64,3072]); sigma in [0,1]; ctx: 1370 x 1536 image tokens (zeros = unconditional); out: 64 x 3072 velocity
    bool forward(const float* latent, float sigma, const float* ctx, int ctxTokens, std::vector<float>& out, std::string& err);
    void unload() { w_.reset(); }
    bool loaded() const { return w_ != nullptr; }
    static constexpr int kLatentTokens = 3072, kChannels = 64, kWidth = 1024, kHeads = 16, kDouble = 16, kSingle = 32, kCtxDim = 1536;
private:
    struct Stream { ggml_tensor *mod_w, *mod_b, *qkv_w, *qkv_b, *qn, *kn, *proj_w, *proj_b, *fc1_w, *fc1_b, *fc2_w, *fc2_b; };
    struct Double { Stream img, txt; };
    struct Single { ggml_tensor *mod_w, *mod_b, *l1_w, *l1_b, *l2_w, *l2_b, *qn, *kn; };
    std::unique_ptr<Weights> w_; std::vector<Double> D_; std::vector<Single> S_;
    ggml_tensor *latW_, *latB_, *t1W_, *t1B_, *t2W_, *t2B_, *condW_, *condB_, *finModW_, *finModB_, *finW_, *finB_;
};
