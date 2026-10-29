#pragma once
// Hooks IDXGISwapChain::Present / ResizeBuffers and ID3D12CommandQueue::ExecuteCommandLists so
// the overlay can render with ImGui inside the game's frame. Vtable addresses are discovered by
// creating a throw-away device + swapchain on a hidden window (no game signatures needed).
#include <functional>
#include <string>

#include "dsloader/plugin_api.h"

namespace dscoop::dx12 {

struct Callbacks {
    /// Called every frame on the render thread with the live swapchain, after ImGui::NewFrame().
    std::function<void()> drawUi;
    /// Return true to swallow the message from the game (menu open & ImGui wants input).
    std::function<bool(void* hwnd, unsigned msg, uintptr_t wparam, intptr_t lparam)> wndProc;
};

bool install(const DSLoaderApi& api, Callbacks callbacks, std::string* err);
void uninstall();
bool isReady();          // ImGui initialised with the real device
unsigned frameCount();
void* gameWindow();
float displayWidth();
float displayHeight();

}  // namespace dscoop::dx12
