#include "dx12_hook.hpp"

#include <windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>

#include <atomic>
#include <mutex>
#include <vector>

#include "imgui.h"
#include "imgui_impl_dx12.h"
#include "imgui_impl_win32.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace dscoop::dx12 {
namespace {

using PresentFn = HRESULT(WINAPI*)(IDXGISwapChain*, UINT, UINT);
using ResizeBuffersFn = HRESULT(WINAPI*)(IDXGISwapChain*, UINT, UINT, UINT, DXGI_FORMAT, UINT);
using ExecuteCommandListsFn = void(WINAPI*)(ID3D12CommandQueue*, UINT, ID3D12CommandList* const*);

const DSLoaderApi* g_api = nullptr;
Callbacks g_cb;
PresentFn g_origPresent = nullptr;
ResizeBuffersFn g_origResize = nullptr;
ExecuteCommandListsFn g_origExecute = nullptr;
void* g_presentTarget = nullptr;
void* g_resizeTarget = nullptr;
void* g_executeTarget = nullptr;

std::atomic<ID3D12CommandQueue*> g_queue{nullptr};
ID3D12Device* g_device = nullptr;
ID3D12DescriptorHeap* g_rtvHeap = nullptr;
ID3D12DescriptorHeap* g_srvHeap = nullptr;
ID3D12GraphicsCommandList* g_cmdList = nullptr;
struct Frame { ID3D12CommandAllocator* alloc = nullptr; ID3D12Resource* backBuffer = nullptr; D3D12_CPU_DESCRIPTOR_HANDLE rtv{}; };
std::vector<Frame> g_frames;
HWND g_hwnd = nullptr;
WNDPROC g_origWndProc = nullptr;
std::atomic<bool> g_ready{false};
std::atomic<unsigned> g_frameCount{0};
std::atomic<float> g_width{0}, g_height{0};
bool g_initFailed = false;
std::mutex g_mutex;

void log(DSLogLevel lvl, const std::string& s) { if (g_api) g_api->log(lvl, "DSCoop/dx12", s.c_str()); }

template <class T> void release(T*& p) { if (p) { p->Release(); p = nullptr; } }

void releaseFrames() {
    for (auto& f : g_frames) { release(f.backBuffer); }
}
void releaseAll() {
    if (g_ready) {
        ImGui_ImplDX12_Shutdown();
        ImGui_ImplWin32_Shutdown();
        g_ready = false;
    }
    for (auto& f : g_frames) { release(f.backBuffer); release(f.alloc); }
    g_frames.clear();
    release(g_cmdList);
    release(g_rtvHeap);
    release(g_srvHeap);
    release(g_device);
}

LRESULT CALLBACK wndProcHook(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (g_ready) {
        ImGui_ImplWin32_WndProcHandler(hwnd, msg, wp, lp);
        if (g_cb.wndProc && g_cb.wndProc(hwnd, msg, static_cast<uintptr_t>(wp), static_cast<intptr_t>(lp))) return 0;
    }
    return CallWindowProcW(g_origWndProc, hwnd, msg, wp, lp);
}

bool createBackBufferViews(IDXGISwapChain3* sc) {
    const UINT inc = g_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    D3D12_CPU_DESCRIPTOR_HANDLE h = g_rtvHeap->GetCPUDescriptorHandleForHeapStart();
    for (size_t i = 0; i < g_frames.size(); ++i) {
        ID3D12Resource* bb = nullptr;
        if (FAILED(sc->GetBuffer(static_cast<UINT>(i), IID_PPV_ARGS(&bb)))) return false;
        g_device->CreateRenderTargetView(bb, nullptr, h);
        g_frames[i].backBuffer = bb;
        g_frames[i].rtv = h;
        h.ptr += inc;
    }
    return true;
}

bool initWithSwapChain(IDXGISwapChain* swapChain) {
    IDXGISwapChain3* sc = nullptr;
    if (FAILED(swapChain->QueryInterface(IID_PPV_ARGS(&sc)))) { log(DSLOG_ERROR, "swapchain is not IDXGISwapChain3"); return false; }
    DXGI_SWAP_CHAIN_DESC desc{};
    sc->GetDesc(&desc);
    if (FAILED(sc->GetDevice(IID_PPV_ARGS(&g_device)))) { sc->Release(); log(DSLOG_ERROR, "GetDevice failed"); return false; }
    g_hwnd = desc.OutputWindow;
    const UINT buffers = desc.BufferCount;
    g_frames.resize(buffers);

    D3D12_DESCRIPTOR_HEAP_DESC rtv{};
    rtv.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV; rtv.NumDescriptors = buffers; rtv.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    D3D12_DESCRIPTOR_HEAP_DESC srv{};
    srv.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV; srv.NumDescriptors = 1; srv.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    if (FAILED(g_device->CreateDescriptorHeap(&rtv, IID_PPV_ARGS(&g_rtvHeap))) || FAILED(g_device->CreateDescriptorHeap(&srv, IID_PPV_ARGS(&g_srvHeap)))) {
        sc->Release(); log(DSLOG_ERROR, "CreateDescriptorHeap failed"); return false;
    }
    for (auto& f : g_frames)
        if (FAILED(g_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&f.alloc)))) { sc->Release(); return false; }
    if (FAILED(g_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, g_frames[0].alloc, nullptr, IID_PPV_ARGS(&g_cmdList)))) { sc->Release(); return false; }
    g_cmdList->Close();
    if (!createBackBufferViews(sc)) { sc->Release(); log(DSLOG_ERROR, "GetBuffer failed"); return false; }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui::StyleColorsDark();
    ImGui_ImplWin32_Init(g_hwnd);
    ImGui_ImplDX12_Init(g_device, static_cast<int>(buffers), desc.BufferDesc.Format, g_srvHeap,
                        g_srvHeap->GetCPUDescriptorHandleForHeapStart(), g_srvHeap->GetGPUDescriptorHandleForHeapStart());
    g_origWndProc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(g_hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(wndProcHook)));
    sc->Release();
    g_ready = true;
    log(DSLOG_INFO, "ImGui DX12 overlay initialised (" + std::to_string(buffers) + " back buffers, " + std::to_string(desc.BufferDesc.Width) + "x" + std::to_string(desc.BufferDesc.Height) + ")");
    return true;
}

