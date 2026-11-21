#include "h3d/vae.h"
#include <algorithm>
#include <cmath>
#include <cstring>

bool ShapeVAE::load(const std::string& ckpt, std::string& err) {
    w_ = std::make_unique<Weights>();
    if (!w_->open(ckpt, err)) { w_.reset(); return false; }
    auto W = [&](const std::string& n) { return w_->want("vae." + n); };
    postW_ = W("post_kl.weight"); postB_ = W("post_kl.bias");
    B_.resize(kLayers);
    for (int i = 0; i < kLayers; i++) {
        const std::string b = "transformer.resblocks." + std::to_string(i) + "."; Block& k = B_[i];
        k.ln1w = W(b + "ln_1.weight"); k.ln1b = W(b + "ln_1.bias"); k.ln2w = W(b + "ln_2.weight"); k.ln2b = W(b + "ln_2.bias");
        k.qkv = W(b + "attn.c_qkv.weight"); k.projw = W(b + "attn.c_proj.weight"); k.projb = W(b + "attn.c_proj.bias");
        k.qn_w = W(b + "attn.attention.q_norm.weight"); k.qn_b = W(b + "attn.attention.q_norm.bias"); k.kn_w = W(b + "attn.attention.k_norm.weight"); k.kn_b = W(b + "attn.attention.k_norm.bias");
        k.fc1w = W(b + "mlp.c_fc.weight"); k.fc1b = W(b + "mlp.c_fc.bias"); k.fc2w = W(b + "mlp.c_proj.weight"); k.fc2b = W(b + "mlp.c_proj.bias");
    }
    const std::string g = "geo_decoder.", x = g + "cross_attn_decoder.";
    qpW_ = W(g + "query_proj.weight"); qpB_ = W(g + "query_proj.bias");
    l1w_ = W(x + "ln_1.weight"); l1b_ = W(x + "ln_1.bias"); l2w_ = W(x + "ln_2.weight"); l2b_ = W(x + "ln_2.bias"); l3w_ = W(x + "ln_3.weight"); l3b_ = W(x + "ln_3.bias");
    cqW_ = W(x + "attn.c_q.weight"); ckvW_ = W(x + "attn.c_kv.weight"); cprojW_ = W(x + "attn.c_proj.weight"); cprojB_ = W(x + "attn.c_proj.bias");
    qnW_ = W(x + "attn.attention.q_norm.weight"); qnB_ = W(x + "attn.attention.q_norm.bias"); knW_ = W(x + "attn.attention.k_norm.weight"); knB_ = W(x + "attn.attention.k_norm.bias");
    f1W_ = W(x + "mlp.c_fc.weight"); f1B_ = W(x + "mlp.c_fc.bias"); f2W_ = W(x + "mlp.c_proj.weight"); f2B_ = W(x + "mlp.c_proj.bias");
    lpW_ = W(g + "ln_post.weight"); lpB_ = W(g + "ln_post.bias"); outW_ = W(g + "output_proj.weight"); outB_ = W(g + "output_proj.bias");
    if (!w_->upload(err)) { w_.reset(); return false; }
    return true;
}

namespace {
// q, k, v of a fused projection laid out per head as [q(64) k(64) v(64)], with the LayerNorm over each head's q and k
void headsOf3(ggml_context* c, ggml_tensor* qkv, int heads, ggml_tensor*& q, ggml_tensor*& k, ggml_tensor*& v) {
    const int64_t N = qkv->ne[1], hd3 = qkv->ne[0] / heads, hd = hd3 / 3;
    ggml_tensor* r = ggml_reshape_3d(c, qkv, hd3, heads, N);
    auto part = [&](int i) { return ggml_cont(c, ggml_view_3d(c, r, hd, heads, N, r->nb[1], r->nb[2], (size_t)i * hd * sizeof(float))); };
    q = part(0); k = part(1); v = part(2);
}
}  // namespace

