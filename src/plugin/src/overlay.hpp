#pragma once
// ImGui UI: menu (host/join/leave, players, chat, settings, debug), world markers over remote
// players (needs camera matrix from recon) and a 2D radar fallback that only needs positions.
#include "coop.hpp"

namespace dscoop {

class Overlay {
public:
    explicit Overlay(CoopManager& coop) : coop_(coop) {}
    void draw();                       // called inside ImGui frame on the render thread
    bool wantsInput() const { return menuOpen_; }
    void toggleMenu() { menuOpen_ = !menuOpen_; }
    bool handleWndProc(void* hwnd, unsigned msg, uintptr_t wparam, intptr_t lparam);
    int menuVirtualKey() const;

private:
    void drawMenu();
    void drawStatusBar();
    void drawMarkers();
    void drawRadar();
    void drawDebug();

    CoopManager& coop_;
    bool menuOpen_ = true;  // open at start so people see the mod is alive
    bool showDebug_ = false;
    char codeBuf_[16] = {0};
    char directBuf_[64] = {0};
    char chatBuf_[240] = {0};
    char nickBuf_[32] = {0};
    char relayBuf_[128] = {0};
    bool buffersInit_ = false;
};

}  // namespace dscoop
