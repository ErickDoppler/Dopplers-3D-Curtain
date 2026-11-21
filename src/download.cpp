// First-start download window and resumable downloader (WinHTTP): the CUDA libraries and the Hunyuan3D model, like in Dopplers Bakery.
#include "download.h"
#include <commctrl.h>
#include <shellapi.h>
#include <winhttp.h>
#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>
#include "sys.h"

HFONT appFont();

namespace {
const wchar_t* kLlamaTag = L"b12282";
const unsigned long long kLlamaZipBytes = 171031017ull, kCudartZipBytes = 426314199ull, kHunyuanBytes = 4928151562ull;
const wchar_t* kHunyuanUrl = L"https://huggingface.co/Comfy-Org/hunyuan3D_2.0_repackaged/resolve/main/split_files/hunyuan3d-dit-v2_fp16.safetensors";
const wchar_t* kCudaDlls[] = {L"ggml-cuda.dll", L"cudart64_13.dll", L"cublas64_13.dll", L"cublasLt64_13.dll"};

std::wstring exeDirW() { return sys::widen(sys::exeDir()); }
std::wstring hunyuanPath() { return sys::widen(hunyuanModelPath()); }
unsigned long long fileSizeW(const std::wstring& p) { WIN32_FILE_ATTRIBUTE_DATA d; if (!GetFileAttributesExW(p.c_str(), GetFileExInfoStandard, &d)) return 0; return ((unsigned long long)d.nFileSizeHigh << 32) | d.nFileSizeLow; }

std::atomic<bool> g_cancel{false}, g_finished{false};
std::atomic<unsigned long long> g_done{0}, g_total{0};
std::mutex g_mx; std::wstring g_text, g_result;
void setText(const std::wstring& s) { std::lock_guard<std::mutex> g(g_mx); g_text = s; }

// url -> dest through dest.part, resumes an unfinished file; returns an error text or empty
std::wstring downloadFile(const std::wstring& url, const std::wstring& dest, unsigned long long expected) {
    URL_COMPONENTSW uc = {sizeof uc}; wchar_t host[256] = {}, path[2048] = {};
    uc.lpszHostName = host; uc.dwHostNameLength = 256; uc.lpszUrlPath = path; uc.dwUrlPathLength = 2048;
    if (!WinHttpCrackUrl(url.c_str(), 0, 0, &uc)) return L"Bad download address.";
    HINTERNET ses = WinHttpOpen(L"DopplersCurtain/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, nullptr, nullptr, 0);
    if (!ses) return L"Cannot start the network session.";
    struct Close { HINTERNET h; ~Close() { if (h) WinHttpCloseHandle(h); } };
    Close cs{ses};
    DWORD redir = WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS; WinHttpSetOption(ses, WINHTTP_OPTION_REDIRECT_POLICY, &redir, sizeof redir);
    HINTERNET con = WinHttpConnect(ses, host, uc.nPort, 0); Close cc{con};
    if (!con) return L"Cannot connect to the server.";
    const std::wstring part = dest + L".part";
    unsigned long long have = fileSizeW(part);
    if (expected && have > expected) { DeleteFileW(part.c_str()); have = 0; }
    HINTERNET req = WinHttpOpenRequest(con, L"GET", path, nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE); Close cr{req};
    if (!req) return L"Cannot open the request.";
    std::wstring hdr = have ? L"Range: bytes=" + std::to_wstring(have) + L"-" : L"";
    if (!WinHttpSendRequest(req, hdr.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : hdr.c_str(), hdr.empty() ? 0 : (DWORD)-1L, nullptr, 0, 0, 0) || !WinHttpReceiveResponse(req, nullptr))
        return L"The server did not answer (" + std::to_wstring(GetLastError()) + L"). Check the internet connection.";
    DWORD status = 0, sz = sizeof status; WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, nullptr, &status, &sz, nullptr);
    if (status == 416 && expected && have == expected) { MoveFileExW(part.c_str(), dest.c_str(), MOVEFILE_REPLACE_EXISTING); return L""; }
    if (status != 200 && status != 206) return L"The server answered with error " + std::to_wstring(status) + L".";
    if (status == 200 && have) have = 0;
    wchar_t lenText[32] = {}; DWORD lsz = sizeof lenText; unsigned long long len = 0;
    if (WinHttpQueryHeaders(req, WINHTTP_QUERY_CONTENT_LENGTH, nullptr, lenText, &lsz, nullptr)) len = _wcstoui64(lenText, nullptr, 10);
    const unsigned long long total = expected ? expected : have + len;
    g_done = have; g_total = total;
    HANDLE f = CreateFileW(part.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, have ? OPEN_ALWAYS : CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return L"Cannot write " + part;
    if (have) SetFilePointer(f, 0, nullptr, FILE_END); else SetEndOfFile(f);
    std::vector<char> buf(1 << 18); unsigned long long done = have; auto last = std::chrono::steady_clock::now();
    for (;;) {
        if (g_cancel) { CloseHandle(f); return L"Download cancelled (it can be resumed)."; }
        DWORD got = 0; if (!WinHttpReadData(req, buf.data(), (DWORD)buf.size(), &got)) { CloseHandle(f); return L"The connection broke (" + std::to_wstring(GetLastError()) + L"). Press Download to resume."; }
        if (!got) break;
        DWORD wr = 0; if (!WriteFile(f, buf.data(), got, &wr, nullptr) || wr != got) { CloseHandle(f); return L"Disk write failed (disk full?)."; }
        done += got; auto now = std::chrono::steady_clock::now();
        if (now - last > std::chrono::milliseconds(150)) { last = now; g_done = done; g_total = total; }
    }
    CloseHandle(f);
    if (expected && fileSizeW(part) != expected) return L"The file is incomplete (" + std::to_wstring(fileSizeW(part) >> 20) + L" MB of " + std::to_wstring(expected >> 20) + L" MB). Press Download to resume.";
    if (!MoveFileExW(part.c_str(), dest.c_str(), MOVEFILE_REPLACE_EXISTING)) return L"Cannot rename the finished download.";
    return L"";
}

std::wstring findFileRec(const std::wstring& dir, const wchar_t* name, int depth = 4) {
    WIN32_FIND_DATAW fd; HANDLE h = FindFirstFileW((dir + L"\\*").c_str(), &fd); if (h == INVALID_HANDLE_VALUE) return L"";
    std::vector<std::wstring> subs; std::wstring hit;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) { if (wcscmp(fd.cFileName, L".") && wcscmp(fd.cFileName, L"..")) subs.push_back(fd.cFileName); }
        else if (_wcsicmp(fd.cFileName, name) == 0) { hit = dir + L"\\" + fd.cFileName; break; }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    if (hit.empty() && depth > 0) for (auto& s : subs) { hit = findFileRec(dir + L"\\" + s, name, depth - 1); if (!hit.empty()) break; }
    return hit;
}
void removeTree(std::wstring dir) {
    dir.push_back(0); dir.push_back(0);
    SHFILEOPSTRUCTW op = {}; op.wFunc = FO_DELETE; op.pFrom = dir.c_str(); op.fFlags = FOF_NOCONFIRMATION | FOF_SILENT | FOF_NOERRORUI; SHFileOperationW(&op);
}
bool unzipTo(const std::wstring& zip, const std::wstring& dest) {                      // tar.exe ships with Windows 10 1803+
    CreateDirectoryW(dest.c_str(), nullptr);
    wchar_t sysdir[MAX_PATH]; GetSystemDirectoryW(sysdir, MAX_PATH);
    std::wstring cl = std::wstring(L"\"") + sysdir + L"\\tar.exe\" -xf \"" + zip + L"\" -C \"" + dest + L"\"";
    STARTUPINFOW si = {sizeof si}; PROCESS_INFORMATION pi = {};
    if (!CreateProcessW(nullptr, &cl[0], nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) return false;
    WaitForSingleObject(pi.hProcess, INFINITE); DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code); CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
    return code == 0;
}
std::wstring installCuda() {
    const std::wstring rt = exeDirW(), work = rt + L"\\cuda_download";
    CreateDirectoryW(work.c_str(), nullptr);
    const std::wstring base = std::wstring(L"https://github.com/ggml-org/llama.cpp/releases/download/") + kLlamaTag + L"/";
    struct Z { std::wstring remote, local, dir; unsigned long long bytes; };
    const Z zs[] = {{std::wstring(L"llama-") + kLlamaTag + L"-bin-win-cuda-13.5-x64.zip", L"llama-cuda.zip", L"x_llama", kLlamaZipBytes},
                    {L"cudart-llama-bin-win-cuda-13.5-x64.zip", L"cudart.zip", L"x_cudart", kCudartZipBytes}};
    int n = 0;
    for (const Z& z : zs) { setText(L"CUDA libraries, archive " + std::to_wstring(++n) + L" of 2"); std::wstring e = downloadFile(base + z.remote, work + L"\\" + z.local, z.bytes); if (!e.empty()) return e; }
    setText(L"Unpacking the CUDA libraries...");
    for (const Z& z : zs) if (!unzipTo(work + L"\\" + z.local, work + L"\\" + z.dir)) return L"Could not unpack " + z.local + L" (needs Windows 10 version 1803 or newer).";
    const struct { const wchar_t* dll; const wchar_t* from; } items[] = {{L"ggml-cuda.dll", L"x_llama"}, {L"cudart64_13.dll", L"x_cudart"}, {L"cublas64_13.dll", L"x_cudart"}, {L"cublasLt64_13.dll", L"x_cudart"}};
    for (auto& it : items) {
        const std::wstring f = findFileRec(work + L"\\" + it.from, it.dll);
        if (f.empty()) return std::wstring(it.dll) + L" is not in the downloaded archive.";
        if (!MoveFileExW(f.c_str(), (rt + L"\\" + it.dll).c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED)) return L"Cannot write " + std::wstring(it.dll) + L" (is the program folder writable?).";
    }
    removeTree(work);
    return L"";
}

struct Setup { HWND wnd = nullptr, list = nullptr, bar = nullptr, status = nullptr, go = nullptr, close = nullptr; bool running = false; std::thread th; } S;
enum { ID_GO = 201, ID_CLOSE };
std::wstring mb(unsigned long long b) { wchar_t t[40]; if (b >= (1ull << 30)) swprintf(t, 40, L"%.1f GB", b / 1073741824.0); else swprintf(t, 40, L"%d MB", (int)(b >> 20)); return t; }
unsigned long long itemBytes(int i) { return i == 0 ? kLlamaZipBytes + kCudartZipBytes : kHunyuanBytes; }
bool itemInstalled(int i) { return i == 0 ? cudaInstalled() : hunyuanInstalled(); }
void rows(bool keep) {
    bool chk[2] = {}; if (keep) for (int i = 0; i < 2; i++) chk[i] = ListView_GetCheckState(S.list, i) != 0;
    ListView_DeleteAllItems(S.list);
    for (int i = 0; i < 2; i++) {
        std::wstring name = i == 0 ? L"CUDA libraries (NVIDIA GPU runtime)" : L"Hunyuan3D 2.0 shape model (image to 3D, fp16)";
        LVITEMW it = {}; it.mask = LVIF_TEXT; it.iItem = i; it.pszText = &name[0]; ListView_InsertItem(S.list, &it);
        std::wstring sz = mb(itemBytes(i)), st = itemInstalled(i) ? L"installed" : L"missing";
        ListView_SetItemText(S.list, i, 1, &sz[0]); ListView_SetItemText(S.list, i, 2, &st[0]);
        ListView_SetCheckState(S.list, i, !itemInstalled(i) && (keep ? chk[i] : true));
    }
}
void worker(std::vector<int> pick) {
    std::wstring err;
    for (int i : pick) {
        if (i == 0) err = installCuda();
        else {
            CreateDirectoryW((exeDirW() + L"\\models").c_str(), nullptr); CreateDirectoryW((exeDirW() + L"\\models\\hunyuan3d").c_str(), nullptr);
            setText(L"Hunyuan3D 2.0 shape model"); err = downloadFile(kHunyuanUrl, hunyuanPath(), kHunyuanBytes);
        }
        if (!err.empty()) break;
    }
    { std::lock_guard<std::mutex> g(g_mx); g_result = err; }
    g_finished = true;
}
LRESULT CALLBACK proc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
    switch (m) {
    case WM_COMMAND:
        if (LOWORD(wp) == ID_GO && !S.running) {
            std::vector<int> pick; unsigned long long need = 0;
            for (int i = 0; i < 2; i++) if (ListView_GetCheckState(S.list, i) && !itemInstalled(i)) { pick.push_back(i); need += itemBytes(i); }
            if (pick.empty()) { SetWindowTextW(S.status, L"Nothing is checked (or it is installed already)."); return 0; }
            ULARGE_INTEGER freeB = {}, a = {}, b = {}; std::wstring dir = exeDirW();
            if (GetDiskFreeSpaceExW(dir.c_str(), &freeB, &a, &b) && freeB.QuadPart < need + (1ull << 30)) { SetWindowTextW(S.status, (L"Not enough free disk space: " + mb(need) + L" needed, " + mb(freeB.QuadPart) + L" free.").c_str()); return 0; }
            S.running = true; g_cancel = false; g_finished = false; g_done = 0; g_total = 0;
            SetWindowTextW(S.go, L"Working..."); EnableWindow(S.go, FALSE); SetWindowTextW(S.close, L"Cancel");
            if (S.th.joinable()) S.th.join();
            S.th = std::thread(worker, pick);
        } else if (LOWORD(wp) == ID_CLOSE) { if (S.running) { g_cancel = true; SetWindowTextW(S.status, L"Cancelling..."); } else DestroyWindow(h); }
        return 0;
    case WM_TIMER:
        if (S.running) {
            const unsigned long long d = g_done, t = g_total; std::wstring txt; { std::lock_guard<std::mutex> g(g_mx); txt = g_text; }
            SendMessageW(S.bar, PBM_SETPOS, t ? (WPARAM)(d * 1000 / t) : 0, 0);
            if (!g_finished) { if (d || t) txt += L"   " + mb(d) + L" / " + mb(t); SetWindowTextW(S.status, txt.c_str()); }
            else {
                if (S.th.joinable()) S.th.join();
                std::wstring err; { std::lock_guard<std::mutex> g(g_mx); err = g_result; }
                S.running = false; SendMessageW(S.bar, PBM_SETPOS, err.empty() ? 1000 : 0, 0);
                SetWindowTextW(S.status, err.empty() ? L"Done." : err.c_str()); SetWindowTextW(S.go, L"Download selected"); EnableWindow(S.go, TRUE); SetWindowTextW(S.close, L"Close");
                rows(false);
            }
        }
        return 0;
    case WM_CLOSE: if (S.running) { g_cancel = true; SetWindowTextW(S.status, L"Cancelling... close again when it stops."); return 0; } DestroyWindow(h); return 0;
    case WM_DESTROY: KillTimer(h, 1); return 0;
    }
    return DefWindowProcW(h, m, wp, lp);
}
}  // namespace

