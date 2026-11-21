#include "h3d/gg.h"
#include <cstdlib>
#include <cstring>
#include <thread>
#include "sys.h"

Backend& Backend::get() { static Backend b; return b; }

bool Backend::init(const std::string& wantIn, std::string& err) {
    if (main) return true;
    std::string want = wantIn; if (const char* e = getenv("D3C_BACKEND")) want = e;
    ggml_backend_load_all_from_path(sys::exeDir().c_str());
    ggml_backend_dev_t cdev = ggml_backend_dev_by_type(GGML_BACKEND_DEVICE_TYPE_CPU);
    if (!cdev) { err = "no CPU backend (the ggml-cpu DLLs must sit next to the program)"; return false; }
    cpu = ggml_backend_dev_init(cdev, nullptr);
    if (auto reg = ggml_backend_dev_backend_reg(cdev))
        if (auto fn = (void (*)(ggml_backend_t, int))ggml_backend_reg_get_proc_address(reg, "ggml_backend_set_n_threads")) fn(cpu, (int)std::max(2u, std::thread::hardware_concurrency() / 2));
    ggml_backend_dev_t gdev = want == "cpu" ? nullptr : ggml_backend_dev_by_type(GGML_BACKEND_DEVICE_TYPE_GPU);
    if (gdev) { main = ggml_backend_dev_init(gdev, nullptr); gpu = main != nullptr; if (gpu) name = ggml_backend_dev_description(gdev); }
    if (!main) { if (want == "gpu") { err = "no GPU backend (are the CUDA libraries next to the program?)"; return false; } main = cpu; name = "CPU"; }
    return true;
}

bool Weights::open(const std::string& path, std::string& err) {
    if (!file.open(path, err)) return false;
    ggml_init_params ip = {ggml_tensor_overhead() * (file.tensors().size() + 16), nullptr, true}; ctx_ = ggml_init(ip);
    return ctx_ != nullptr;
}

ggml_tensor* Weights::want(const std::string& name) {
    const StTensor* t = file.find(name);
    if (!t) { missing_ += (missing_.empty() ? "" : ", ") + name; return nullptr; }
    const int n = (int)t->shape.size();
    const bool w16 = (n == 2 || n == 4) && name.size() > 7 && name.compare(name.size() - 7, 7, ".weight") == 0;
    const ggml_type ty = w16 ? GGML_TYPE_F16 : GGML_TYPE_F32;
    int64_t ne[4] = {1, 1, 1, 1}; for (int i = 0; i < n && i < 4; i++) ne[i] = t->shape[n - 1 - i];
    ggml_tensor* r = ggml_new_tensor(ctx_, ty, n < 1 ? 1 : n, ne); ggml_set_name(r, name.c_str());
    pending_.push_back({r, t}); return r;
}

bool Weights::upload(std::string& err) {
    if (!missing_.empty()) { err = "missing tensors: " + missing_; return false; }
    buf_ = ggml_backend_alloc_ctx_tensors(ctx_, Backend::get().main);
    if (!buf_) { err = "cannot allocate the model in " + Backend::get().name + " memory"; return false; }
    std::vector<float> tmp; std::vector<ggml_fp16_t> h16;
    for (auto& p : pending_) {
        ggml_tensor* d = p.first; const StTensor* s = p.second;
        if (d->type == GGML_TYPE_F16 && s->dtype == "F16") { ggml_backend_tensor_set(d, s->data, 0, s->bytes); continue; }
        tmp.resize((size_t)s->numel()); StFile::toFloat(*s, tmp.data());
        if (d->type == GGML_TYPE_F16) { h16.resize(tmp.size()); ggml_fp32_to_fp16_row(tmp.data(), h16.data(), (int64_t)tmp.size()); ggml_backend_tensor_set(d, h16.data(), 0, h16.size() * 2); }
        else ggml_backend_tensor_set(d, tmp.data(), 0, tmp.size() * 4);
    }
    pending_.clear(); return true;
}

void Weights::release() {
    if (buf_) ggml_backend_buffer_free(buf_); if (ctx_) ggml_free(ctx_); buf_ = nullptr; ctx_ = nullptr; pending_.clear();
}

Graph::Graph(int maxNodes) {
    meta_.resize(ggml_tensor_overhead() * (size_t)(maxNodes * 2 + 64) + ggml_graph_overhead_custom((size_t)maxNodes, false) + 4096);
    ggml_init_params ip = {meta_.size(), meta_.data(), true}; ctx = ggml_init(ip);
    g = ggml_new_graph_custom(ctx, (size_t)maxNodes, false);
}
Graph::~Graph() { if (sched_) ggml_backend_sched_free(sched_); if (ctx) ggml_free(ctx); }

ggml_tensor* Graph::input(ggml_type t, int64_t a, int64_t b, int64_t c, int64_t d) { ggml_tensor* r = ggml_new_tensor_4d(ctx, t, a, b, c, d); ggml_set_input(r); return r; }

bool Graph::alloc(std::string& err) {
    Backend& B = Backend::get(); ggml_backend_t be[2] = {B.main, B.cpu}; ggml_backend_buffer_type_t bt[2] = {ggml_backend_get_default_buffer_type(B.main), ggml_backend_get_default_buffer_type(B.cpu)};
    sched_ = ggml_backend_sched_new(be, bt, B.gpu ? 2 : 1, (size_t)ggml_graph_size(g) + 256, false, false);
    if (!sched_ || !ggml_backend_sched_alloc_graph(sched_, g)) { err = "cannot allocate the graph (not enough memory?)"; return false; }
    return true;
}
bool Graph::run(std::string& err) {
    if (ggml_backend_sched_graph_compute(sched_, g) != GGML_STATUS_SUCCESS) { err = "the graph failed to run"; return false; }
    return true;
}

ggml_tensor* layerNorm(ggml_context* c, ggml_tensor* x, ggml_tensor* w, ggml_tensor* b, float eps) {
    ggml_tensor* r = ggml_norm(c, x, eps);
    if (w) r = ggml_mul(c, r, w);
    if (b) r = ggml_add(c, r, b);
    return r;
}
ggml_tensor* linear(ggml_context* c, ggml_tensor* w, ggml_tensor* b, ggml_tensor* x) {
    ggml_tensor* r = ggml_mul_mat(c, w, x);
    return b ? ggml_add(c, r, b) : r;
}
ggml_tensor* attention(ggml_context* c, ggml_tensor* q, ggml_tensor* k, ggml_tensor* v, float scale) {
    const int64_t hd = q->ne[0], heads = q->ne[1], nq = q->ne[2];
    ggml_tensor* qp = ggml_permute(c, q, 0, 2, 1, 3);                          // [hd, nq, heads]
    ggml_tensor* kp = ggml_permute(c, k, 0, 2, 1, 3);                          // [hd, nk, heads]
    ggml_tensor* vp = ggml_cont(c, ggml_permute(c, v, 1, 2, 0, 3));            // [nk, hd, heads]
    ggml_tensor* kq = ggml_soft_max_ext(c, ggml_mul_mat(c, kp, qp), nullptr, scale, 0.0f);   // [nk, nq, heads]
    ggml_tensor* o = ggml_mul_mat(c, vp, kq);                                  // [hd, nq, heads]
    return ggml_reshape_2d(c, ggml_cont(c, ggml_permute(c, o, 0, 2, 1, 3)), hd * heads, nq);
}
