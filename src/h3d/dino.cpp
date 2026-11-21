#include "h3d/dino.h"
#include <cmath>
#include <cstring>

static const char* P = "conditioner.main_image_encoder.model.";

bool DinoEncoder::load(const std::string& ckpt, std::string& err) {
    w_ = std::make_unique<Weights>();
    if (!w_->open(ckpt, err)) { w_.reset(); return false; }
    auto W = [&](const std::string& n) { return w_->want(std::string(P) + n); };
    convW_ = W("embeddings.patch_embeddings.projection.weight"); convB_ = W("embeddings.patch_embeddings.projection.bias");
    cls_ = W("embeddings.cls_token"); pos_ = W("embeddings.position_embeddings"); lnW_ = W("layernorm.weight"); lnB_ = W("layernorm.bias");
    L_.resize(40);
    for (int i = 0; i < 40; i++) {
        const std::string b = "encoder.layer." + std::to_string(i) + "."; Layer& l = L_[i];
        l.n1w = W(b + "norm1.weight"); l.n1b = W(b + "norm1.bias"); l.n2w = W(b + "norm2.weight"); l.n2b = W(b + "norm2.bias");
        l.qw = W(b + "attention.attention.query.weight"); l.qb = W(b + "attention.attention.query.bias");
        l.kw = W(b + "attention.attention.key.weight"); l.kb = W(b + "attention.attention.key.bias");
        l.vw = W(b + "attention.attention.value.weight"); l.vb = W(b + "attention.attention.value.bias");
        l.ow = W(b + "attention.output.dense.weight"); l.ob = W(b + "attention.output.dense.bias");
        l.ls1 = W(b + "layer_scale1.lambda1"); l.ls2 = W(b + "layer_scale2.lambda1");
        l.win = W(b + "mlp.weights_in.weight"); l.bin = W(b + "mlp.weights_in.bias"); l.wout = W(b + "mlp.weights_out.weight"); l.bout = W(b + "mlp.weights_out.bias");
    }
    if (!w_->upload(err)) { w_.reset(); return false; }
    return true;
}

bool DinoEncoder::encode(const float* pixels, std::vector<float>& out, std::string& err) {
    if (!w_) { err = "the image encoder is not loaded"; return false; }
    const int D = kWidth, H = 24, N = kTokens;
    Graph G(3200); ggml_context* c = G.ctx;
    ggml_tensor* img = G.input(GGML_TYPE_F32, kSize, kSize, 3, 1);
    ggml_tensor* x = ggml_conv_2d(c, convW_, img, 14, 14, 0, 0, 1, 1);                          // [37, 37, 1536, 1]
    x = ggml_add(c, x, ggml_reshape_4d(c, convB_, 1, 1, D, 1));
    x = ggml_cont(c, ggml_transpose(c, ggml_reshape_2d(c, x, N - 1, D)));                       // [1536, 1369]
    x = ggml_concat(c, ggml_reshape_2d(c, cls_, D, 1), x, 1);                                   // [1536, 1370]
    x = ggml_add(c, x, ggml_reshape_2d(c, pos_, D, N));
    for (const Layer& l : L_) {
        ggml_tensor* h = layerNorm(c, x, l.n1w, l.n1b, 1e-6f);
        ggml_tensor* q = ggml_reshape_3d(c, linear(c, l.qw, l.qb, h), D / H, H, N);
        ggml_tensor* k = ggml_reshape_3d(c, linear(c, l.kw, l.kb, h), D / H, H, N);
        ggml_tensor* v = ggml_reshape_3d(c, linear(c, l.vw, l.vb, h), D / H, H, N);
        ggml_tensor* a = linear(c, l.ow, l.ob, attention(c, q, k, v, 1.0f / std::sqrt((float)(D / H))));
        x = ggml_add(c, x, ggml_mul(c, a, l.ls1));
        h = layerNorm(c, x, l.n2w, l.n2b, 1e-6f);
        ggml_tensor* g = linear(c, l.win, l.bin, h);                                            // [8192, N]: SwiGLU, the first half is gated
        const int64_t hid = g->ne[0] / 2;
        ggml_tensor* g1 = ggml_cont(c, ggml_view_2d(c, g, hid, N, g->nb[1], 0)), *g2 = ggml_view_2d(c, g, hid, N, g->nb[1], (size_t)hid * sizeof(float));
        ggml_tensor* m = linear(c, l.wout, l.bout, ggml_mul(c, ggml_silu(c, g1), g2));
        x = ggml_add(c, x, ggml_mul(c, m, l.ls2));
    }
    x = layerNorm(c, x, lnW_, lnB_, 1e-6f);
    G.output(x);
    if (!G.alloc(err)) return false;
    Graph::set(img, pixels, (size_t)3 * kSize * kSize * 4);
    if (!G.run(err)) return false;
    out.resize((size_t)N * D); Graph::get(x, out.data(), out.size() * 4);
    return true;
}