std::string hunyuanModelPath() { return sys::join(sys::join(sys::join(sys::exeDir(), "models"), "hunyuan3d"), "hunyuan3d-dit-v2_fp16.safetensors"); }
bool cudaInstalled() { for (const wchar_t* n : kCudaDlls) if (GetFileAttributesW((exeDirW() + L"\\" + n).c_str()) == INVALID_FILE_ATTRIBUTES) return false; return true; }
bool hunyuanInstalled() { return fileSizeW(hunyuanPath()) == kHunyuanBytes; }

void showSetupDialog(HWND parent, bool first) {
    static bool reg = false; HINSTANCE inst = GetModuleHandleW(nullptr);
    if (!reg) {
        INITCOMMONCONTROLSEX ic = {sizeof ic, ICC_LISTVIEW_CLASSES | ICC_PROGRESS_CLASS}; InitCommonControlsEx(&ic);
        WNDCLASSW wc = {}; wc.lpfnWndProc = proc; wc.hInstance = inst; wc.hCursor = LoadCursorW(nullptr, IDC_ARROW); wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1); wc.lpszClassName = L"D3CSetup";
        wc.hIcon = (HICON)GetClassLongPtrW(parent, GCLP_HICON); RegisterClassW(&wc); reg = true;
    }
    const int W = 700, H = 360; RECT pr; GetWindowRect(parent, &pr);
    RECT r = {0, 0, W, H}; AdjustWindowRect(&r, WS_CAPTION | WS_SYSMENU, FALSE);
    S.running = false;
    S.wnd = CreateWindowExW(WS_EX_DLGMODALFRAME, L"D3CSetup", first ? L"Welcome - download the files for Image to 3D" : L"Download models and CUDA libraries", WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
                            pr.left + ((pr.right - pr.left) - (r.right - r.left)) / 2, pr.top + 100, r.right - r.left, r.bottom - r.top, parent, nullptr, inst, nullptr);
    if (!S.wnd) return;
    auto mk = [&](const wchar_t* cls, const wchar_t* text, DWORD style, int x, int y, int w, int hgt, int id, DWORD ex = 0) {
        HWND c = CreateWindowExW(ex, cls, text, WS_CHILD | WS_VISIBLE | style, x, y, w, hgt, S.wnd, (HMENU)(INT_PTR)id, inst, nullptr); SendMessageW(c, WM_SETFONT, (WPARAM)appFont(), TRUE); return c; };
    mk(L"STATIC", first ? L"Opening and editing 3D models needs nothing more. Turning a picture into a 3D model (Image tabs) needs the Hunyuan3D shape model and the NVIDIA CUDA libraries. "
                          L"The files go next to the program (models\\ and the program folder). You can skip this and download later from File > Download models."
                        : L"The CUDA libraries go into the program folder, the model into models\\hunyuan3d\\.", SS_LEFT, 14, 12, W - 28, 56, 0);
    S.list = mk(WC_LISTVIEWW, L"", LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS | WS_TABSTOP, 14, 72, W - 28, 100, 10, WS_EX_CLIENTEDGE);
    ListView_SetExtendedListViewStyle(S.list, LVS_EX_CHECKBOXES | LVS_EX_FULLROWSELECT);
    const wchar_t* cols[] = {L"File", L"Size", L"State"}; const int cw[] = {440, 100, 100};
    for (int i = 0; i < 3; i++) { LVCOLUMNW c = {}; c.mask = LVCF_TEXT | LVCF_WIDTH; c.pszText = (LPWSTR)cols[i]; c.cx = cw[i]; ListView_InsertColumn(S.list, i, &c); }
    rows(false);
    S.status = mk(L"STATIC", L"Untick what you do not need.", SS_LEFT | SS_ENDELLIPSIS, 14, 190, W - 28, 20, 0);
    S.bar = mk(PROGRESS_CLASSW, L"", PBS_SMOOTH, 14, 216, W - 28, 20, 0); SendMessageW(S.bar, PBM_SETRANGE32, 0, 1000);
    S.go = mk(L"BUTTON", L"Download selected", BS_DEFPUSHBUTTON | WS_TABSTOP, W - 270, 310, 150, 30, ID_GO);
    S.close = mk(L"BUTTON", first ? L"Skip" : L"Close", BS_PUSHBUTTON | WS_TABSTOP, W - 110, 310, 96, 30, ID_CLOSE);
    SetTimer(S.wnd, 1, 200, nullptr);
    EnableWindow(parent, FALSE);
    MSG msg; while (IsWindow(S.wnd) && GetMessageW(&msg, nullptr, 0, 0) > 0) { if (!IsDialogMessageW(S.wnd, &msg)) { TranslateMessage(&msg); DispatchMessageW(&msg); } }
    if (S.th.joinable()) { g_cancel = true; S.th.join(); }
    EnableWindow(parent, TRUE); SetForegroundWindow(parent);
}