HRESULT WINAPI hkPresent(IDXGISwapChain* swapChain, UINT sync, UINT flags) {
    std::lock_guard<std::mutex> lk(g_mutex);
    g_frameCount++;
    ID3D12CommandQueue* queue = g_queue.load();
    if (!g_ready && !g_initFailed && queue) {
        if (!initWithSwapChain(swapChain)) { g_initFailed = true; releaseAll(); log(DSLOG_ERROR, "overlay init failed — running without UI"); }
    }
    if (g_ready && queue) {
        IDXGISwapChain3* sc = nullptr;
        if (SUCCEEDED(swapChain->QueryInterface(IID_PPV_ARGS(&sc)))) {
            if (g_frames[0].backBuffer == nullptr) createBackBufferViews(sc);  // after ResizeBuffers
            DXGI_SWAP_CHAIN_DESC desc{};
            sc->GetDesc(&desc);
            g_width = static_cast<float>(desc.BufferDesc.Width);
            g_height = static_cast<float>(desc.BufferDesc.Height);

            ImGui_ImplDX12_NewFrame();
            ImGui_ImplWin32_NewFrame();
            ImGui::NewFrame();
            if (g_cb.drawUi) g_cb.drawUi();
            ImGui::Render();

            const UINT idx = sc->GetCurrentBackBufferIndex();
            Frame& f = g_frames[idx % g_frames.size()];
            if (f.backBuffer) {
                f.alloc->Reset();
                g_cmdList->Reset(f.alloc, nullptr);
                D3D12_RESOURCE_BARRIER b{};
                b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                b.Transition.pResource = f.backBuffer;
                b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
                b.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
                b.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
                g_cmdList->ResourceBarrier(1, &b);
                g_cmdList->OMSetRenderTargets(1, &f.rtv, FALSE, nullptr);
                g_cmdList->SetDescriptorHeaps(1, &g_srvHeap);
                ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), g_cmdList);
                std::swap(b.Transition.StateBefore, b.Transition.StateAfter);
                g_cmdList->ResourceBarrier(1, &b);
                g_cmdList->Close();
                ID3D12CommandList* lists[] = {g_cmdList};
                queue->ExecuteCommandLists(1, lists);
            }
            sc->Release();
        }
    }
    return g_origPresent(swapChain, sync, flags);
}

HRESULT WINAPI hkResizeBuffers(IDXGISwapChain* sc, UINT count, UINT w, UINT h, DXGI_FORMAT fmt, UINT flags) {
    std::lock_guard<std::mutex> lk(g_mutex);
    if (g_ready) {
        releaseFrames();
        ImGui_ImplDX12_InvalidateDeviceObjects();
    }
    const HRESULT hr = g_origResize(sc, count, w, h, fmt, flags);
    if (g_ready) {
        ImGui_ImplDX12_CreateDeviceObjects();
        if (count != 0 && count != g_frames.size()) {
            // Buffer count changed: rebuild everything on the next Present.
            releaseAll();
            g_initFailed = false;
            ImGui::DestroyContext();
        }
    }
    return hr;
}

