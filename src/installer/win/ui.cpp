// DSCoopSetup.exe — animated installer window (GDI+, no external runtime).
//
// Look: original procedural art (assets/installer/hero.jpg, tools/art/gen_installer_art.py), live
// timefall rain, drifting mist, golden motes and a "chiral network" between the ridges that lights up
// as the install progresses. Custom title bar, frosted panel, animated buttons, page transitions.
// Fonts: Russo One + PT Sans (SIL OFL), embedded. Respects "Show animations in Windows" (off = static).
#include <algorithm>
namespace Gdiplus {
using std::max;
using std::min;
}  // namespace Gdiplus

#include "sys.hpp"

#include <commctrl.h>
#include <dwmapi.h>
#include <objidl.h>  // GDI+ needs COM types that WIN32_LEAN_AND_MEAN leaves out (MSVC)
#include <gdiplus.h>
#include <shlwapi.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <memory>
#include <random>
#include <thread>
#include <unordered_map>

#include "dscommon/version.hpp"
#include "resource_ids.h"
#include "u8.hpp"

namespace dsinst::ui {

using namespace Gdiplus;
namespace fs = std::filesystem;
using win::U;
using win::W;

// =============================================================================== strings
struct Strings {
    const wchar_t* tagline;
    const wchar_t* gameDC;
    const wchar_t* gameOriginal;
    const wchar_t* gameNotFound;
    const wchar_t* pickHint;
    const wchar_t* change;
    const wchar_t* choose;
    const wchar_t* pickTitle;
    const wchar_t* installedState;
    const wchar_t* notInstalled;
    const wchar_t* foreignMod;
    const wchar_t* originalWarn;
    const wchar_t* nickLabel;
    const wchar_t* relayLabel;
    const wchar_t* relayHint;
    const wchar_t* backup;
    const wchar_t* install;
    const wchar_t* update;
    const wchar_t* reinstall;
    const wchar_t* uninstall;
    const wchar_t* footer;
    const wchar_t* confirm;
    const wchar_t* confirmYes;
    const wchar_t* cancel;
    const wchar_t* progressInstall;
    const wchar_t* progressUninstall;
    const wchar_t* progressHint;
    const wchar_t* stepsInstall[4];
    const wchar_t* stepsUninstall[3];
    const wchar_t* stepDone;
    const wchar_t* stepNow;
    const wchar_t* doneTitle;
    const wchar_t* doneText;
    const wchar_t* removedTitle;
    const wchar_t* removedText;
    const wchar_t* play;
    const wchar_t* close;
    const wchar_t* errorTitle;
    const wchar_t* back;
    const wchar_t* gameRunning;
    const wchar_t* needAdmin;
    const wchar_t* noExe;
    const wchar_t* newerInstalled;
    const wchar_t* busy;
    const wchar_t* noGame;
};

const Strings kRu = {
    L"СЮЖЕТНЫЙ КООПЕРАТИВ ДЛЯ DEATH STRANDING",
    L"Death Stranding Director's Cut",
    L"Death Stranding",
    L"Игра не найдена",
    L"Укажи папку, где лежит ds.exe",
    L"Изменить",
    L"Указать папку",
    L"Выбери папку Death Stranding (где лежит ds.exe)",
    L"Установлен DSCoop ",
    L"Мод ещё не установлен",
    L"Другой мод с version.dll продолжит работать",
    L"Мод сделан для Director's Cut",
    L"НИК В КООПЕРАТИВЕ",
    L"СЕРВЕР-РЕЛЕЙ · НЕОБЯЗАТЕЛЬНО",
    L"нужен для входа по коду",
    L"Сделать копию сохранений перед установкой",
    L"УСТАНОВИТЬ",
    L"ОБНОВИТЬ",
    L"ПЕРЕУСТАНОВИТЬ",
    L"Удалить DSCoop",
    L"Файлы игры не изменяются",
    L"Удалить DSCoop из игры? Копии сохранений и настройки останутся в папке dscoop.",
    L"ДА, УДАЛИТЬ",
    L"Отмена",
    L"ПОДКЛЮЧАЕМ К СЕТИ",
    L"ОТКЛЮЧАЕМ ОТ СЕТИ",
    L"Пара секунд — не закрывай окно",
    {L"Копия сохранений", L"Файлы мода", L"Настройки", L"Завершение"},
    {L"Файлы мода", L"Другие моды", L"Уборка"},
    L"готово",
    L"сейчас",
    L"ГОТОВО",
    L"Запусти игру и в главном меню нажми F9 — откроется «Совместная игра».",
    L"УДАЛЕНО",
    L"Игра снова обычная. Копии сохранений остались в папке dscoop.",
    L"ИГРАТЬ",
    L"Закрыть",
    L"НЕ ПОЛУЧИЛОСЬ",
    L"НАЗАД",
    L"Игра сейчас запущена. Закрой Death Stranding и попробуй ещё раз.",
    L"Нужны права администратора: папка игры защищена. Сейчас Windows спросит разрешение.",
    L"В этой папке нет exe-файла игры. Выбери папку, где лежит ds.exe.",
    L"Установлена более новая версия DSCoop. Нажми ещё раз, чтобы заменить её этой.",
    L"Подожди, установка ещё идёт.",
    L"Сначала укажи папку игры.",
};

const Strings kEn = {
    L"STORY CO-OP FOR DEATH STRANDING",
    L"Death Stranding Director's Cut",
    L"Death Stranding",
    L"Game not found",
    L"Point to the folder with ds.exe",
    L"Change",
    L"Choose folder",
    L"Pick the Death Stranding folder (the one with ds.exe)",
    L"Installed: DSCoop ",
    L"The mod is not installed yet",
    L"Your other version.dll mod keeps working",
    L"The mod targets the Director's Cut",
    L"CO-OP NICKNAME",
    L"RELAY SERVER · OPTIONAL",
    L"needed to join by code",
    L"Back up my saves before installing",
    L"INSTALL",
    L"UPDATE",
    L"REINSTALL",
    L"Uninstall DSCoop",
    L"Game files are not modified",
    L"Remove DSCoop from the game? Save backups and settings stay in the dscoop folder.",
    L"YES, REMOVE",
    L"Cancel",
    L"CONNECTING TO THE NETWORK",
    L"DISCONNECTING",
    L"A few seconds — keep this window open",
    {L"Save backup", L"Mod files", L"Settings", L"Finishing"},
    {L"Mod files", L"Other mods", L"Cleanup"},
    L"done",
    L"now",
    L"ALL SET",
    L"Start the game and press F9 in the main menu to open the co-op menu.",
    L"REMOVED",
    L"The game is vanilla again. Save backups stay in the dscoop folder.",
    L"PLAY",
    L"Close",
    L"SOMETHING WENT WRONG",
    L"BACK",
    L"The game is running. Close Death Stranding and try again.",
    L"Administrator rights are needed: the game folder is protected. Windows will ask now.",
    L"No game executable in this folder. Pick the folder with ds.exe.",
    L"A newer DSCoop is installed. Click again to replace it with this one.",
    L"Please wait, still installing.",
    L"Choose the game folder first.",
};

const Strings* S = &kEn;

// =============================================================================== small math
constexpr float kW = 920.f, kH = 580.f;
constexpr float kPi = 3.14159265f;

float clamp01(float v) { return v < 0 ? 0 : v > 1 ? 1 : v; }
float easeOut(float t) { t = clamp01(t); return 1 - (1 - t) * (1 - t) * (1 - t); }
float easeInOut(float t) { t = clamp01(t); return t < 0.5f ? 4 * t * t * t : 1 - std::pow(-2 * t + 2, 3.f) / 2; }

struct Tw {  // exponential smoothing towards a target
    float v = 0, target = 0;
    void step(float dt, float speed) { v += (target - v) * (1 - std::exp(-speed * dt)); }
};

struct R {
    float x = 0, y = 0, w = 0, h = 0;
    bool hit(float px, float py) const { return px >= x && py >= y && px < x + w && py < y + h; }
    RectF rf() const { return RectF(x, y, w, h); }
    R inset(float d) const { return {x + d, y + d, w - 2 * d, h - 2 * d}; }
    float cx() const { return x + w / 2; }
    float cy() const { return y + h / 2; }
};

Color C(uint32_t rgb, float a) {
    return Color(static_cast<BYTE>(clamp01(a) * 255.f + 0.5f), static_cast<BYTE>(rgb >> 16), static_cast<BYTE>(rgb >> 8), static_cast<BYTE>(rgb));
}

// Palette
constexpr uint32_t kGold = 0xF0BE5A, kGoldLight = 0xFFDD94, kGoldDeep = 0xC4852A, kText = 0xE9EFEA, kDim = 0x9AADAA,
                   kFaint = 0x63777A, kLink = 0x8FD6D0, kPanel = 0x0A1316, kField = 0x0F1B1F, kDanger = 0xE07A5F,
                   kOk = 0x86D39A, kAmber = 0xF0B85A;

// =============================================================================== app state
enum class Page { Welcome, Progress, Done, Error };
enum Id : int { kNone = 0, kClose, kMinimize, kChange, kBackup, kPrimary, kSecondary, kField1, kField2, kIdCount };

struct Drop { float x, y, len, speed, alpha, width; };
struct Mote { float x, y, r, vy, phase; };
struct FogBlob { float x, y, rx, ry, speed, alpha; };

struct Fonts {
    PrivateFontCollection pfc;
    std::unique_ptr<FontFamily[]> fams;
    const FontFamily* title = nullptr;
    const FontFamily* body = nullptr;
    std::unique_ptr<Font> logo, heading, big, button, body15, bodyBold15, body13, small12, label11, titlebar, toast;
};

constexpr UINT WM_APP_STEP = WM_APP + 1;
constexpr UINT WM_APP_LOG = WM_APP + 2;
constexpr UINT WM_APP_DONE = WM_APP + 3;
constexpr int kEditNick = 1001, kEditRelay = 1002;

struct App {
    HWND wnd = nullptr;
    HINSTANCE inst = nullptr;
    win::Args args;
    float scale = 1.f;
    bool motion = true;