bool ShapeVAE::setLatents(const float* lat, std::string& err) {
    if (!w_) { err = "the shape decoder is not loaded"; return false; }
    const int N = kTokens, D = kWidth, H = kHeads;
    Graph G(1200); ggml_context* c = G.ctx;
    ggml_tensor* in = G.input(GGML_TYPE_F32, 64, N);
    ggml_tensor* x = linear(c, postW_, postB_, in);
    for (const Block& b : B_) {
        ggml_tensor* q, *k, *v; headsOf3(c, linear(c, b.qkv, nullptr, layerNorm(c, x, b.ln1w, b.ln1b, 1e-6f)), H, q, k, v);
        q = layerNorm(c, q, b.qn_w, b.qn_b, 1e-6f); k = layerNorm(c, k, b.kn_w, b.kn_b, 1e-6f);
        x = ggml_add(c, x, linear(c, b.projw, b.projb, attention(c, q, k, v, 1.0f / std::sqrt((float)(D / H)))));
        x = ggml_add(c, x, linear(c, b.fc2w, b.fc2b, ggml_gelu_erf(c, linear(c, b.fc1w, b.fc1b, layerNorm(c, x, b.ln2w, b.ln2b, 1e-6f)))));
    }
    G.output(x);
    // keys and values of the decoder: LayerNorm of the latents, c_kv, per head [k(64) v(64)], LayerNorm on k
    ggml_tensor* kv = ggml_reshape_3d(c, linear(c, ckvW_, nullptr, layerNorm(c, x, l2w_, l2b_, 1e-6f)), 128, H, N);
    ggml_tensor* K = layerNorm(c, ggml_cont(c, ggml_view_3d(c, kv, 64, H, N, kv->nb[1], kv->nb[2], 0)), knW_, knB_, 1e-6f);
    ggml_tensor* V = ggml_cont(c, ggml_view_3d(c, kv, 64, H, N, kv->nb[1], kv->nb[2], 64 * sizeof(float)));
    G.output(K); G.output(V);
    if (!G.alloc(err)) return false;
    std::vector<float> t((size_t)N * 64); for (int i = 0; i < N; i++) for (int ch = 0; ch < 64; ch++) t[(size_t)i * 64 + ch] = lat[(size_t)ch * N + i];
    Graph::set(in, t.data(), t.size() * 4);
    if (!G.run(err)) return false;
    transformerOut.resize((size_t)N * D); Graph::get(x, transformerOut.data(), transformerOut.size() * 4);
    K_.resize((size_t)64 * H * N); V_.resize(K_.size()); Graph::get(K, K_.data(), K_.size() * 4); Graph::get(V, V_.data(), V_.size() * 4);
    return true;
}

bool ShapeVAE::query(const float* xyz, int n, float* out, std::string& err) {
    if (K_.empty()) { err = "set the latents first"; return false; }
    const int N = kTokens, H = kHeads;
    Graph G(160); ggml_context* c = G.ctx;
    ggml_tensor* qin = G.input(GGML_TYPE_F32, kQueryDim, n), *Kin = G.input(GGML_TYPE_F32, 64, H, N), *Vin = G.input(GGML_TYPE_F32, 64, H, N);
    ggml_tensor* x = linear(c, qpW_, qpB_, qin);                                              // [1024, n]
    ggml_tensor* q = ggml_reshape_3d(c, linear(c, cqW_, nullptr, layerNorm(c, x, l1w_, l1b_, 1e-6f)), 64, H, n);
    q = layerNorm(c, q, qnW_, qnB_, 1e-6f);
    x = ggml_add(c, x, linear(c, cprojW_, cprojB_, attention(c, q, Kin, Vin, 1.0f / std::sqrt(64.0f))));
    x = ggml_add(c, x, linear(c, f2W_, f2B_, ggml_gelu_erf(c, linear(c, f1W_, f1B_, layerNorm(c, x, l3w_, l3b_, 1e-6f)))));
    ggml_tensor* y = linear(c, outW_, outB_, layerNorm(c, x, lpW_, lpB_, 1e-5f));            // [1, n]
    G.output(y);
    if (!G.alloc(err)) return false;
    std::vector<float> emb((size_t)n * kQueryDim);
    for (int i = 0; i < n; i++) {                                                             // Fourier features: x, sin(2^k x), cos(2^k x), dimension-major
        float* e = &emb[(size_t)i * kQueryDim]; const float* p = xyz + (size_t)i * 3;
        for (int d = 0; d < 3; d++) { e[d] = p[d]; for (int k = 0; k < kFreqs; k++) { const float a = p[d] * (float)(1 << k); e[3 + d * kFreqs + k] = std::sin(a); e[3 + 24 + d * kFreqs + k] = std::cos(a); } }
    }
    Graph::set(qin, emb.data(), emb.size() * 4); Graph::set(Kin, K_.data(), K_.size() * 4); Graph::set(Vin, V_.data(), V_.size() * 4);
    if (!G.run(err)) return false;
    Graph::get(y, out, (size_t)n * 4);
    return true;
}

bool ShapeVAE::decodeGrid(int res, float bound, int chunk, std::vector<float>& grid, std::string& err, const std::function<bool(float)>& progress) {
    const int n = res + 1; const size_t total = (size_t)n * n * n; grid.assign(total, 0.0f);
    std::vector<float> xyz((size_t)chunk * 3);
    for (size_t start = 0; start < total; start += (size_t)chunk) {
        const int m = (int)std::min<size_t>(chunk, total - start);
        for (int i = 0; i < m; i++) {
            const size_t id = start + i; const int iz = (int)(id % n), iy = (int)((id / n) % n), ix = (int)(id / ((size_t)n * n));
            xyz[(size_t)i * 3] = -bound + 2 * bound * ix / res; xyz[(size_t)i * 3 + 1] = -bound + 2 * bound * iy / res; xyz[(size_t)i * 3 + 2] = -bound + 2 * bound * iz / res;
        }
        if (!query(xyz.data(), m, &grid[start], err)) return false;
        if (progress && !progress((float)(start + m) / (float)total)) { err = "cancelled"; return false; }
    }
    return true;
}
