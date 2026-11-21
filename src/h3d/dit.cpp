#include "h3d/dit.h"
#include <cmath>
#include <cstring>

bool Hunyuan3DiT::load(const std::string& ckpt, std::string& err) {
    w_ = std::make_unique<Weights>();
    if (!w_->open(ckpt, err)) { w_.reset(); return false; }
    auto W = [&](const std::string& n) { return w_->want("model." + n); };
    latW_ = W("latent_in.weight"); latB_ = W("latent_in.bias"); t1W_ = W("time_in.in_layer.weight"); t1B_ = W("time_in.in_layer.bias");
    t2W_ = W("time_in.out_layer.weight"); t2B_ = W("time_in.out_layer.bias"); condW_ = W("cond_in.weight"); condB_ = W("cond_in.bias");
    finModW_ = W("final_layer.adaLN_modulation.1.weight"); finModB_ = W("final_layer.adaLN_modulation.1.bias"); finW_ = W("final_layer.linear.weight"); finB_ = W("final_layer.linear.bias");
    D_.resize(kDouble); S_.resize(kSingle);
    for (int i = 0; i < kDouble; i++) {
        const std::string b = "double_blocks." + std::to_string(i) + ".";
        auto stream = [&](const char* s, Stream& st) {
            const std::string p = b + s;
            st.mod_w = W(p + "_mod.lin.weight"); st.mod_b = W(p + "_mod.lin.bias"); st.qkv_w = W(p + "_attn.qkv.weight"); st.qkv_b = W(p + "_attn.qkv.bias");
            st.qn = W(p + "_attn.norm.query_norm.scale"); st.kn = W(p + "_attn.norm.key_norm.scale"); st.proj_w = W(p + "_attn.proj.weight"); st.proj_b = W(p + "_attn.proj.bias");
            st.fc1_w = W(p + "_mlp.0.weight"); st.fc1_b = W(p + "_mlp.0.bias"); st.fc2_w = W(p + "_mlp.2.weight"); st.fc2_b = W(p + "_mlp.2.bias");
        };
        stream("img", D_[i].img); stream("txt", D_[i].txt);
    }
    for (int i = 0; i < kSingle; i++) {
        const std::string b = "single_blocks." + std::to_string(i) + "."; Single& s = S_[i];
        s.mod_w = W(b + "modulation.lin.weight"); s.mod_b = W(b + "modulation.lin.bias"); s.l1_w = W(b + "linear1.weight"); s.l1_b = W(b + "linear1.bias");
        s.l2_w = W(b + "linear2.weight"); s.l2_b = W(b + "linear2.bias"); s.qn = W(b + "norm.query_norm.scale"); s.kn = W(b + "norm.key_norm.scale");
    }
    if (!w_->upload(err)) { w_.reset(); return false; }
    return true;
}

namespace {
// a [1024, 1] slice of a modulation vector
ggml_tensor* slice(ggml_context* c, ggml_tensor* m, int idx, int width) { return ggml_view_2d(c, m, width, 1, m->nb[1], (size_t)idx * width * sizeof(float)); }
// LayerNorm without parameters, then x * (1 + scale) + shift
ggml_tensor* modulate(ggml_context* c, ggml_tensor* x, ggml_tensor* shift, ggml_tensor* scale) {
    return ggml_add(c, ggml_mul(c, ggml_norm(c, x, 1e-6f), ggml_scale_bias(c, scale, 1.0f, 1.0f)), shift);
}
// q, k, v of a fused projection laid out [q | k | v], each heads x head_dim; RMS-normalised q and k (per head) with learned scales
struct QKV { ggml_tensor *q, *k, *v; };
QKV splitQKV(ggml_context* c, ggml_tensor* qkv, ggml_tensor* qn, ggml_tensor* kn, int heads) {
    const int64_t W = qkv->ne[0] / 3, N = qkv->ne[1], hd = W / heads;
    auto part = [&](int i) { return ggml_reshape_3d(c, ggml_cont(c, ggml_view_2d(c, qkv, W, N, qkv->nb[1], (size_t)i * W * sizeof(float))), hd, heads, N); };
    QKV r; r.q = part(0); r.k = part(1); r.v = part(2);
    if (qn) r.q = ggml_mul(c, ggml_rms_norm(c, r.q, 1e-6f), qn);
    if (kn) r.k = ggml_mul(c, ggml_rms_norm(c, r.k, 1e-6f), kn);
    return r;
}
}  // namespace