    // game
    std::optional<steam::GameInstall> game;
    fs::path gameDir;
    bool manualDir = false;
    InstallState st;

    // ui
    Page page = Page::Welcome, nextPage = Page::Welcome;
    bool switching = false;
    float pageAlpha = 0.f, pageOffset = 10.f;
    bool confirmUninstall = false;
    bool backup = true;
    bool replaceNewerArmed = false;
    float replaceArmTime = 0;
    std::array<Tw, kIdCount> hover{};
    Tw focusNick, focusRelay, checkAnim;
    int hot = kNone, pressed = kNone;
    bool tracking = false;
    std::vector<std::pair<int, R>> hits;
    HWND editNick = nullptr, editRelay = nullptr;
    HFONT editFont = nullptr;
    HBRUSH fieldBrush = nullptr;

    // toast
    std::wstring toastText;
    uint32_t toastColor = kAmber;
    float toastUntil = 0;
    Tw toastA;

    // progress
    bool busy = false;
    bool uninstalling = false;
    int workerStep = -1;
    bool workerDone = false, workerOk = false;
    std::wstring workerError, lastLog;
    int shownStep = -1;
    float lastStepTime = 0, allDoneTime = -1;
    Tw strandFill;
    float doneEnter = 0;
    bool doneIsUninstall = false;
    float pendingElevateAt = -1;
    std::wstring pendingElevateParams;
    float pendingCloseAt = -1;

    // time + effects
    float t = 0;
    std::chrono::steady_clock::time_point last;
    std::vector<Drop> rain;
    std::vector<Mote> motes;
    std::vector<FogBlob> fog;
    std::array<Tw, 5> edgeLit{};
    std::array<float, 5> pulse{};
    std::mt19937 rng{20261029};

