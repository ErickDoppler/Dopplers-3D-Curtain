// Hunyuan3D 2.0 shape VAE, decoder side: latents (3072 x 64) -> 16-layer transformer -> cross-attention decoder that answers "how far inside the shape"
// for any query point. A grid of queries gives the signed distance volume that is turned into a mesh.
#pragma once
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include "h3d/gg.h"

class ShapeVAE {
public:
    bool load(const std::string& checkpoint, std::string& err);
    // latents: 64 x 3072 channel-major, already divided by the latent scale factor. Runs the transformer and prepares the keys and values of the decoder.
    bool setLatents(const float* latents, std::string& err);
    // field value at the points (x, y, z triples in [-1.01, 1.01]); chunk = points per graph
    bool query(const float* xyz, int n, float* out, std::string& err);
    // (res+1)^3 grid over [-bound, bound]^3, index [ix][iy][iz] with z fastest; progress(fraction) may return false to stop
    bool decodeGrid(int res, float bound, int chunk, std::vector<float>& grid, std::string& err, const std::function<bool(float)>& progress = nullptr);
    // Same grid as decodeGrid, but coarse to fine: every stride-th point first (stride 4), then only the cells near the surface are refined down to the full
    // resolution; the rest is interpolated. res must be a multiple of the stride. queries (optional) receives how many points were evaluated.
    bool decodeGridHier(int res, float bound, int chunk, std::vector<float>& grid, std::string& err, const std::function<bool(float)>& progress = nullptr, size_t* queries = nullptr, int stride = 4);
    std::vector<float> transformerOut;                           // 3072 x 1024 after setLatents (for tests)
    void unload() { w_.reset(); }
    bool loaded() const { return w_ != nullptr; }
    static constexpr int kTokens = 3072, kWidth = 1024, kHeads = 16, kLayers = 16, kFreqs = 8, kQueryDim = 51;
private:
    struct Block { ggml_tensor *ln1w, *ln1b, *qkv, *qn_w, *qn_b, *kn_w, *kn_b, *projw, *projb, *ln2w, *ln2b, *fc1w, *fc1b, *fc2w, *fc2b; };
    std::unique_ptr<Weights> w_; std::vector<Block> B_;
    ggml_tensor *postW_, *postB_;
    // decoder
    ggml_tensor *qpW_, *qpB_, *l1w_, *l1b_, *l2w_, *l2b_, *l3w_, *l3b_, *cqW_, *ckvW_, *cprojW_, *cprojB_, *qnW_, *qnB_, *knW_, *knB_, *f1W_, *f1B_, *f2W_, *f2B_, *lpW_, *lpB_, *outW_, *outB_;
    std::vector<float> K_, V_;                                   // [64 x 16 x 3072] each, ready for the decoder graphs
};