bool Hunyuan3DiT::forward(const float* latent, float sigma, const float* ctx, int ctxTokens, std::vector<float>& out, std::string& err) {
    if (!w_) { err = "the DiT is not loaded"; return false; }
    const int T = kLatentTokens, C = kChannels, Wd = kWidth, H = kHeads, NT = ctxTokens, NA = NT + T;
    Graph G(4200); ggml_context* c = G.ctx;
    ggml_tensor* xin = G.input(GGML_TYPE_F32, C, T), *cin = G.input(GGML_TYPE_F32, kCtxDim, NT), *tin = G.input(GGML_TYPE_F32, 256);
    // timestep embedding: sinusoidal of 1000 * (1 - sigma), max period 1000, cos then sin; then two linear layers with SiLU between
    ggml_tensor* vec = linear(c, t2W_, t2B_, ggml_silu(c, linear(c, t1W_, t1B_, tin)));                      // [1024]
    ggml_tensor* sv = ggml_silu(c, vec);
    ggml_tensor* img = linear(c, latW_, latB_, xin);                                                         // [1024, 3072]
    ggml_tensor* txt = linear(c, condW_, condB_, cin);                                                       // [1024, NT]
    const float scale = 1.0f / std::sqrt((float)(Wd / H));
    for (const Double& b : D_) {
        ggml_tensor* mi = linear(c, b.img.mod_w, b.img.mod_b, sv), *mt = linear(c, b.txt.mod_w, b.txt.mod_b, sv);          // [6144]: shift1 scale1 gate1 shift2 scale2 gate2
        QKV qi = splitQKV(c, linear(c, b.img.qkv_w, b.img.qkv_b, modulate(c, img, slice(c, mi, 0, Wd), slice(c, mi, 1, Wd))), b.img.qn, b.img.kn, H);
        QKV qt = splitQKV(c, linear(c, b.txt.qkv_w, b.txt.qkv_b, modulate(c, txt, slice(c, mt, 0, Wd), slice(c, mt, 1, Wd))), b.txt.qn, b.txt.kn, H);
        ggml_tensor* a = attention(c, ggml_concat(c, qt.q, qi.q, 2), ggml_concat(c, qt.k, qi.k, 2), ggml_concat(c, qt.v, qi.v, 2), scale);   // text tokens first
        ggml_tensor* at = ggml_view_2d(c, a, Wd, NT, a->nb[1], 0), *ai = ggml_view_2d(c, a, Wd, T, a->nb[1], (size_t)NT * a->nb[1]);
        img = ggml_add(c, img, ggml_mul(c, linear(c, b.img.proj_w, b.img.proj_b, ggml_cont(c, ai)), slice(c, mi, 2, Wd)));
        txt = ggml_add(c, txt, ggml_mul(c, linear(c, b.txt.proj_w, b.txt.proj_b, ggml_cont(c, at)), slice(c, mt, 2, Wd)));
        ggml_tensor* hi = linear(c, b.img.fc2_w, b.img.fc2_b, ggml_gelu(c, linear(c, b.img.fc1_w, b.img.fc1_b, modulate(c, img, slice(c, mi, 3, Wd), slice(c, mi, 4, Wd)))));
        ggml_tensor* ht = linear(c, b.txt.fc2_w, b.txt.fc2_b, ggml_gelu(c, linear(c, b.txt.fc1_w, b.txt.fc1_b, modulate(c, txt, slice(c, mt, 3, Wd), slice(c, mt, 4, Wd)))));
        img = ggml_add(c, img, ggml_mul(c, hi, slice(c, mi, 5, Wd)));
        txt = ggml_add(c, txt, ggml_mul(c, ht, slice(c, mt, 5, Wd)));
    }
    ggml_tensor* x = ggml_concat(c, txt, img, 1);                                                            // [1024, NA]
    for (const Single& s : S_) {
        ggml_tensor* m = linear(c, s.mod_w, s.mod_b, sv);                                                    // [3072]: shift scale gate
        ggml_tensor* l1 = linear(c, s.l1_w, s.l1_b, modulate(c, x, slice(c, m, 0, Wd), slice(c, m, 1, Wd)));  // [7168, NA]
        QKV q = splitQKV(c, ggml_cont(c, ggml_view_2d(c, l1, 3 * Wd, NA, l1->nb[1], 0)), s.qn, s.kn, H);
        ggml_tensor* a = attention(c, q.q, q.k, q.v, scale);                                                 // [1024, NA]
        ggml_tensor* mlp = ggml_gelu(c, ggml_cont(c, ggml_view_2d(c, l1, 4 * Wd, NA, l1->nb[1], (size_t)3 * Wd * sizeof(float))));
        ggml_tensor* o = linear(c, s.l2_w, s.l2_b, ggml_concat(c, a, mlp, 0));                               // [1024, NA]
        x = ggml_add(c, x, ggml_mul(c, o, slice(c, m, 2, Wd)));
    }
    ggml_tensor* xi = ggml_cont(c, ggml_view_2d(c, x, Wd, T, x->nb[1], (size_t)NT * x->nb[1]));
    ggml_tensor* fm = linear(c, finModW_, finModB_, sv);                                                     // [2048]: shift scale
    ggml_tensor* y = linear(c, finW_, finB_, modulate(c, xi, slice(c, fm, 0, Wd), slice(c, fm, 1, Wd)));      // [64, 3072]
    y = ggml_scale(c, y, -1.0f);
    G.output(y);
    if (!G.alloc(err)) return false;
    std::vector<float> xt((size_t)T * C), te(256);
    for (int t = 0; t < T; t++) for (int ch = 0; ch < C; ch++) xt[(size_t)t * C + ch] = latent[(size_t)ch * T + t];     // channel-major -> tokens x channels
    const float tt = 1000.0f * (1.0f - sigma);
    for (int i = 0; i < 128; i++) { const float f = std::exp(-std::log(1000.0f) * i / 128.0f), a = tt * f; te[i] = std::cos(a); te[128 + i] = std::sin(a); }
    Graph::set(xin, xt.data(), xt.size() * 4); Graph::set(cin, ctx, (size_t)NT * kCtxDim * 4); Graph::set(tin, te.data(), 256 * 4);
    if (!G.run(err)) return false;
    std::vector<float> yt((size_t)T * C); Graph::get(y, yt.data(), yt.size() * 4);
    out.resize((size_t)T * C);
    for (int t = 0; t < T; t++) for (int ch = 0; ch < C; ch++) out[(size_t)ch * T + t] = yt[(size_t)t * C + ch];
    return true;
}