    // gdi+
    ULONG_PTR gdipToken = 0;
    std::unique_ptr<Fonts> fonts;
    std::unique_ptr<Bitmap> hero;
    std::unique_ptr<CachedBitmap> bgCache;
    std::unique_ptr<GraphicsPath> logoPath;
    RectF logoBounds;
    std::unordered_map<uint64_t, float> charW;
    HDC memDC = nullptr;
    HBITMAP memBmp = nullptr;
    HGDIOBJ memOld = nullptr;
    int bufW = 0, bufH = 0;
};
App g;

float rnd(float a, float b) { return std::uniform_real_distribution<float>(a, b)(g.rng); }

// =============================================================================== network geometry
// Ridge points in design units, produced by tools/art/gen_installer_art.py (assets/installer/nodes.txt).
const PointF kNodes[6] = {{432.4f, 322.3f}, {515.2f, 377.1f}, {649.7f, 392.9f}, {710.0f, 390.0f}, {809.6f, 309.9f}, {874.0f, 378.4f}};
// Edge 0 links the two porters: the co-op link. The rest light up outward as the install goes.
const int kEdges[5][2] = {{2, 3}, {1, 2}, {3, 4}, {0, 1}, {4, 5}};

void edgeControls(int e, PointF& a, PointF& c1, PointF& c2, PointF& b) {
    a = kNodes[kEdges[e][0]];
    b = kNodes[kEdges[e][1]];
    const float dx = b.X - a.X, dy = b.Y - a.Y;
    const float h = std::sqrt(dx * dx + dy * dy) * 0.22f + 14.f;
    c1 = PointF(a.X + dx * 0.3f, a.Y + dy * 0.3f - h);
    c2 = PointF(a.X + dx * 0.7f, a.Y + dy * 0.7f - h);
}

PointF bezier(const PointF& a, const PointF& b, const PointF& c, const PointF& d, float t) {
    const float u = 1 - t;
    return PointF(u * u * u * a.X + 3 * u * u * t * b.X + 3 * u * t * t * c.X + t * t * t * d.X,
                  u * u * u * a.Y + 3 * u * u * t * b.Y + 3 * u * t * t * c.Y + t * t * t * d.Y);
}

// =============================================================================== resources
IStream* streamOf(int id) {
    auto s = win::resource(id);
    if (s.empty()) return nullptr;
    return SHCreateMemStream(s.data(), static_cast<UINT>(s.size()));
}

void loadFonts() {
    g.fonts = std::make_unique<Fonts>();
    Fonts& f = *g.fonts;
    for (int id : {IDR_FONT_TITLE, IDR_FONT_BODY, IDR_FONT_BODY_BOLD}) {
        auto s = win::resource(id);
        if (s.empty()) continue;
        f.pfc.AddMemoryFont(s.data(), static_cast<INT>(s.size()));
        DWORD n = 0;  // also for GDI (edit controls)
        AddFontMemResourceEx(const_cast<uint8_t*>(s.data()), static_cast<DWORD>(s.size()), nullptr, &n);
    }
    const INT count = f.pfc.GetFamilyCount();
    if (count > 0) {
        f.fams.reset(new FontFamily[static_cast<size_t>(count)]);
        INT found = 0;
        f.pfc.GetFamilies(count, f.fams.get(), &found);
        for (INT i = 0; i < found; ++i) {
            WCHAR name[LF_FACESIZE] = {0};
            f.fams[static_cast<size_t>(i)].GetFamilyName(name);
            if (wcsstr(name, L"Russo")) f.title = &f.fams[static_cast<size_t>(i)];
            if (wcsstr(name, L"PT Sans")) f.body = &f.fams[static_cast<size_t>(i)];
        }
    }
    if (!f.title) f.title = FontFamily::GenericSansSerif();
    if (!f.body) f.body = FontFamily::GenericSansSerif();
    auto mk = [](const FontFamily* fam, float size, INT style) { return std::make_unique<Font>(fam, size, style, UnitPixel); };
    f.logo = mk(f.title, 54, FontStyleRegular);
    f.heading = mk(f.title, 21, FontStyleRegular);
    f.big = mk(f.title, 30, FontStyleRegular);
    f.button = mk(f.title, 16.5f, FontStyleRegular);
    f.body15 = mk(f.body, 15, FontStyleRegular);
    f.bodyBold15 = mk(f.body, 15, FontStyleBold);
    f.body13 = mk(f.body, 13.5f, FontStyleRegular);
    f.small12 = mk(f.body, 12.5f, FontStyleRegular);
    f.label11 = mk(f.body, 11, FontStyleBold);
    f.titlebar = mk(f.body, 13, FontStyleBold);
    f.toast = mk(f.body, 13.5f, FontStyleRegular);

    g.logoPath = std::make_unique<GraphicsPath>();
    g.logoPath->AddString(L"DSCOOP", -1, f.title, FontStyleRegular, 56, PointF(0, 0), StringFormat::GenericTypographic());
    g.logoPath->GetBounds(&g.logoBounds);
}

void loadArt() {
    if (IStream* s = streamOf(IDR_ART_HERO)) {
        g.hero = std::make_unique<Bitmap>(s, FALSE);
        s->Release();
        if (g.hero->GetLastStatus() != Ok) g.hero.reset();
    }
}

// =============================================================================== drawing helpers
void roundRect(GraphicsPath& p, const R& r, float rad) {
    const float d = std::min(rad * 2, std::min(r.w, r.h));
    p.AddArc(r.x, r.y, d, d, 180, 90);
    p.AddArc(r.x + r.w - d, r.y, d, d, 270, 90);
    p.AddArc(r.x + r.w - d, r.y + r.h - d, d, d, 0, 90);
    p.AddArc(r.x, r.y + r.h - d, d, d, 90, 90);
    p.CloseFigure();
}

void fillRound(Graphics& gr, const R& r, float rad, const Brush& b) {
    GraphicsPath p;
    roundRect(p, r, rad);
    gr.FillPath(&b, &p);
}

void strokeRound(Graphics& gr, const R& r, float rad, Color c, float w) {
    GraphicsPath p;
    roundRect(p, r, rad);
    Pen pen(c, w);
    gr.DrawPath(&pen, &p);
}

void glowRound(Graphics& gr, const R& r, float rad, uint32_t rgb, float a, int rings = 5, float spread = 2.2f) {
    for (int i = rings; i >= 1; --i) {
        const float d = static_cast<float>(i) * spread;
        SolidBrush b(C(rgb, a * 0.05f * static_cast<float>(rings - i + 1) / static_cast<float>(rings)));
        fillRound(gr, {r.x - d, r.y - d, r.w + 2 * d, r.h + 2 * d}, rad + d, b);
    }
}

void text(Graphics& gr, const std::wstring& s, const Font* f, Color c, const R& r, StringAlignment h = StringAlignmentNear,
          StringAlignment v = StringAlignmentNear, bool wrap = false, StringTrimming trim = StringTrimmingEllipsisCharacter) {
    StringFormat sf;
    sf.SetAlignment(h);
    sf.SetLineAlignment(v);
    sf.SetTrimming(trim);
    if (!wrap) sf.SetFormatFlags(StringFormatFlagsNoWrap);
    SolidBrush b(c);
    gr.DrawString(s.c_str(), static_cast<INT>(s.size()), f, r.rf(), &sf, &b);
}

float charWidth(Graphics& gr, const Font* f, wchar_t ch) {
    const uint64_t key = (reinterpret_cast<uint64_t>(f) << 16) ^ ch;
    auto it = g.charW.find(key);
    if (it != g.charW.end()) return it->second;
    StringFormat sf(StringFormat::GenericTypographic());
    sf.SetFormatFlags(sf.GetFormatFlags() | StringFormatFlagsMeasureTrailingSpaces);
    RectF box;
    gr.MeasureString(&ch, 1, f, PointF(0, 0), &sf, &box);
    g.charW[key] = box.Width;
    return box.Width;
}

/// Letter-spaced text (GDI+ has no tracking). Returns the width.
float tracked(Graphics& gr, const std::wstring& s, const Font* f, Color c, float x, float y, float track, bool draw = true) {
    float w = 0;
    for (wchar_t ch : s) w += charWidth(gr, f, ch) + track;
    w -= track;
    if (!draw) return w;
    SolidBrush b(c);
    const StringFormat* sf = StringFormat::GenericTypographic();
    float cx = x;
    for (wchar_t ch : s) {
        gr.DrawString(&ch, 1, f, PointF(cx, y), sf, &b);
        cx += charWidth(gr, f, ch) + track;
    }
    return w;
}

void trackedCentered(Graphics& gr, const std::wstring& s, const Font* f, Color c, float cx, float y, float track) {
    const float w = tracked(gr, s, f, c, 0, 0, track, false);
    tracked(gr, s, f, c, cx - w / 2, y, track);
}

void ringIcon(Graphics& gr, float cx, float cy, float r, float a) {
    Pen ring(C(kGold, a), r * 0.28f);
    gr.DrawEllipse(&ring, cx - r, cy - r, r * 2, r * 2);
    Pen strand(C(kGoldLight, a), r * 0.2f);
    strand.SetLineCap(LineCapRound, LineCapRound, DashCapRound);
    gr.DrawBezier(&strand, PointF(cx - r * 0.6f, cy + r * 0.25f), PointF(cx - r * 0.2f, cy - r * 0.6f), PointF(cx + r * 0.2f, cy + r * 0.6f),
                  PointF(cx + r * 0.6f, cy - r * 0.25f));
}

// =============================================================================== effects
void initEffects() {
    g.rain.clear();
    for (int i = 0; i < 230; ++i) {
        const bool front = i % 4 == 0;  // ('near' is a macro in windows.h)
        g.rain.push_back({rnd(-40, kW + 120), rnd(-kH, kH), front ? rnd(18, 30) : rnd(9, 18), front ? rnd(520, 680) : rnd(330, 460),
                          front ? rnd(0.14f, 0.26f) : rnd(0.06f, 0.15f), front ? 1.3f : 0.9f});
    }
    g.motes.clear();
    for (int i = 0; i < 34; ++i) g.motes.push_back({rnd(430, kW), rnd(200, kH), rnd(0.8f, 2.1f), rnd(5, 14), rnd(0, 6.28f)});
    g.fog.clear();
    for (int i = 0; i < 6; ++i) g.fog.push_back({rnd(0, kW), rnd(340, 470), rnd(170, 320), rnd(22, 40), rnd(4, 11), rnd(0.05f, 0.09f)});
}

void updateEffects(float dt) {
    if (!g.motion) return;
    for (auto& d : g.rain) {
        d.y += d.speed * dt;
        d.x -= d.speed * dt * 0.17f;
        if (d.y > kH + 30) { d.y = rnd(-80, -10); d.x = rnd(-20, kW + 140); }
    }
    for (auto& m : g.motes) {
        m.y -= m.vy * dt;
        m.x += std::sin(g.t * 0.6f + m.phase) * 6.f * dt;
        if (m.y < 190) { m.y = rnd(460, kH); m.x = rnd(430, kW); }
    }
    for (auto& f : g.fog) {
        f.x += f.speed * dt;
        if (f.x - f.rx > kW) f.x = -f.rx;
    }
    for (int e = 0; e < 5; ++e) {
        g.pulse[static_cast<size_t>(e)] += dt * (0.16f + 0.30f * g.edgeLit[static_cast<size_t>(e)].v);
        if (g.pulse[static_cast<size_t>(e)] > 1) g.pulse[static_cast<size_t>(e)] -= 1;
    }
}

void drawFog(Graphics& gr) {
    for (const auto& f : g.fog) {
        GraphicsPath p;
        p.AddEllipse(f.x - f.rx, f.y - f.ry, f.rx * 2, f.ry * 2);
        PathGradientBrush b(&p);
        b.SetCenterColor(C(0xC8D3D0, f.alpha));
        Color surround = C(0xC8D3D0, 0);
        INT n = 1;
        b.SetSurroundColors(&surround, &n);
        gr.FillPath(&b, &p);
    }
}

void drawNetwork(Graphics& gr) {
    for (int e = 0; e < 5; ++e) {
        const float lit = g.edgeLit[static_cast<size_t>(e)].v;
        PointF a, c1, c2, b;
        edgeControls(e, a, c1, c2, b);
        const float breathe = e == 0 ? 0.85f + 0.15f * std::sin(g.t * 2.2f) : 1.f;
        for (auto [w, al] : {std::pair{7.f, 0.05f}, std::pair{3.5f, 0.12f}, std::pair{1.3f, 0.55f}}) {
            Pen pen(C(kGold, al * (0.25f + 0.75f * lit) * breathe), w);
            pen.SetLineCap(LineCapRound, LineCapRound, DashCapRound);
            if (lit < 0.3f && w < 2.f) {  // unlit strands are dashed
                REAL dash[2] = {3.f, 5.f};
                pen.SetDashPattern(dash, 2);
            }
            gr.DrawBezier(&pen, a, c1, c2, b);
        }
        if (g.motion && lit > 0.05f) {
            for (int k = 0; k < 2; ++k) {
                float tt = g.pulse[static_cast<size_t>(e)] + static_cast<float>(k) * 0.5f;
                if (tt > 1) tt -= 1;
                const PointF p = bezier(a, c1, c2, b, tt);
                SolidBrush halo(C(kGoldLight, 0.16f * lit));
                gr.FillEllipse(&halo, p.X - 5, p.Y - 5, 10.f, 10.f);
                SolidBrush core(C(0xFFF2D2, 0.85f * lit));
                gr.FillEllipse(&core, p.X - 1.6f, p.Y - 1.6f, 3.2f, 3.2f);
            }
        }
    }
    for (int i = 0; i < 6; ++i) {
        const PointF p = kNodes[i];
        const bool pod = i == 2 || i == 3;
        const float pulse = 0.5f + 0.5f * std::sin(g.t * (pod ? 2.6f : 1.4f) + static_cast<float>(i));
        const uint32_t col = pod ? 0xFFB347 : kGold;
        const float rr = pod ? 3.5f + 1.5f * pulse : 7.f + 2.f * pulse;  // pods are small: they sit on the porters' chests
        SolidBrush halo(C(col, pod ? 0.22f + 0.18f * pulse : 0.07f));
        gr.FillEllipse(&halo, p.X - rr, p.Y - rr, rr * 2, rr * 2);
        if (!pod) {
            SolidBrush core(C(kGoldLight, 0.75f));
            gr.FillEllipse(&core, p.X - 2.2f, p.Y - 2.2f, 4.4f, 4.4f);
        }
    }
}

void drawMotes(Graphics& gr) {
    for (const auto& m : g.motes) {
        const float tw = 0.35f + 0.65f * (0.5f + 0.5f * std::sin(g.t * 2.f + m.phase * 3.f));
        const float fade = clamp01((m.y - 190) / 120.f);
        SolidBrush halo(C(kGold, 0.10f * tw * fade));
        gr.FillEllipse(&halo, m.x - m.r * 3, m.y - m.r * 3, m.r * 6, m.r * 6);
        SolidBrush core(C(kGoldLight, 0.55f * tw * fade));
        gr.FillEllipse(&core, m.x - m.r * 0.6f, m.y - m.r * 0.6f, m.r * 1.2f, m.r * 1.2f);
    }
}

void drawRain(Graphics& gr) {
    Pen pen(C(0xCAD6D4, 0.1f), 1.f);
    for (const auto& d : g.rain) {
        pen.SetColor(C(0xCAD6D4, d.alpha));
        pen.SetWidth(d.width);
        gr.DrawLine(&pen, d.x, d.y, d.x + d.len * 0.17f, d.y - d.len);
    }
}

// =============================================================================== widgets
void addHit(int id, const R& r) { g.hits.emplace_back(id, r); }
float hov(int id) { return g.hover[static_cast<size_t>(id)].v; }

enum class BtnStyle { Gold, Danger, Ghost };

void button(Graphics& gr, int id, const R& r0, const std::wstring& label, BtnStyle style, float a, bool enabled = true) {
    addHit(id, r0);
    const float h = enabled ? hov(id) : 0.f;
    const bool down = g.pressed == id && g.hot == id;
    R r = down ? r0.inset(1.5f) : r0;
    if (style == BtnStyle::Ghost) {
        SolidBrush fill(C(0xFFFFFF, (0.03f + 0.05f * h) * a));
        fillRound(gr, r, 12, fill);
        strokeRound(gr, r, 12, C(0xFFFFFF, (0.16f + 0.2f * h) * a), 1.f);
        text(gr, label, g.fonts->bodyBold15.get(), C(kText, (0.8f + 0.2f * h) * a), r, StringAlignmentCenter, StringAlignmentCenter);
        return;
    }
    const uint32_t top = style == BtnStyle::Gold ? kGoldLight : 0xF19A82;
    const uint32_t mid = style == BtnStyle::Gold ? kGold : kDanger;
    const uint32_t bot = style == BtnStyle::Gold ? kGoldDeep : 0xA9452F;
    const float ea = enabled ? a : a * 0.35f;
    if (g.motion) glowRound(gr, r, 12, mid, ea * (0.35f + 0.65f * h), 6, 2.6f);
    LinearGradientBrush grad(PointF(0, r.y), PointF(0, r.y + r.h), C(top, ea), C(bot, ea));
    Color cols[3] = {C(top, ea), C(mid, ea), C(bot, ea)};
    REAL pos[3] = {0.f, 0.45f, 1.f};
    grad.SetInterpolationColors(cols, pos, 3);
    fillRound(gr, r, 12, grad);
    // glossy top edge + hover sheen
    LinearGradientBrush gloss(PointF(0, r.y), PointF(0, r.y + r.h * 0.5f), C(0xFFFFFF, (0.22f + 0.12f * h) * ea), C(0xFFFFFF, 0));
    fillRound(gr, {r.x + 1, r.y + 1, r.w - 2, r.h * 0.5f}, 11, gloss);
    if (g.motion && h > 0.01f) {
        const float sweep = std::fmod(g.t * 0.8f, 1.6f) - 0.3f;
        const float sx = r.x + r.w * sweep;
        GraphicsPath clip;
        roundRect(clip, r, 12);
        gr.SetClip(&clip);
        LinearGradientBrush sheen(PointF(sx - 40, 0), PointF(sx + 40, 0), C(0xFFFFFF, 0), C(0xFFFFFF, 0));
        Color sc[3] = {C(0xFFFFFF, 0), C(0xFFFFFF, 0.28f * h * ea), C(0xFFFFFF, 0)};
        REAL sp[3] = {0.f, 0.5f, 1.f};
        sheen.SetInterpolationColors(sc, sp, 3);
        gr.FillRectangle(&sheen, sx - 40, r.y, 80.f, r.h);
        gr.ResetClip();
    }
    const uint32_t ink = style == BtnStyle::Gold ? 0x1C1406 : 0xFFF4EE;
    trackedCentered(gr, label, g.fonts->button.get(), C(ink, ea), r.cx(), r.cy() - 10.5f, 1.6f);
}

void link(Graphics& gr, int id, float x, float y, const std::wstring& s, float a, bool alignRight = false, uint32_t color = kLink) {
    RectF box;
    gr.MeasureString(s.c_str(), static_cast<INT>(s.size()), g.fonts->body13.get(), PointF(0, 0), &box);
    const float lx = alignRight ? x - box.Width : x;
    const R r{lx - 4, y - 3, box.Width + 8, box.Height + 6};
    addHit(id, r);
    const float h = hov(id);
    SolidBrush b(C(color, (0.78f + 0.22f * h) * a));
    gr.DrawString(s.c_str(), static_cast<INT>(s.size()), g.fonts->body13.get(), PointF(lx, y), &b);
    if (h > 0.02f) {
        Pen u(C(color, 0.8f * h * a), 1.f);
        gr.DrawLine(&u, lx + 2, y + box.Height - 2, lx + 2 + (box.Width - 4) * h, y + box.Height - 2);
    }
}

void checkbox(Graphics& gr, int id, float x, float y, const std::wstring& label, bool on, float a) {
    RectF box;
    gr.MeasureString(label.c_str(), static_cast<INT>(label.size()), g.fonts->body13.get(), PointF(0, 0), &box);
    addHit(id, {x - 4, y - 4, 28 + box.Width + 8, 28});
    const float h = hov(id);
    const float c = g.checkAnim.v;
    const R b{x, y, 20, 20};
    SolidBrush bg(C(kField, a));
    fillRound(gr, b, 5, bg);
    if (c > 0.01f) {
        LinearGradientBrush gold(PointF(0, y), PointF(0, y + 20), C(kGoldLight, a * c), C(kGoldDeep, a * c));
        fillRound(gr, b, 5, gold);
        Pen ck(C(0x1C1406, a * c), 2.4f);
        ck.SetLineCap(LineCapRound, LineCapRound, DashCapRound);
        ck.SetLineJoin(LineJoinRound);
        PointF pts[3] = {PointF(x + 5, y + 10.5f), PointF(x + 8.6f, y + 14), PointF(x + 15.5f, y + 6.5f)};
        gr.DrawLines(&ck, pts, 3);
    }
    strokeRound(gr, b, 5, C(on ? kGold : 0xFFFFFF, (on ? 0.6f : 0.22f + 0.25f * h) * a), 1.f);
    SolidBrush tb(C(kText, (0.82f + 0.18f * h) * a));
    gr.DrawString(label.c_str(), static_cast<INT>(label.size()), g.fonts->body13.get(), PointF(x + 30, y + 1), &tb);
    (void)on;
}

void field(Graphics& gr, int id, const R& r, const std::wstring& label, const std::wstring& hint, float focus, float a) {
    tracked(gr, label, g.fonts->label11.get(), C(kDim, a), r.x + 2, r.y - 19, 1.4f);
    if (!hint.empty()) {
        RectF lb;
        const float lw = tracked(gr, label, g.fonts->label11.get(), C(kDim, 0), 0, 0, 1.4f, false);
        (void)lb;
        text(gr, hint, g.fonts->small12.get(), C(kFaint, a), {r.x + lw + 10, r.y - 20.5f, r.w - lw - 10, 16});
    }
    addHit(id, r);
    if (g.motion && focus > 0.01f) glowRound(gr, r, 10, kGold, focus * a * 0.8f, 4, 2.f);
    SolidBrush bg(C(kField, a));
    fillRound(gr, r, 10, bg);
    const float h = hov(id);
    strokeRound(gr, r, 10, C(focus > 0.5f ? kGold : 0xFFFFFF, focus > 0.5f ? 0.75f * a : (0.12f + 0.12f * h) * a), 1.f);
}

// =============================================================================== state helpers
void refreshState() { g.st = g.gameDir.empty() ? InstallState{} : inspect(g.gameDir); }

void toast(const std::wstring& s, uint32_t color = kAmber, float seconds = 5.5f) {
    g.toastText = s;
    g.toastColor = color;
    g.toastUntil = g.t + seconds;
}

void goPage(Page p) {
    if (g.page == p && !g.switching) return;
    g.nextPage = p;
    g.switching = true;
}

std::wstring editText(HWND e) {
    const int n = GetWindowTextLengthW(e);
    std::wstring s(static_cast<size_t>(n), L'\0');
    GetWindowTextW(e, s.data(), n + 1);
    return s;
}

void placeEdits() {
    const bool show = g.page == Page::Welcome && !g.switching && !g.confirmUninstall && g.pageAlpha > 0.6f;
    auto place = [&](HWND e, const R& r) {
        SetWindowPos(e, nullptr, static_cast<int>((r.x + 14) * g.scale), static_cast<int>((r.y + 11) * g.scale + g.pageOffset * g.scale),
                     static_cast<int>((r.w - 28) * g.scale), static_cast<int>(21 * g.scale), SWP_NOZORDER | SWP_NOACTIVATE);
        ShowWindow(e, show ? SW_SHOWNA : SW_HIDE);
    };
    place(g.editNick, {60, 290, 336, 42});
    place(g.editRelay, {60, 362, 336, 42});
}

// =============================================================================== actions
void startWorker(bool uninstalling) {
    g.busy = true;
    g.uninstalling = uninstalling;
    g.workerStep = -1;
    g.workerDone = false;
    g.workerOk = false;
    g.workerError.clear();
    g.lastLog.clear();
    g.shownStep = -1;
    g.lastStepTime = g.t;
    g.allDoneTime = -1;
    g.strandFill = Tw{};
    goPage(Page::Progress);
    HWND wnd = g.wnd;
    auto onStep = [wnd](int k) { PostMessageW(wnd, WM_APP_STEP, static_cast<WPARAM>(k), 0); };
    auto onLog = [wnd](const std::string& l) { PostMessageW(wnd, WM_APP_LOG, 0, reinterpret_cast<LPARAM>(new std::wstring(W(l)))); };
    auto done = [wnd](const Result& r) { PostMessageW(wnd, WM_APP_DONE, r.ok ? 1 : 0, reinterpret_cast<LPARAM>(new std::wstring(W(r.error)))); };
    if (uninstalling) {
        const fs::path dir = g.gameDir;
        std::thread([dir, onStep, onLog, done] {
            Result r = uninstall(dir, false, onLog, onStep);
            if (r.ok) win::unregisterUninstall();
            done(r);
        }).detach();
    } else {
        InstallOptions o;
        o.gameDir = g.gameDir;
        o.payload = win::payload();
        o.version = DSCOOP_VERSION_STRING;
        o.nick = U(editText(g.editNick));
        win::splitRelay(editText(g.editRelay), o.relayHost, o.relayPort);
        o.backupSaves = g.backup;
        o.saveDirs = win::saveDirs();
        o.installerExe = win::selfPath();
        o.onStep = onStep;
        std::thread([o, onLog, done] {
            Result r = install(o, onLog);
            if (r.ok) win::registerUninstall(o.gameDir);
            done(r);
        }).detach();
    }
}

std::wstring elevateParams(bool uninstalling) {
    std::wstring p = L"/dir=\"" + g.gameDir.wstring() + L"\" /autostart";
    if (uninstalling) p += L" /uninstall";
    const std::wstring nick = editText(g.editNick), relay = editText(g.editRelay);
    if (!nick.empty()) p += L" \"/nick=" + nick + L"\"";
    if (!relay.empty()) p += L" \"/relay=" + relay + L"\"";
    if (!g.backup) p += L" /nobackup";
    return p;
}

/// Checks shared by install and uninstall. False = do not start (a toast explains why).
bool preflight(bool uninstalling) {
    if (g.gameDir.empty()) { toast(S->noGame); return false; }
    if (win::processRunning(steam::findGameExe(g.gameDir))) { toast(S->gameRunning, kDanger); return false; }
    if (!canWrite(g.gameDir)) {
        if (win::isElevated()) { toast(W("no write access to " + utf8(g.gameDir)), kDanger); return false; }
        toast(S->needAdmin, kAmber, 3.f);
        g.pendingElevateAt = g.t + 1.4f;
        g.pendingElevateParams = elevateParams(uninstalling);
        return false;
    }
    return true;
}

void doInstall() {
    if (g.busy) return;
    refreshState();
    if (g.st.installed && compareVersions(g.st.version, DSCOOP_VERSION_STRING) > 0 && !(g.replaceNewerArmed && g.t - g.replaceArmTime < 8)) {
        g.replaceNewerArmed = true;
        g.replaceArmTime = g.t;
        toast(S->newerInstalled);
        return;
    }
    if (!preflight(false)) return;
    startWorker(false);
}

void doUninstall() {
    if (g.busy) return;
    if (!preflight(true)) return;
    g.confirmUninstall = false;
    startWorker(true);
}

void chooseFolder() {
    if (g.busy) return;
    if (auto p = win::pickFolder(g.wnd, S->pickTitle)) {
        if (steam::findGameExe(*p).empty()) { toast(S->noExe); return; }
        g.gameDir = *p;
        std::error_code ec;
        g.manualDir = !(g.game && fs::equivalent(g.game->gameDir, *p, ec));
        refreshState();
        std::string nick, relay;
        win::readSettings(g.gameDir, nick, relay);
        if (!nick.empty()) SetWindowTextW(g.editNick, W(nick).c_str());
        if (!relay.empty()) SetWindowTextW(g.editRelay, W(relay).c_str());
    }
}

void requestClose() {
    if (g.busy) { toast(S->busy); return; }
    DestroyWindow(g.wnd);
}

void activate(int id) {
    switch (id) {
        case kClose: requestClose(); break;
        case kMinimize: ShowWindow(g.wnd, SW_MINIMIZE); break;
        case kChange: chooseFolder(); break;
        case kBackup: g.backup = !g.backup; break;
        case kField1: SetFocus(g.editNick); break;
        case kField2: SetFocus(g.editRelay); break;
        case kPrimary:
            if (g.page == Page::Welcome) { g.confirmUninstall ? doUninstall() : doInstall(); }
            else if (g.page == Page::Done) {
                if (!g.doneIsUninstall) { win::launchGame(g.game ? g.game->appId : ""); g.pendingCloseAt = g.t + 0.7f; }
                else requestClose();
            } else if (g.page == Page::Error) { refreshState(); goPage(Page::Welcome); }
            break;
        case kSecondary:
            if (g.page == Page::Welcome) g.confirmUninstall = !g.confirmUninstall;
            else if (g.page == Page::Done) requestClose();
            break;
        default: break;
    }
}

// =============================================================================== pages
void drawLogo(Graphics& gr, float cx, float y, float a, float sizeScale = 1.f) {
    const RectF& b = g.logoBounds;
    GraphicsState saved = gr.Save();
    gr.TranslateTransform(cx - (b.X + b.Width / 2) * sizeScale, y - b.Y * sizeScale);
    gr.ScaleTransform(sizeScale, sizeScale);
    if (g.motion) {
        for (auto [w, al] : {std::pair{10.f, 0.035f}, std::pair{5.f, 0.07f}}) {
            Pen halo(C(kGold, al * a), w);
            halo.SetLineJoin(LineJoinRound);
            gr.DrawPath(&halo, g.logoPath.get());
        }
    }
    LinearGradientBrush fill(PointF(0, b.Y), PointF(0, b.Y + b.Height), C(kGoldLight, a), C(kGoldDeep, a));
    Color cols[3] = {C(0xFFF0C8, a), C(kGold, a), C(kGoldDeep, a)};
    REAL pos[3] = {0.f, 0.5f, 1.f};
    fill.SetInterpolationColors(cols, pos, 3);
    gr.FillPath(&fill, g.logoPath.get());
    if (g.motion) {  // shimmer sweeping across every ~5 s
        const float cycle = std::fmod(g.t, 5.f) / 1.4f;
        if (cycle < 1.f) {
            const float sx = b.X - 60 + (b.Width + 120) * easeInOut(cycle);
            gr.SetClip(g.logoPath.get());
            LinearGradientBrush sh(PointF(sx - 45, 0), PointF(sx + 45, 0), C(0xFFFFFF, 0), C(0xFFFFFF, 0));
            Color sc[3] = {C(0xFFFFFF, 0), C(0xFFFFFF, 0.55f * a), C(0xFFFFFF, 0)};
            REAL sp[3] = {0.f, 0.5f, 1.f};
            sh.SetInterpolationColors(sc, sp, 3);
            gr.FillRectangle(&sh, sx - 45, b.Y, 90.f, b.Height);
            gr.ResetClip();
        }
    }
    gr.Restore(saved);
}

void drawGameCard(Graphics& gr, float a) {
    const R card{60, 168, 336, 86};
    SolidBrush bg(C(0xFFFFFF, 0.035f * a));
    fillRound(gr, card, 12, bg);
    strokeRound(gr, card, 12, C(0xFFFFFF, 0.08f * a), 1.f);
    uint32_t dot = kDanger;
    std::wstring title = S->gameNotFound, sub = S->pickHint;
    if (!g.gameDir.empty()) {
        sub = g.gameDir.wstring();
        if (g.game && !g.manualDir) {
            title = g.game->directorsCut() ? S->gameDC : S->gameOriginal;
            dot = g.game->directorsCut() ? kOk : kAmber;
        } else {
            title = g.gameDir.filename().wstring();
            dot = kAmber;
        }
    }
    const float pulse = g.motion ? 0.5f + 0.5f * std::sin(g.t * 3.f) : 0.5f;
    SolidBrush halo(C(dot, (0.12f + 0.12f * pulse) * a));
    gr.FillEllipse(&halo, card.x + 18 - 8.f, card.y + 25 - 8.f, 16.f, 16.f);
    SolidBrush core(C(dot, a));
    gr.FillEllipse(&core, card.x + 18 - 4.f, card.y + 25 - 4.f, 8.f, 8.f);
    text(gr, title, g.fonts->bodyBold15.get(), C(kText, a), {card.x + 34, card.y + 14, 200, 22});
    link(gr, kChange, card.x + card.w - 14, card.y + 15, g.gameDir.empty() ? S->choose : S->change, a, true);
    text(gr, sub, g.fonts->small12.get(), C(kFaint, a), {card.x + 34, card.y + 37, card.w - 48, 18}, StringAlignmentNear, StringAlignmentNear, false,
         StringTrimmingEllipsisPath);
    std::wstring state = S->notInstalled;
    uint32_t sc = kFaint;
    if (g.st.installed) { state = std::wstring(S->installedState) + W(g.st.version); sc = kLink; }
    if (g.st.foreignLoader || g.st.chainedForeign) { state += L" · "; state += S->foreignMod; }
    if (g.game && !g.game->directorsCut() && !g.manualDir) { state = S->originalWarn; sc = kAmber; }
    text(gr, state, g.fonts->small12.get(), C(sc, a), {card.x + 34, card.y + 57, card.w - 48, 18});
}

void drawWelcome(Graphics& gr, float a) {
    drawLogo(gr, 228, 76, a);
    trackedCentered(gr, S->tagline, g.fonts->label11.get(), C(kDim, a), 228, 138, 1.6f);
    drawGameCard(gr, a);
    if (!g.confirmUninstall) {
        field(gr, kField1, {60, 290, 336, 42}, S->nickLabel, L"", g.focusNick.v, a);
        field(gr, kField2, {60, 362, 336, 42}, S->relayLabel, L"", g.focusRelay.v, a);
        checkbox(gr, kBackup, 62, 418, S->backup, g.backup, a);
        std::wstring label = S->install;
        if (g.st.installed) label = compareVersions(g.st.version, DSCOOP_VERSION_STRING) < 0 ? S->update : S->reinstall;
        button(gr, kPrimary, {60, 452, 336, 52}, label, BtnStyle::Gold, a, !g.gameDir.empty());
        if (g.st.installed || g.st.loaderIsOurs) link(gr, kSecondary, 60, 518, S->uninstall, a, false, 0xE6A08E);
        text(gr, S->footer, g.fonts->small12.get(), C(kFaint, a), {200, 519, 196, 18}, StringAlignmentFar);
    } else {
        const R box{60, 286, 336, 140};
        SolidBrush bg(C(kDanger, 0.07f * a));
        fillRound(gr, box, 12, bg);
        strokeRound(gr, box, 12, C(kDanger, 0.35f * a), 1.f);
        text(gr, S->confirm, g.fonts->body15.get(), C(kText, a), box.inset(18), StringAlignmentNear, StringAlignmentCenter, true);
        button(gr, kPrimary, {60, 452, 336, 52}, S->confirmYes, BtnStyle::Danger, a);
        link(gr, kSecondary, 60, 518, S->cancel, a);
    }
}

void drawProgress(Graphics& gr, float a) {
    drawLogo(gr, 228, 76, a, 0.78f);
    trackedCentered(gr, g.uninstalling ? S->progressUninstall : S->progressInstall, g.fonts->heading.get(), C(kText, a), 228, 150, 1.4f);
    text(gr, S->progressHint, g.fonts->body13.get(), C(kDim, a), {60, 182, 336, 20}, StringAlignmentCenter);
    const int n = g.uninstalling ? kUninstallSteps : kInstallSteps;
    const float x = 96, y0 = 238, y1 = 238 + (n == 4 ? 210.f : 200.f);
    const float gap = (y1 - y0) / static_cast<float>(n - 1);
    // base strand
    Pen base(C(0xFFFFFF, 0.10f * a), 2.f);
    gr.DrawLine(&base, x, y0, x, y1);
    // filled strand with glow
    const float fill = clamp01(g.strandFill.v / static_cast<float>(n - 1));
    const float yf = y0 + (y1 - y0) * fill;
    for (auto [w, al] : {std::pair{9.f, 0.06f}, std::pair{5.f, 0.12f}, std::pair{2.2f, 0.9f}}) {
        Pen p(C(kGold, al * a), w);
        p.SetLineCap(LineCapRound, LineCapRound, DashCapRound);
        if (yf > y0 + 0.5f) gr.DrawLine(&p, x, y0, x, yf);
    }
    if (g.motion && yf > y0 + 4) {  // light flowing down the strand
        for (int k = 0; k < 3; ++k) {
            const float tt = std::fmod(g.t * 0.9f + static_cast<float>(k) / 3.f, 1.f);
            const float py = y0 + (yf - y0) * tt;
            SolidBrush hb(C(kGoldLight, 0.25f * a));
            gr.FillEllipse(&hb, x - 6, py - 6, 12.f, 12.f);
            SolidBrush cb(C(0xFFF4DA, 0.9f * a));
            gr.FillEllipse(&cb, x - 2, py - 2, 4.f, 4.f);
        }
    }
    for (int i = 0; i < n; ++i) {
        const float y = y0 + gap * static_cast<float>(i);
        const bool done = g.shownStep > i || (g.shownStep >= n);
        const bool now = g.shownStep == i;
        if (done) {
            SolidBrush halo(C(kGold, 0.18f * a));
            gr.FillEllipse(&halo, x - 13, y - 13, 26.f, 26.f);
            LinearGradientBrush gold(PointF(0, y - 9), PointF(0, y + 9), C(kGoldLight, a), C(kGoldDeep, a));
            gr.FillEllipse(&gold, x - 9, y - 9, 18.f, 18.f);
            Pen ck(C(0x1C1406, a), 2.2f);
            ck.SetLineCap(LineCapRound, LineCapRound, DashCapRound);
            PointF pts[3] = {PointF(x - 4.2f, y + 0.2f), PointF(x - 1.2f, y + 3.4f), PointF(x + 4.6f, y - 3.6f)};
            gr.DrawLines(&ck, pts, 3);
        } else if (now) {
            const float p = g.motion ? 0.5f + 0.5f * std::sin(g.t * 6.f) : 0.5f;
            Pen ring(C(kGold, (0.35f + 0.5f * p) * a), 2.f);
            const float rr = 11.f + 4.f * p;
            gr.DrawEllipse(&ring, x - rr, y - rr, rr * 2, rr * 2);
            SolidBrush core(C(kGoldLight, a));
            gr.FillEllipse(&core, x - 5, y - 5, 10.f, 10.f);
        } else {
            SolidBrush bg(C(kPanel, a));
            gr.FillEllipse(&bg, x - 8, y - 8, 16.f, 16.f);
            Pen ring(C(0xFFFFFF, 0.25f * a), 1.5f);
            gr.DrawEllipse(&ring, x - 8, y - 8, 16.f, 16.f);
        }
        const wchar_t* label = g.uninstalling ? S->stepsUninstall[i] : S->stepsInstall[i];
        text(gr, label, done || now ? g.fonts->bodyBold15.get() : g.fonts->body15.get(), C(done || now ? kText : kFaint, a), {x + 28, y - 11, 220, 22});
        if (done || now) text(gr, done ? S->stepDone : S->stepNow, g.fonts->small12.get(), C(done ? kGold : kDim, 0.9f * a), {x + 230, y - 9, 70, 20}, StringAlignmentFar);
    }
    text(gr, g.lastLog, g.fonts->small12.get(), C(kFaint, 0.9f * a), {60, 506, 336, 18}, StringAlignmentNear, StringAlignmentNear, false,
         StringTrimmingEllipsisPath);
}

void drawResult(Graphics& gr, float a, bool ok) {
    const float t = g.t - g.doneEnter;
    const float cx = 228, cy = 226, r = 52;
    const float ringP = g.motion ? easeOut(t / 0.65f) : 1.f;
    const float markP = g.motion ? easeOut((t - 0.45f) / 0.35f) : 1.f;
    const uint32_t col = ok ? kGold : kDanger;
    // burst
    if (g.motion && t > 0.75f && t < 2.0f) {
        const float bt = (t - 0.75f) / 1.25f;
        for (int i = 0; i < 18; ++i) {
            const float ang = static_cast<float>(i) / 18.f * 2 * kPi + 0.3f;
            const float d = r + 8 + 70 * easeOut(bt);
            SolidBrush b(C(ok ? kGoldLight : kDanger, (1 - bt) * 0.8f * a));
            gr.FillEllipse(&b, cx + std::cos(ang) * d - 2, cy + std::sin(ang) * d - 2, 4.f, 4.f);
        }
    }
    SolidBrush disc(C(col, 0.08f * a * ringP));
    gr.FillEllipse(&disc, cx - r, cy - r, r * 2, r * 2);
    for (auto [w, al] : {std::pair{12.f, 0.06f}, std::pair{6.f, 0.14f}, std::pair{3.f, 1.f}}) {
        Pen p(C(col, al * a), w);
        p.SetLineCap(LineCapRound, LineCapRound, DashCapRound);
        if (ringP > 0.01f) gr.DrawArc(&p, cx - r, cy - r, r * 2, r * 2, -90.f, 360.f * ringP);
    }
    if (markP > 0.01f) {
        Pen m(C(ok ? kGoldLight : 0xF5B3A3, a), 5.f);
        m.SetLineCap(LineCapRound, LineCapRound, DashCapRound);
        m.SetLineJoin(LineJoinRound);
        if (ok) {
            const PointF p0(cx - 20, cy + 2), p1(cx - 6, cy + 16), p2(cx + 22, cy - 14);
            const float l1 = std::hypot(p1.X - p0.X, p1.Y - p0.Y), l2 = std::hypot(p2.X - p1.X, p2.Y - p1.Y);
            const float len = (l1 + l2) * markP;
            if (len <= l1) {
                const float k = len / l1;
                gr.DrawLine(&m, p0, PointF(p0.X + (p1.X - p0.X) * k, p0.Y + (p1.Y - p0.Y) * k));
            } else {
                const float k = (len - l1) / l2;
                PointF pts[3] = {p0, p1, PointF(p1.X + (p2.X - p1.X) * k, p1.Y + (p2.Y - p1.Y) * k)};
                gr.DrawLines(&m, pts, 3);
            }
        } else {
            const float k = markP * 16;
            gr.DrawLine(&m, cx - k, cy - k, cx + k, cy + k);
            gr.DrawLine(&m, cx + k, cy - k, cx - k, cy + k);
        }
    }
    const float ta = a * (g.motion ? easeOut((t - 0.35f) / 0.5f) : 1.f);
    std::wstring title = ok ? (g.doneIsUninstall ? S->removedTitle : S->doneTitle) : S->errorTitle;
    trackedCentered(gr, title, g.fonts->big.get(), C(kText, ta), 228, 300, 2.f);
    std::wstring body = ok ? (g.doneIsUninstall ? S->removedText : S->doneText) : g.workerError;
    text(gr, body, g.fonts->body15.get(), C(kDim, ta), {72, 352, 312, 80}, StringAlignmentCenter, StringAlignmentNear, true);
    if (ok) {
        if (!g.doneIsUninstall) {
            button(gr, kPrimary, {60, 452, 336, 52}, S->play, BtnStyle::Gold, ta);
            link(gr, kSecondary, 228 + 26, 518, S->close, ta, true, kDim);
        } else {
            button(gr, kPrimary, {60, 452, 336, 52}, S->close, BtnStyle::Ghost, ta);
        }
    } else {
        button(gr, kPrimary, {60, 452, 336, 52}, S->back, BtnStyle::Ghost, ta);
    }
}

void drawTitleBar(Graphics& gr) {
    ringIcon(gr, 20, 17, 7, 1.f);
    SolidBrush tb(C(kText, 0.92f));
    gr.DrawString(L"DSCoop", -1, g.fonts->titlebar.get(), PointF(34, 9), &tb);
    RectF w;
    gr.MeasureString(L"DSCoop", -1, g.fonts->titlebar.get(), PointF(0, 0), &w);
    SolidBrush vb(C(kFaint, 1));
    const std::wstring ver = L"v" + W(DSCOOP_VERSION_STRING);
    gr.DrawString(ver.c_str(), -1, g.fonts->small12.get(), PointF(34 + w.Width + 4, 10), &vb);
    const R minR{kW - 92, 0, 46, 34}, closeR{kW - 46, 0, 46, 34};
    addHit(kMinimize, minR);
    addHit(kClose, closeR);
    if (hov(kMinimize) > 0.01f) { SolidBrush b(C(0xFFFFFF, 0.08f * hov(kMinimize))); gr.FillRectangle(&b, minR.rf()); }
    if (hov(kClose) > 0.01f) { SolidBrush b(C(0xD9503A, 0.9f * hov(kClose))); gr.FillRectangle(&b, closeR.rf()); }
    Pen ic(C(kText, 0.85f), 1.2f);
    gr.DrawLine(&ic, minR.cx() - 5, 17.f, minR.cx() + 5, 17.f);
    gr.DrawLine(&ic, closeR.cx() - 5, 12.f, closeR.cx() + 5, 22.f);
    gr.DrawLine(&ic, closeR.cx() + 5, 12.f, closeR.cx() - 5, 22.f);
}

void drawToast(Graphics& gr) {
    const float a = g.toastA.v;
    if (a < 0.01f || g.toastText.empty()) return;
    const R r{44, 58 - 12 * (1 - a), 368, 64};
    SolidBrush bg(C(0x1A1612, 0.96f * a));
    fillRound(gr, r, 12, bg);
    strokeRound(gr, r, 12, C(g.toastColor, 0.7f * a), 1.f);
    SolidBrush dot(C(g.toastColor, a));
    gr.FillEllipse(&dot, r.x + 16, r.cy() - 11, 22.f, 22.f);
    SolidBrush ex(C(0x1A1612, a));
    gr.FillRectangle(&ex, r.x + 25.8f, r.cy() - 6.5f, 2.4f, 8.f);
    gr.FillEllipse(&ex, r.x + 25.6f, r.cy() + 3, 2.8f, 2.8f);
    text(gr, g.toastText, g.fonts->toast.get(), C(kText, a), {r.x + 50, r.y + 6, r.w - 62, r.h - 12}, StringAlignmentNear, StringAlignmentCenter, true);
}

// =============================================================================== frame
void ensureBuffer(HDC ref, int w, int h) {
    if (g.memDC && w == g.bufW && h == g.bufH) return;
    if (g.memDC) {
        SelectObject(g.memDC, g.memOld);
        DeleteObject(g.memBmp);
        DeleteDC(g.memDC);
    }
    g.memDC = CreateCompatibleDC(ref);
    g.memBmp = CreateCompatibleBitmap(ref, w, h);
    g.memOld = SelectObject(g.memDC, g.memBmp);
    g.bufW = w;
    g.bufH = h;
    g.bgCache.reset();
}

void render(HDC hdc) {
    RECT rc;
    GetClientRect(g.wnd, &rc);
    const int w = rc.right, h = rc.bottom;
    if (w <= 0 || h <= 0) return;
    ensureBuffer(hdc, w, h);
    Graphics gr(g.memDC);
    gr.SetSmoothingMode(SmoothingModeAntiAlias);
    gr.SetPixelOffsetMode(PixelOffsetModeHalf);
    gr.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
    gr.SetCompositingQuality(CompositingQualityHighSpeed);

    // --- background art (Ken Burns zoom on open, cached afterwards)
    const float zoom = g.motion ? 1.0f + 0.06f * (1 - easeOut(g.t / 1.8f)) : 1.0f;
    if (!g.hero) {
        SolidBrush bb(C(0x0B1316, 1));
        gr.FillRectangle(&bb, 0, 0, w, h);
    } else if (zoom > 1.0005f) {
        gr.SetInterpolationMode(InterpolationModeHighQualityBilinear);
        const float dw = static_cast<float>(w) * zoom, dh = static_cast<float>(h) * zoom;
        gr.DrawImage(g.hero.get(), RectF((static_cast<float>(w) - dw) / 2, (static_cast<float>(h) - dh) / 2, dw, dh), 0, 0, static_cast<REAL>(g.hero->GetWidth()),
                     static_cast<REAL>(g.hero->GetHeight()), UnitPixel);
    } else {
        if (!g.bgCache) {
            Bitmap bg(w, h, PixelFormat32bppPARGB);
            {
                Graphics gb(&bg);
                gb.SetInterpolationMode(InterpolationModeHighQualityBicubic);
                gb.SetPixelOffsetMode(PixelOffsetModeHalf);
                gb.DrawImage(g.hero.get(), RectF(0, 0, static_cast<REAL>(w), static_cast<REAL>(h)), 0, 0, static_cast<REAL>(g.hero->GetWidth()),
                             static_cast<REAL>(g.hero->GetHeight()), UnitPixel);
            }
            g.bgCache = std::make_unique<CachedBitmap>(&bg, &gr);
        }
        gr.DrawCachedBitmap(g.bgCache.get(), 0, 0);
    }

    gr.ScaleTransform(g.scale, g.scale);
    g.hits.clear();
    drawFog(gr);
    drawNetwork(gr);
    if (g.motion) drawMotes(gr);
    if (g.motion) drawRain(gr);

    // --- panel
    const float intro = g.motion ? easeOut((g.t - 0.15f) / 0.7f) : 1.f;
    const float slide = (1 - intro) * 22.f;
    GraphicsState st = gr.Save();
    gr.TranslateTransform(0, slide);
    const R panel{28, 50, 400, 502};
    if (g.motion) glowRound(gr, panel, 18, 0x000000, intro * 0.9f, 6, 3.f);
    SolidBrush pfill(C(kPanel, 0.84f * intro));
    fillRound(gr, panel, 18, pfill);
    LinearGradientBrush sheen(PointF(0, panel.y), PointF(0, panel.y + 140), C(0xFFFFFF, 0.05f * intro), C(0xFFFFFF, 0));
    fillRound(gr, {panel.x, panel.y, panel.w, 140}, 18, sheen);
    strokeRound(gr, panel, 18, C(kGold, 0.20f * intro), 1.f);

    const float a = g.pageAlpha * intro;
    gr.TranslateTransform(0, g.pageOffset);
    switch (g.page) {
        case Page::Welcome: drawWelcome(gr, a); break;
        case Page::Progress: drawProgress(gr, a); break;
        case Page::Done: drawResult(gr, a, true); break;
        case Page::Error: drawResult(gr, a, false); break;
    }
    gr.Restore(st);
    drawToast(gr);
    drawTitleBar(gr);

    BitBlt(hdc, 0, 0, w, h, g.memDC, 0, 0, SRCCOPY);
}

void tick(float dt) {
    g.t += dt;
    updateEffects(dt);

    // window fade-in
    if (g.motion && g.t < 0.6f) SetLayeredWindowAttributes(g.wnd, 0, static_cast<BYTE>(255 * easeOut(g.t / 0.45f)), LWA_ALPHA);
    else if (g.t - dt < 0.6f || !g.motion) SetLayeredWindowAttributes(g.wnd, 0, 255, LWA_ALPHA);

    // page transitions
    if (g.switching) {
        g.pageAlpha = g.motion ? std::max(0.f, g.pageAlpha - dt / 0.16f) : 0.f;
        if (g.pageAlpha <= 0.f) {
            g.page = g.nextPage;
            g.switching = false;
            g.pageOffset = 12.f;
            if (g.page == Page::Done || g.page == Page::Error) g.doneEnter = g.t;
        }
    } else {
        g.pageAlpha = g.motion ? std::min(1.f, g.pageAlpha + dt / 0.24f) : 1.f;
        g.pageOffset = g.motion ? g.pageOffset * std::exp(-dt * 12.f) : 0.f;
    }

    for (int i = 0; i < kIdCount; ++i) {
        g.hover[static_cast<size_t>(i)].target = g.hot == i ? 1.f : 0.f;
        g.hover[static_cast<size_t>(i)].step(dt, 14.f);
    }
    const HWND focus = GetFocus();
    g.focusNick.target = focus == g.editNick ? 1.f : 0.f;
    g.focusRelay.target = focus == g.editRelay ? 1.f : 0.f;
    g.focusNick.step(dt, 14.f);
    g.focusRelay.step(dt, 14.f);
    g.checkAnim.target = g.backup ? 1.f : 0.f;
    g.checkAnim.step(dt, 18.f);
    g.toastA.target = g.t < g.toastUntil ? 1.f : 0.f;
    g.toastA.step(dt, 10.f);

    // progress pacing: show each step for at least ~0.55 s so players can follow it
    if (g.busy || g.page == Page::Progress) {
        const int n = g.uninstalling ? kUninstallSteps : kInstallSteps;
        const int target = g.workerDone && g.workerOk ? n : g.workerStep;
        if (g.shownStep < target && g.t - g.lastStepTime >= (g.motion ? 0.55f : 0.f)) {
            g.shownStep++;
            g.lastStepTime = g.t;
            if (g.shownStep >= n) g.allDoneTime = g.t;
        }
        g.strandFill.target = static_cast<float>(std::min(g.shownStep, n - 1));
        g.strandFill.step(dt, 7.f);
        if (g.workerDone && !g.workerOk && g.t - g.lastStepTime > 0.4f) {
            g.busy = false;
            goPage(Page::Error);
        } else if (g.allDoneTime > 0 && g.t - g.allDoneTime > 0.55f) {
            g.busy = false;
            g.doneIsUninstall = g.uninstalling;
            g.allDoneTime = -1;
            refreshState();
            goPage(Page::Done);
        }
    }

    // network lighting follows the story: one link idle, lights up with progress, all lit when done
    for (int e = 0; e < 5; ++e) {
        float target = e == 0 ? 0.7f : 0.12f;
        if (g.page == Page::Progress || g.busy) {
            const int n = g.uninstalling ? kUninstallSteps : kInstallSteps;
            const float prog = clamp01((static_cast<float>(g.shownStep) + 0.5f) / static_cast<float>(n));
            const float lit = prog * 5.f;
            target = g.uninstalling ? (static_cast<float>(e) < 5.f - lit ? 0.6f : 0.05f) : (static_cast<float>(e) < lit ? 1.f : (e == 0 ? 0.7f : 0.12f));
        } else if (g.page == Page::Done) {
            target = g.doneIsUninstall ? (e == 0 ? 0.25f : 0.04f) : 1.f;
        } else if (g.st.installed) {
            target = 0.85f;
        }
        g.edgeLit[static_cast<size_t>(e)].target = target;
        g.edgeLit[static_cast<size_t>(e)].step(dt, 4.f);
    }

    if (g.pendingElevateAt > 0 && g.t >= g.pendingElevateAt) {
        g.pendingElevateAt = -1;
        if (win::relaunchElevated(g.pendingElevateParams)) DestroyWindow(g.wnd);
    }
    if (g.pendingCloseAt > 0 && g.t >= g.pendingCloseAt) {
        g.pendingCloseAt = -1;
        DestroyWindow(g.wnd);
        return;
    }
    if (g.args.autostart && g.t > 1.0f && g.t - dt <= 1.0f) {
        if (g.args.uninstall) doUninstall();
        else doInstall();
    }
    placeEdits();
}

// =============================================================================== window
int hitAt(float x, float y) {
    for (auto it = g.hits.rbegin(); it != g.hits.rend(); ++it)
        if (it->second.hit(x, y)) return it->first;
    return kNone;
}

LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_NCHITTEST: {
            POINT p{static_cast<short>(LOWORD(lp)), static_cast<short>(HIWORD(lp))};
            ScreenToClient(hwnd, &p);
            const float x = static_cast<float>(p.x) / g.scale, y = static_cast<float>(p.y) / g.scale;
            if (y < 34 && x >= kW - 92) return HTCLIENT;                 // title buttons
            if (y < 34) return HTCAPTION;                                // title bar drags
            if (R{28, 50, 400, 502}.hit(x, y)) return HTCLIENT;          // the panel
            if (g.toastA.v > 0.5f && R{44, 46, 368, 76}.hit(x, y)) return HTCLIENT;
            return HTCAPTION;                                            // the art drags the window too
        }
        case WM_MOUSEMOVE: {
            if (!g.tracking) {
                TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, hwnd, 0};
                TrackMouseEvent(&tme);
                g.tracking = true;
            }
            g.hot = hitAt(static_cast<float>(static_cast<short>(LOWORD(lp))) / g.scale, static_cast<float>(static_cast<short>(HIWORD(lp))) / g.scale);
            return 0;
        }
        case WM_MOUSELEAVE:
            g.tracking = false;
            g.hot = kNone;
            return 0;
        case WM_SETCURSOR:
            if (LOWORD(lp) == HTCLIENT && g.hot != kNone && g.hot != kField1 && g.hot != kField2) {
                SetCursor(LoadCursorW(nullptr, IDC_HAND));
                return TRUE;
            }
            break;
        case WM_LBUTTONDOWN:
            g.pressed = g.hot;
            SetCapture(hwnd);
            return 0;
        case WM_LBUTTONUP: {
            ReleaseCapture();
            const int id = g.pressed;
            g.pressed = kNone;
            if (id != kNone && id == g.hot) activate(id);
            return 0;
        }
        case WM_COMMAND:
            if (LOWORD(wp) == IDOK) { activate(kPrimary); return 0; }
            if (LOWORD(wp) == IDCANCEL) {
                if (g.confirmUninstall) g.confirmUninstall = false;
                else requestClose();
                return 0;
            }
            return 0;
        case WM_CTLCOLOREDIT: {
            HDC dc = reinterpret_cast<HDC>(wp);
            SetTextColor(dc, RGB(0xE9, 0xEF, 0xEA));
            SetBkColor(dc, RGB(0x0F, 0x1B, 0x1F));
            return reinterpret_cast<LRESULT>(g.fieldBrush);
        }
        case WM_TIMER: {
            const auto now = std::chrono::steady_clock::now();
            float dt = std::chrono::duration<float>(now - g.last).count();
            g.last = now;
            if (dt > 0.1f) dt = 0.1f;
            tick(dt);
            if (IsWindow(hwnd)) InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            render(hdc);
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_APP_STEP:
            g.workerStep = std::max(g.workerStep, static_cast<int>(wp));
            return 0;
        case WM_APP_LOG: {
            std::unique_ptr<std::wstring> s(reinterpret_cast<std::wstring*>(lp));
            g.lastLog = *s;
            return 0;
        }
        case WM_APP_DONE: {
            std::unique_ptr<std::wstring> err(reinterpret_cast<std::wstring*>(lp));
            g.workerDone = true;
            g.workerOk = wp != 0;
            g.workerError = *err;
            if (g.workerOk && g.uninstalling && win::gameDirFromSelf()) win::scheduleSelfDelete();
            return 0;
        }
        case WM_CLOSE:
            requestClose();
            return 0;
        case WM_DESTROY:
            KillTimer(hwnd, 1);
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

void detectInitial() {
    win::Detected d;
    if (!g.args.dir.empty()) {
        g.gameDir = g.args.dir;
        g.manualDir = true;
        d = win::detectSteam();
        std::error_code ec;
        if (d.game && fs::equivalent(d.game->gameDir, g.gameDir, ec)) { g.game = d.game; g.manualDir = false; }
    } else if (auto self = win::gameDirFromSelf()) {
        g.gameDir = *self;
        d = win::detectSteam();
        g.game = d.game;
        std::error_code ec;
        g.manualDir = !(d.game && fs::equivalent(d.game->gameDir, g.gameDir, ec));
    } else {
        d = win::detectSteam();
        if (d.game) { g.game = d.game; g.gameDir = d.game->gameDir; }
    }
    std::string nick = U(g.args.nick), relay = U(g.args.relay);
    if (!g.gameDir.empty()) win::readSettings(g.gameDir, nick, relay);
    if (nick.empty() || nick == "Porter") nick = d.persona.value_or("Porter");
    SetWindowTextW(g.editNick, W(nick).c_str());
    SetWindowTextW(g.editRelay, W(relay).c_str());
    if (g.args.noBackup) g.backup = false;
    refreshState();
    if (g.args.uninstall && !g.args.autostart) g.confirmUninstall = true;
}

int run(HINSTANCE inst, int show, const win::Args& args) {
    g.inst = inst;
    g.args = args;
    switch (PRIMARYLANGID(GetUserDefaultUILanguage())) {
        case LANG_RUSSIAN: case LANG_UKRAINIAN: case LANG_BELARUSIAN: case LANG_KAZAK: S = &kRu; break;
        default: S = &kEn; break;
    }
    BOOL anim = TRUE;
    SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &anim, 0);
    g.motion = anim != FALSE;

    GdiplusStartupInput gin;
    if (GdiplusStartup(&g.gdipToken, &gin, nullptr) != Ok) {
        MessageBoxW(nullptr, L"GDI+ is not available.", L"DSCoop", MB_ICONERROR);
        return 1;
    }
    INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&icc);
    HDC screen = GetDC(nullptr);
    g.scale = static_cast<float>(GetDeviceCaps(screen, LOGPIXELSY)) / 96.f;
    ReleaseDC(nullptr, screen);
    loadFonts();
    loadArt();
    initEffects();
    g.fieldBrush = CreateSolidBrush(RGB(0x0F, 0x1B, 0x1F));

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_DROPSHADOW;
    wc.lpfnWndProc = wndProc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(IDI_APP));
    wc.hIconSm = static_cast<HICON>(LoadImageW(inst, MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON, 16, 16, 0));
    wc.lpszClassName = L"DSCoopSetupFancy";
    RegisterClassExW(&wc);

    const int w = static_cast<int>(kW * g.scale), h = static_cast<int>(kH * g.scale);
    const std::wstring caption = L"DSCoop " + W(DSCOOP_VERSION_STRING);
    g.wnd = CreateWindowExW(WS_EX_APPWINDOW | WS_EX_LAYERED, wc.lpszClassName, caption.c_str(),
                            WS_POPUP | WS_CLIPCHILDREN | WS_SYSMENU | WS_MINIMIZEBOX, (GetSystemMetrics(SM_CXSCREEN) - w) / 2,
                            (GetSystemMetrics(SM_CYSCREEN) - h) / 2, w, h, nullptr, nullptr, inst, nullptr);
    if (!g.wnd) return 1;
    SetLayeredWindowAttributes(g.wnd, 0, g.motion ? 0 : 255, LWA_ALPHA);
    const int roundPref = 2;  // DWMWCP_ROUND (Windows 11; ignored elsewhere)
    DwmSetWindowAttribute(g.wnd, 33 /* DWMWA_WINDOW_CORNER_PREFERENCE */, &roundPref, sizeof(roundPref));

    g.editFont = CreateFontW(-static_cast<int>(15 * g.scale), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                             CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"PT Sans");
    auto mkEdit = [&](int id) {
        HWND e = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_TABSTOP | ES_AUTOHSCROLL, 0, 0, 10, 10, g.wnd,
                                 reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), inst, nullptr);
        SendMessageW(e, WM_SETFONT, reinterpret_cast<WPARAM>(g.editFont), TRUE);
        return e;
    };
    g.editNick = mkEdit(kEditNick);
    g.editRelay = mkEdit(kEditRelay);
    SendMessageW(g.editNick, EM_SETLIMITTEXT, 24, 0);
    SendMessageW(g.editRelay, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(L"relay.example.com:27000"));

    detectInitial();
    g.last = std::chrono::steady_clock::now();
    SetTimer(g.wnd, 1, 15, nullptr);
    ShowWindow(g.wnd, show);
    UpdateWindow(g.wnd);

    MSG m;
    while (GetMessageW(&m, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(g.wnd, &m)) {
            TranslateMessage(&m);
            DispatchMessageW(&m);
        }
    }
    // GDI+ objects must die before GdiplusShutdown.
    g.bgCache.reset();
    g.hero.reset();
    g.logoPath.reset();
    g.fonts.reset();
    if (g.memDC) { SelectObject(g.memDC, g.memOld); DeleteObject(g.memBmp); DeleteDC(g.memDC); }
    DeleteObject(g.fieldBrush);
    DeleteObject(g.editFont);
    GdiplusShutdown(g.gdipToken);
    return 0;
}

}  // namespace dsinst::ui
