#include "overlay.hpp"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstring>

#include "dscommon/log.hpp"
#include "dx12_hook.hpp"
#include "game_tick_hook.hpp"
#include "imgui.h"

namespace dscoop {
using namespace dsproto;

namespace {
const ImU32 kColors[8] = {IM_COL32(255, 255, 255, 255), IM_COL32(90, 200, 255, 255), IM_COL32(255, 170, 60, 255), IM_COL32(120, 255, 120, 255),
                          IM_COL32(255, 110, 200, 255), IM_COL32(255, 240, 90, 255), IM_COL32(170, 120, 255, 255), IM_COL32(120, 255, 240, 255)};

bool worldToScreen(const Mat4& vp, const std::string& layout, const Vec3& w, float width, float height, ImVec2& out) {
    const float* m = vp.m;
    float x, y, z, wc;
    if (layout == "column_major") {
        x = m[0] * w.x + m[4] * w.y + m[8] * w.z + m[12];
        y = m[1] * w.x + m[5] * w.y + m[9] * w.z + m[13];
        z = m[2] * w.x + m[6] * w.y + m[10] * w.z + m[14];
        wc = m[3] * w.x + m[7] * w.y + m[11] * w.z + m[15];
    } else {
        x = m[0] * w.x + m[1] * w.y + m[2] * w.z + m[3];
        y = m[4] * w.x + m[5] * w.y + m[6] * w.z + m[7];
        z = m[8] * w.x + m[9] * w.y + m[10] * w.z + m[11];
        wc = m[12] * w.x + m[13] * w.y + m[14] * w.z + m[15];
    }
    if (wc < 0.001f) return false;
    (void)z;
    out.x = (x / wc * 0.5f + 0.5f) * width;
    out.y = (1.0f - (y / wc * 0.5f + 0.5f)) * height;
    return true;
}
}  // namespace

int Overlay::menuVirtualKey() const {
    const std::string& k = coop_.config().menuKey;
    if (k.size() >= 2 && (k[0] == 'F' || k[0] == 'f')) {
        const int n = std::atoi(k.c_str() + 1);
        if (n >= 1 && n <= 12) return VK_F1 + n - 1;
    }
    if (k.size() == 1) return std::toupper(static_cast<unsigned char>(k[0]));
    return VK_F9;
}

bool Overlay::handleWndProc(void*, unsigned msg, uintptr_t wparam, intptr_t) {
    if (msg == WM_KEYDOWN && static_cast<int>(wparam) == menuVirtualKey()) {
        toggleMenu();
        return true;
    }
    if (!menuOpen_) return false;
    const ImGuiIO& io = ImGui::GetIO();
    const bool mouse = (msg >= WM_MOUSEFIRST && msg <= WM_MOUSELAST);
    const bool key = (msg >= WM_KEYFIRST && msg <= WM_KEYLAST);
    if (mouse && io.WantCaptureMouse) return true;
    if (key && io.WantCaptureKeyboard) return true;
    return false;
}

void Overlay::draw() {
    std::lock_guard<std::recursive_mutex> lk(coop_.mutex());
    if (!buffersInit_) {
        buffersInit_ = true;
        std::strncpy(codeBuf_, coop_.config().lastCode.c_str(), sizeof(codeBuf_) - 1);
        std::strncpy(directBuf_, coop_.config().directHost.c_str(), sizeof(directBuf_) - 1);
        std::strncpy(nickBuf_, coop_.config().nick.c_str(), sizeof(nickBuf_) - 1);
        std::strncpy(relayBuf_, coop_.config().relayHost.c_str(), sizeof(relayBuf_) - 1);
    }
    ImGuiIO& io = ImGui::GetIO();
    io.MouseDrawCursor = menuOpen_;
    if (dx12::displayHeight() > 0) coop_.bridge().setAspect(dx12::displayWidth() / dx12::displayHeight());
    if (coop_.config().showMarkers) drawMarkers();
    drawStatusBar();
    if (coop_.config().showRadar && coop_.session() && coop_.session()->role() != dsnet::Session::Role::None) drawRadar();
    if (menuOpen_) drawMenu();
    if (showDebug_) drawDebug();
}

void Overlay::drawStatusBar() {
    auto* s = coop_.session();
    ImGui::SetNextWindowPos(ImVec2(10, 10));
    ImGui::SetNextWindowBgAlpha(0.35f);
    ImGui::Begin("##dscoop_status", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoSavedSettings);
    ImGui::TextColored(ImVec4(0.35f, 0.8f, 1.0f, 1.0f), "DSCoop");
    ImGui::SameLine();
    ImGui::TextDisabled("[%s] menu", coop_.config().menuKey.c_str());
    if (s && s->role() != dsnet::Session::Role::None) {
        const auto st = s->stats();
        ImGui::Text("%s | %zu players | rtt %u ms | %u snap/s", s->statusText().c_str(), s->players().size() + 1, st.rttMs, st.snapshotsPerSec);
    } else {
        ImGui::TextDisabled("not in a session");
    }
    if (!coop_.bridge().isAttached()) ImGui::TextColored(ImVec4(1, 0.7f, 0.3f, 1), "game memory: %s", coop_.bridge().status().c_str());
    if (s && s->role() != dsnet::Session::Role::None && !coop_.gameTickHooked())
        ImGui::TextColored(ImVec4(1, 0.7f, 0.3f, 1), "puppets off: %s", coop_.gameTickStatus().c_str());
    ImGui::End();
}

void Overlay::drawMarkers() {
    auto* s = coop_.session();
    if (!s || s->role() == dsnet::Session::Role::None) return;
    auto vp = coop_.bridge().viewProj();
    if (!vp) return;  // camera matrix not found yet -> radar only
    const float w = dx12::displayWidth(), h = dx12::displayHeight();
    const std::string layout = "row_major";
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    const uint32_t now = coop_.nowMs();
    const Vec3 me = coop_.bridge().localState().pos;
    for (const auto& [id, p] : s->players()) {
        auto st = s->remoteStateAt(id, now);
        if (!st) continue;
        ImVec2 sp;
        Vec3 head = st->pos; head.z += 2.0f;
        if (!worldToScreen(*vp, layout, head, w, h, sp)) continue;
        if (sp.x < -50 || sp.y < -50 || sp.x > w + 50 || sp.y > h + 50) continue;
        const float dist = (st->pos - me).length();
        const ImU32 col = kColors[id % 8];
        dl->AddTriangleFilled(ImVec2(sp.x, sp.y), ImVec2(sp.x - 7, sp.y - 12), ImVec2(sp.x + 7, sp.y - 12), col);
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%s  %.0f m", p.nick.c_str(), dist);
        const ImVec2 ts = ImGui::CalcTextSize(buf);
        dl->AddRectFilled(ImVec2(sp.x - ts.x / 2 - 4, sp.y - 32), ImVec2(sp.x + ts.x / 2 + 4, sp.y - 14), IM_COL32(0, 0, 0, 150), 3.0f);
        dl->AddText(ImVec2(sp.x - ts.x / 2, sp.y - 31), col, buf);
    }
}

void Overlay::drawRadar() {
    auto* s = coop_.session();
    const float size = 160.0f;
    const float w = dx12::displayWidth();
    ImGui::SetNextWindowPos(ImVec2(w - size - 20, 10));
    ImGui::SetNextWindowSize(ImVec2(size + 16, size + 40));
    ImGui::SetNextWindowBgAlpha(0.3f);
    ImGui::Begin("##dscoop_radar", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoSavedSettings);
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 c(origin.x + size / 2 + 8, origin.y + size / 2 + 4);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float rangeM = 200.0f;  // radar radius in metres
    dl->AddCircle(c, size / 2, IM_COL32(200, 200, 200, 120), 48);
    dl->AddCircle(c, size / 4, IM_COL32(200, 200, 200, 60), 48);
    dl->AddLine(ImVec2(c.x, c.y - size / 2), ImVec2(c.x, c.y + size / 2), IM_COL32(200, 200, 200, 40));
    dl->AddLine(ImVec2(c.x - size / 2, c.y), ImVec2(c.x + size / 2, c.y), IM_COL32(200, 200, 200, 40));
    const PlayerState me = coop_.bridge().isAttached() ? coop_.bridge().localState() : PlayerState{};
    const float cy = std::cos(-me.yaw), sy = std::sin(-me.yaw);
    dl->AddTriangleFilled(ImVec2(c.x, c.y - 6), ImVec2(c.x - 4, c.y + 4), ImVec2(c.x + 4, c.y + 4), IM_COL32(255, 255, 255, 255));
    const uint32_t now = coop_.nowMs();
    for (const auto& [id, p] : s->players()) {
        auto st = s->remoteStateAt(id, now);
        if (!st) continue;
        const Vec3 d = st->pos - me.pos;
        // rotate so "forward" (yaw) points up
        float rx = d.x * cy - d.y * sy;
        float ry = d.x * sy + d.y * cy;
        const float dist = std::sqrt(rx * rx + ry * ry);
        float scale = (size / 2) / rangeM;
        if (dist > rangeM) { rx = rx / dist * rangeM; ry = ry / dist * rangeM; }
        const ImVec2 pt(c.x + ry * scale, c.y - rx * scale);
        dl->AddCircleFilled(pt, 4.0f, kColors[id % 8]);
        dl->AddText(ImVec2(pt.x + 6, pt.y - 7), kColors[id % 8], p.nick.c_str());
    }
    ImGui::SetCursorScreenPos(ImVec2(origin.x, origin.y + size + 10));
    ImGui::TextDisabled("radar %.0f m", rangeM);
    ImGui::End();
}

void Overlay::drawMenu() {
    auto* s = coop_.session();
    auto& cfg = coop_.config();
    ImGui::SetNextWindowSize(ImVec2(520, 440), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(40, 80), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("DSCoop — Совместная игра", &menuOpen_, ImGuiWindowFlags_NoSavedSettings)) { ImGui::End(); return; }

    const bool inSession = s && s->role() != dsnet::Session::Role::None && s->state() != dsnet::Session::State::Closed;
    if (ImGui::BeginTabBar("tabs")) {
        if (ImGui::BeginTabItem("Сессия")) {
            if (!inSession) {
                ImGui::SeparatorText("Создать сессию");
                ImGui::BeginDisabled(cfg.relayHost.empty());
                if (ImGui::Button("Создать через релей (код приглашения)")) coop_.hostSession(true);
                ImGui::EndDisabled();
                if (cfg.relayHost.empty()) { ImGui::SameLine(); ImGui::TextDisabled("(укажи relay в Настройках)"); }
                if (ImGui::Button("Создать напрямую (P2P / LAN)")) coop_.hostSession(false);
                ImGui::SameLine(); ImGui::TextDisabled("UDP порт %u", cfg.hostPort);

                ImGui::SeparatorText("Войти по коду");
                ImGui::SetNextItemWidth(120);
                ImGui::InputText("##code", codeBuf_, sizeof(codeBuf_), ImGuiInputTextFlags_CharsUppercase | ImGuiInputTextFlags_CharsNoBlank);
                ImGui::SameLine();
                ImGui::BeginDisabled(cfg.relayHost.empty() || std::strlen(codeBuf_) < 6);
                if (ImGui::Button("Войти")) coop_.joinByCode(codeBuf_);
                ImGui::EndDisabled();

                ImGui::SeparatorText("Войти напрямую");
                ImGui::SetNextItemWidth(220);
                ImGui::InputTextWithHint("##direct", "ip:port", directBuf_, sizeof(directBuf_));
                ImGui::SameLine();
                if (ImGui::Button("Подключиться")) coop_.joinDirect(directBuf_);
            } else {
                ImGui::Text("%s", s->statusText().c_str());
                if (s->role() == dsnet::Session::Role::Host && !s->roomCode().empty()) {
                    ImGui::Text("Код приглашения:");
                    ImGui::SameLine();
                    ImGui::TextColored(ImVec4(1, 0.9f, 0.3f, 1), "%s", s->roomCode().c_str());
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Копировать")) ImGui::SetClipboardText(s->roomCode().c_str());
                }
                const auto st = s->stats();
                ImGui::Text("tick %u | rtt %u ms | relay rtt %u ms | snap/s %u | state/s %u | desync %u", st.hostTick, st.rttMs, st.relayRttMs,
                            st.snapshotsPerSec, st.statesPerSec, st.desyncs);
                ImGui::Text("in %llu KB  out %llu KB  lost %llu", static_cast<unsigned long long>(st.transport.bytesReceived / 1024),
                            static_cast<unsigned long long>(st.transport.bytesSent / 1024), static_cast<unsigned long long>(st.transport.packetsLost));
                if (ImGui::Button("Покинуть сессию")) coop_.leaveSession();

                ImGui::SeparatorText("Игроки");
                if (ImGui::BeginTable("players", 5, ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders)) {
                    ImGui::TableSetupColumn("#"); ImGui::TableSetupColumn("Ник"); ImGui::TableSetupColumn("Позиция"); ImGui::TableSetupColumn("Дист"); ImGui::TableSetupColumn("rtt");
                    ImGui::TableHeadersRow();
                    const PlayerState me = coop_.bridge().isAttached() ? coop_.bridge().localState() : PlayerState{};
                    ImGui::TableNextRow(); ImGui::TableNextColumn(); ImGui::Text("%u", s->localId()); ImGui::TableNextColumn(); ImGui::Text("%s (ты)", cfg.nick.c_str());
                    ImGui::TableNextColumn(); ImGui::Text("%.1f %.1f %.1f", me.pos.x, me.pos.y, me.pos.z); ImGui::TableNextColumn(); ImGui::Text("-"); ImGui::TableNextColumn(); ImGui::Text("-");
                    for (const auto& [id, p] : s->players()) {
                        auto rs = s->remoteStateAt(id, coop_.nowMs());
                        ImGui::TableNextRow();
                        ImGui::TableNextColumn(); ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(kColors[id % 8]), "%u", id);
                        ImGui::TableNextColumn(); ImGui::Text("%s", p.nick.c_str());
                        ImGui::TableNextColumn();
                        if (rs) ImGui::Text("%.1f %.1f %.1f%s", rs->pos.x, rs->pos.y, rs->pos.z, (rs->flags & PF_Fake) ? " (fake)" : "");
                        else ImGui::TextDisabled("—");
                        ImGui::TableNextColumn(); if (rs) ImGui::Text("%.0f m", (rs->pos - me.pos).length()); else ImGui::Text("-");
                        ImGui::TableNextColumn(); ImGui::Text("%u", p.rttMs);
                    }
                    ImGui::EndTable();
                }
            }
            ImGui::SeparatorText("События");
            ImGui::BeginChild("events", ImVec2(0, 90), ImGuiChildFlags_Borders);
            for (const auto& e : coop_.eventLog()) ImGui::TextUnformatted(e.c_str());
            if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 5) ImGui::SetScrollHereY(1.0f);
            ImGui::EndChild();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Чат")) {
            ImGui::BeginChild("chat", ImVec2(0, -30), ImGuiChildFlags_Borders);
            for (const auto& c : coop_.chatLog()) ImGui::TextWrapped("%s", c.c_str());
            if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 5) ImGui::SetScrollHereY(1.0f);
            ImGui::EndChild();
            ImGui::SetNextItemWidth(-80);
            const bool enter = ImGui::InputText("##chatinput", chatBuf_, sizeof(chatBuf_), ImGuiInputTextFlags_EnterReturnsTrue);
            ImGui::SameLine();
            if ((ImGui::Button("Отправить") || enter) && chatBuf_[0]) { coop_.sendChat(chatBuf_); chatBuf_[0] = 0; ImGui::SetKeyboardFocusHere(-1); }
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Настройки")) {
            ImGui::BeginDisabled(inSession);
            if (ImGui::InputText("Ник", nickBuf_, sizeof(nickBuf_))) cfg.nick = nickBuf_;
            if (ImGui::InputTextWithHint("Relay host", "relay.example.com", relayBuf_, sizeof(relayBuf_))) cfg.relayHost = relayBuf_;
            int port = cfg.relayControlPort; if (ImGui::InputInt("Relay control port", &port)) cfg.relayControlPort = static_cast<uint16_t>(std::clamp(port, 1, 65535));
            int hp = cfg.hostPort; if (ImGui::InputInt("Host UDP port", &hp)) cfg.hostPort = static_cast<uint16_t>(std::clamp(hp, 1, 65535));
            int tr = static_cast<int>(cfg.tickRate); if (ImGui::SliderInt("Tick rate", &tr, 10, 60)) cfg.tickRate = static_cast<uint32_t>(tr);
            ImGui::SeparatorText("Правила хоста");
            ImGui::Checkbox("PvP (стрельба по портерам, кража груза)", &cfg.pvp);
            ImGui::Checkbox("Контент Director's Cut", &cfg.dcContent);
            ImGui::Checkbox("Синхронизировать погоду/время", &cfg.syncWeather);
            ImGui::EndDisabled();
            ImGui::SeparatorText("Марионетки (этап 2)");
            ImGui::Checkbox("Спавнить настоящих Сэмов для напарников", &cfg.puppets);
            ImGui::TextDisabled("%s", coop_.puppets().status().c_str());
            ImGui::TextDisabled("игровой тик: %s", coop_.gameTickStatus().c_str());
            ImGui::SeparatorText("Интерфейс");
            ImGui::Checkbox("Маркеры над игроками", &cfg.showMarkers);
            ImGui::Checkbox("Радар", &cfg.showRadar);
            ImGui::Checkbox("Автоматически создавать сессию при запуске", &cfg.autoHost);
            if (ImGui::Button("Сохранить")) coop_.saveConfig();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Отладка")) {
            ImGui::Checkbox("Окно отладки (память игры)", &showDebug_);
            if (ImGui::Button("Собрать отчёт (логи + конфиги + дампы)")) coop_.collectReport();
            ImGui::SameLine(); ImGui::TextDisabled("-> dscoop/reports/");
            ImGui::Text("game build %u | loader %s | frame %u", coop_.api().game->build_id, coop_.api().loader_version, dx12::frameCount());
            ImGui::SeparatorText("Лог");
            ImGui::BeginChild("log", ImVec2(0, 0), ImGuiChildFlags_Borders);
            for (const auto& l : dscommon::log::recentLines(60)) ImGui::TextUnformatted(l.c_str());
            if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 5) ImGui::SetScrollHereY(1.0f);
            ImGui::EndChild();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::End();
}