void WINAPI hkExecuteCommandLists(ID3D12CommandQueue* queue, UINT n, ID3D12CommandList* const* lists) {
    if (!g_queue.load() && queue) {
        D3D12_COMMAND_QUEUE_DESC d = queue->GetDesc();
        if (d.Type == D3D12_COMMAND_LIST_TYPE_DIRECT) {
            g_queue = queue;
            log(DSLOG_INFO, "captured direct command queue");
        }
    }
    g_origExecute(queue, n, lists);
}

/// Create a dummy device/swapchain to read vtable pointers.
bool findVtables(std::string* err) {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"DSCoopDummyDX12";
    RegisterClassExW(&wc);
    HWND hwnd = CreateWindowExW(0, wc.lpszClassName, L"dscoop", WS_OVERLAPPEDWINDOW, 0, 0, 64, 64, nullptr, nullptr, wc.hInstance, nullptr);
    if (!hwnd) { if (err) *err = "CreateWindowEx failed"; return false; }
    bool ok = false;
    IDXGIFactory4* factory = nullptr;
    ID3D12Device* device = nullptr;
    ID3D12CommandQueue* queue = nullptr;
    IDXGISwapChain1* sc1 = nullptr;
    do {
        if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) { if (err) *err = "CreateDXGIFactory1 failed"; break; }
        if (FAILED(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device)))) { if (err) *err = "D3D12CreateDevice failed"; break; }
        D3D12_COMMAND_QUEUE_DESC qd{};
        qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        if (FAILED(device->CreateCommandQueue(&qd, IID_PPV_ARGS(&queue)))) { if (err) *err = "CreateCommandQueue failed"; break; }
        DXGI_SWAP_CHAIN_DESC1 sd{};
        sd.BufferCount = 2; sd.Width = 64; sd.Height = 64; sd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT; sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD; sd.SampleDesc.Count = 1;
        if (FAILED(factory->CreateSwapChainForHwnd(queue, hwnd, &sd, nullptr, nullptr, &sc1))) { if (err) *err = "CreateSwapChainForHwnd failed"; break; }
        void** scVt = *reinterpret_cast<void***>(sc1);
        void** qVt = *reinterpret_cast<void***>(queue);
        g_presentTarget = scVt[8];
        g_resizeTarget = scVt[13];
        g_executeTarget = qVt[10];
        ok = true;
    } while (false);
    release(sc1); release(queue); release(device); release(factory);
    DestroyWindow(hwnd);
    UnregisterClassW(wc.lpszClassName, wc.hInstance);
    return ok;
}

}  // namespace

bool install(const DSLoaderApi& api, Callbacks callbacks, std::string* err) {
    g_api = &api;
    g_cb = std::move(callbacks);
    if (!findVtables(err)) return false;
    int st = api.hook_create(g_presentTarget, reinterpret_cast<void*>(&hkPresent), reinterpret_cast<void**>(&g_origPresent));
    if (st != 0) { if (err) *err = "hook Present failed (" + std::to_string(st) + ")"; return false; }
    st = api.hook_create(g_resizeTarget, reinterpret_cast<void*>(&hkResizeBuffers), reinterpret_cast<void**>(&g_origResize));
    if (st != 0) { if (err) *err = "hook ResizeBuffers failed"; return false; }
    st = api.hook_create(g_executeTarget, reinterpret_cast<void*>(&hkExecuteCommandLists), reinterpret_cast<void**>(&g_origExecute));
    if (st != 0) { if (err) *err = "hook ExecuteCommandLists failed"; return false; }
    api.hook_enable(g_executeTarget);
    api.hook_enable(g_resizeTarget);
    api.hook_enable(g_presentTarget);
    log(DSLOG_INFO, "DX12 hooks installed (Present/ResizeBuffers/ExecuteCommandLists)");
    return true;
}

void uninstall() {
    if (!g_api) return;
    g_api->hook_disable(g_presentTarget);
    g_api->hook_disable(g_resizeTarget);
    g_api->hook_disable(g_executeTarget);
    std::lock_guard<std::mutex> lk(g_mutex);
    if (g_hwnd && g_origWndProc) SetWindowLongPtrW(g_hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(g_origWndProc));
    releaseAll();
    if (ImGui::GetCurrentContext()) ImGui::DestroyContext();
    g_api->hook_remove(g_presentTarget);
    g_api->hook_remove(g_resizeTarget);
    g_api->hook_remove(g_executeTarget);
}

bool isReady() { return g_ready; }
unsigned frameCount() { return g_frameCount; }
void* gameWindow() { return g_hwnd; }
float displayWidth() { return g_width; }
float displayHeight() { return g_height; }

}  // namespace dscoop::dx12