void Overlay::drawDebug() {
    ImGui::SetNextWindowSize(ImVec2(560, 300), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("DSCoop debug — game memory", &showDebug_)) { ImGui::End(); return; }
    auto& b = coop_.bridge();
    ImGui::Text("bridge: %s", b.status().c_str());
    const auto& ls = b.localState();
    ImGui::Text("local pos %.2f %.2f %.2f  yaw %.2f  vel %.2f  hp %.2f", ls.pos.x, ls.pos.y, ls.pos.z, ls.yaw, ls.vel.length(), ls.health);
    ImGui::Text("camera matrix: %s", b.hasCamera() ? "yes" : "no (markers disabled, radar works)");
    ImGui::Text("game tick: %s (%u ticks) | puppets: %s", coop_.gameTickStatus().c_str(), gametick::tickCount(), coop_.puppets().status().c_str());
    if (ImGui::BeginTable("ptrs", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
        ImGui::TableSetupColumn("pointer"); ImGui::TableSetupColumn("type"); ImGui::TableSetupColumn("address"); ImGui::TableSetupColumn("note");
        ImGui::TableHeadersRow();
        for (const auto& p : b.debugPointers()) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn(); ImGui::Text("%s", p.name.c_str());
            ImGui::TableNextColumn(); ImGui::Text("%s", p.type.c_str());
            ImGui::TableNextColumn();
            if (p.ok) ImGui::Text("%016llX", static_cast<unsigned long long>(p.address)); else ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "unresolved");
            ImGui::TableNextColumn(); ImGui::TextWrapped("%s", p.note.c_str());
        }
        ImGui::EndTable();
    }
    ImGui::End();
}

}  // namespace dscoop
