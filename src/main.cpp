#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>
#include <commctrl.h>
#include <d2d1.h>
#include <dwmapi.h>
#include <dwrite_3.h>
#include <shellapi.h>
#include <shlobj.h>
#include <windowsx.h>
#include <wrl/client.h>

#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

#include "http.hpp"
#include "smtc.hpp"
#include "tracker.hpp"

#pragma comment(linker, \
  "\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

namespace {

using namespace euilscrr;
using Microsoft::WRL::ComPtr;

constexpr wchar_t kWindowClass[] = L"EuilscrrWatcherWindow";
constexpr wchar_t kMutexName[] = L"Local\\euilscrr-watcher-singleton";
constexpr wchar_t kRunRegKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr wchar_t kRunValueName[] = L"euilscrrWatcher";

constexpr UINT WM_APP_TRAY = WM_APP + 1;
constexpr UINT WM_APP_STATE = WM_APP + 2;
constexpr UINT WM_APP_SHOW = WM_APP + 3;

constexpr UINT_PTR kTimerUiTick = 1;
constexpr int kCtrlTokenEdit = 101;
constexpr int kResAppIcon = 1;
constexpr int kResFontInter = 101;
constexpr UINT kTrayIconId = 1;
constexpr UINT kMenuOpen = 2001;
constexpr UINT kMenuStartup = 2002;
constexpr UINT kMenuQuit = 2003;

constexpr int kClientW = 360;
constexpr int kClientH = 274;

enum class SyncState {
  TokenNeeded,
  Idle,
  Listening,
  Paused,
  InvalidToken,
  Offline,
};

struct UiSnapshot {
  SyncState state = SyncState::TokenNeeded;
  std::wstring title;
  std::wstring artist;
  std::optional<int64_t> position_ms;
  std::optional<int64_t> duration_ms;
  int64_t position_at_ms = 0;
  bool is_playing = false;
  bool scrobbled = false;
};

struct Theme {
  bool dark = true;
  COLORREF bg;
  COLORREF card;
  COLORREF input_bg;
  COLORREF border;
  COLORREF btn_border;
  COLORREF text;
  COLORREF muted;
  COLORREF dim;
  COLORREF btn_bg;
  COLORREF btn_bg_hover;
  COLORREF btn_text;
  COLORREF badge_ok_bg;
  COLORREF badge_ok_fg;
  COLORREF badge_idle_bg;
  COLORREF badge_idle_fg;
  COLORREF badge_err_bg;
  COLORREF badge_err_fg;
  COLORREF dot_ok;
  COLORREF dot_idle;
  COLORREF dot_err;
  COLORREF track_bg;
  COLORREF green;
  COLORREF check_bg;
  COLORREF check_fg;
};

Theme make_theme(bool dark) {
  if (dark) {
    return Theme{
        true,
        RGB(9, 9, 11),       // #09090B bg
        RGB(17, 17, 19),     // #111113 card
        RGB(17, 17, 19),     // #111113 input_bg
        RGB(39, 39, 42),     // #27272A border
        RGB(63, 63, 70),     // #3F3F46 btn_border
        RGB(244, 244, 245),  // #F4F4F5 text
        RGB(161, 161, 170),  // #A1A1AA muted
        RGB(113, 113, 122),  // #71717A dim
        RGB(244, 244, 245),  // #F4F4F5 btn_bg
        RGB(228, 228, 231),  // #E4E4E7 btn_bg_hover
        RGB(9, 9, 11),       // #09090B btn_text
        RGB(13, 38, 29),     // badge_ok_bg
        RGB(52, 211, 153),   // #34D399 badge_ok_fg
        RGB(39, 39, 42),     // #27272A badge_idle_bg
        RGB(161, 161, 170),  // #A1A1AA badge_idle_fg
        RGB(45, 18, 21),     // badge_err_bg
        RGB(248, 113, 113),  // #F87171 badge_err_fg
        RGB(16, 185, 129),   // #10B981 dot_ok
        RGB(113, 113, 122),  // #71717A dot_idle
        RGB(239, 68, 68),    // #EF4444 dot_err
        RGB(39, 39, 42),     // #27272A track_bg
        RGB(30, 215, 96),    // #1ED760 green
        RGB(244, 244, 245),  // #F4F4F5 check_bg
        RGB(9, 9, 11),       // #09090B check_fg
    };
  }
  return Theme{
      false,
      RGB(255, 255, 255),  // #FFFFFF bg
      RGB(250, 250, 250),  // #FAFAFA card
      RGB(255, 255, 255),  // #FFFFFF input_bg
      RGB(228, 228, 231),  // #E4E4E7 border
      RGB(212, 212, 216),  // #D4D4D8 btn_border
      RGB(9, 9, 11),       // #09090B text
      RGB(82, 82, 91),     // #52525B muted
      RGB(113, 113, 122),  // #71717A dim
      RGB(24, 24, 27),     // #18181B btn_bg
      RGB(39, 39, 42),     // #27272A btn_bg_hover
      RGB(255, 255, 255),  // #FFFFFF btn_text
      RGB(231, 247, 240),  // badge_ok_bg
      RGB(4, 120, 87),     // #047857 badge_ok_fg
      RGB(244, 244, 245),  // #F4F4F5 badge_idle_bg
      RGB(82, 82, 91),     // #52525B badge_idle_fg
      RGB(254, 242, 242),  // badge_err_bg
      RGB(220, 38, 38),    // #DC2626 badge_err_fg
      RGB(16, 185, 129),   // #10B981 dot_ok
      RGB(161, 161, 170),  // #A1A1AA dot_idle
      RGB(220, 38, 38),    // #DC2626 dot_err
      RGB(228, 228, 231),  // #E4E4E7 track_bg
      RGB(30, 215, 96),    // #1ED760 green
      RGB(24, 24, 27),     // #18181B check_bg
      RGB(255, 255, 255),  // #FFFFFF check_fg
  };
}

D2D1_COLOR_F to_d2d(COLORREF c, float a = 1.0f) {
  return D2D1::ColorF(GetRValue(c) / 255.0f, GetGValue(c) / 255.0f, GetBValue(c) / 255.0f, a);
}

int64_t epoch_now_ms() {
  using namespace std::chrono;
  return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

std::wstring utf8_to_wide(const std::string& utf8) {
  if (utf8.empty()) return {};
  int needed = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
  if (needed <= 0) return {};
  std::wstring out(static_cast<size_t>(needed), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), out.data(), needed);
  return out;
}

std::string wide_to_utf8(const std::wstring& wide) {
  if (wide.empty()) return {};
  int needed = WideCharToMultiByte(
      CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr);
  if (needed <= 0) return {};
  std::string out(static_cast<size_t>(needed), '\0');
  WideCharToMultiByte(
      CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), out.data(), needed, nullptr, nullptr);
  return out;
}

std::wstring format_clock_w(int64_t ms) {
  if (ms < 0) ms = 0;
  int64_t total_sec = ms / 1000;
  int64_t mins = total_sec / 60;
  int64_t secs = total_sec % 60;
  wchar_t buf[32];
  std::swprintf(buf, std::size(buf), L"%lld:%02lld", mins, secs);
  return buf;
}

bool is_windows_dark_mode() {
  DWORD value = 1;
  DWORD size = sizeof(value);
  if (RegGetValueW(
          HKEY_CURRENT_USER,
          L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
          L"AppsUseLightTheme",
          RRF_RT_REG_DWORD,
          nullptr,
          &value,
          &size) == ERROR_SUCCESS) {
    return value == 0;
  }
  return true;
}

std::filesystem::path token_file_path() {
  PWSTR appdata = nullptr;
  std::filesystem::path dir;
  if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &appdata)) && appdata) {
    dir = std::filesystem::path(appdata) / L"euilscrr";
    CoTaskMemFree(appdata);
  } else {
    dir = std::filesystem::current_path();
  }
  return dir / L"token.txt";
}

std::string load_saved_token() {
  std::error_code ec;
  auto path = token_file_path();
  if (!std::filesystem::exists(path, ec)) return {};
  std::ifstream in(path, std::ios::binary);
  std::string raw((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  return trim_copy(raw);
}

void save_token_file(const std::string& token) {
  std::error_code ec;
  auto path = token_file_path();
  if (token.empty()) {
    std::filesystem::remove(path, ec);
    return;
  }
  std::filesystem::create_directories(path.parent_path(), ec);
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  out << token;
}

std::wstring exe_path_string() {
  wchar_t buf[MAX_PATH]{};
  DWORD len = GetModuleFileNameW(nullptr, buf, MAX_PATH);
  return std::wstring(buf, len);
}

bool is_startup_enabled() {
  DWORD type = 0;
  DWORD size = 0;
  return RegGetValueW(HKEY_CURRENT_USER, kRunRegKey, kRunValueName, RRF_RT_REG_SZ, &type, nullptr, &size) ==
         ERROR_SUCCESS;
}

void set_startup_enabled(bool enable) {
  HKEY key = nullptr;
  if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunRegKey, 0, KEY_SET_VALUE, &key) != ERROR_SUCCESS) {
    return;
  }
  if (enable) {
    std::wstring cmd = L"\"" + exe_path_string() + L"\" --minimized";
    RegSetValueExW(
        key,
        kRunValueName,
        0,
        REG_SZ,
        reinterpret_cast<const BYTE*>(cmd.c_str()),
        static_cast<DWORD>((cmd.size() + 1) * sizeof(wchar_t)));
  } else {
    RegDeleteValueW(key, kRunValueName);
  }
  RegCloseKey(key);
}

std::wstring pick_available_font(IDWriteFactory* dwrite, std::initializer_list<const wchar_t*> candidates) {
  ComPtr<IDWriteFontCollection> collection;
  if (dwrite && SUCCEEDED(dwrite->GetSystemFontCollection(&collection))) {
    for (const wchar_t* name : candidates) {
      UINT32 idx = 0;
      BOOL exists = FALSE;
      if (SUCCEEDED(collection->FindFamilyName(name, &idx, &exists)) && exists) {
        return name;
      }
    }
  }
  return L"Segoe UI";
}

// Generate monochrome vinyl-dot icon matching euilscrr brand mark without external .ico resource.
HICON load_app_icon(HINSTANCE instance, int size) {
  auto icon = static_cast<HICON>(
      LoadImageW(instance, MAKEINTRESOURCEW(kResAppIcon), IMAGE_ICON, size, size, LR_DEFAULTCOLOR));
  return icon ? icon : LoadIconW(nullptr, IDI_APPLICATION);
}

class WatcherApp {
 public:
  WatcherApp() : api_url_(resolve_api_url()), theme_(make_theme(is_windows_dark_mode())) {
    token_ = load_saved_token();
    startup_enabled_ = is_startup_enabled();
    if (!token_.empty()) {
      ui_.state = SyncState::Idle;
    }
  }

  ~WatcherApp() {
    stop_worker();
    destroy_resources();
  }

  int run(HINSTANCE instance, bool start_minimized) {
    instance_ = instance;
    init_direct2d();
    icon_small_ = load_app_icon(instance_, GetSystemMetrics(SM_CXSMICON));
    icon_large_ = load_app_icon(instance_, GetSystemMetrics(SM_CXICON));

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = &WatcherApp::wnd_proc;
    wc.hInstance = instance_;
    wc.lpszClassName = kWindowClass;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = icon_large_;
    wc.hIconSm = icon_small_;
    wc.style = CS_HREDRAW | CS_VREDRAW;
    if (!RegisterClassExW(&wc)) return 1;

    dpi_ = GetDpiForSystem();
    RECT rc{0, 0, s(kClientW), s(kClientH)};
    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_CLIPCHILDREN;
    AdjustWindowRectExForDpi(&rc, style, FALSE, 0, dpi_);

    int win_w = rc.right - rc.left;
    int win_h = rc.bottom - rc.top;
    int screen_w = GetSystemMetrics(SM_CXSCREEN);
    int screen_h = GetSystemMetrics(SM_CYSCREEN);

    hwnd_ = CreateWindowExW(
        0,
        kWindowClass,
        L"euilscrr Watcher",
        style,
        (screen_w - win_w) / 2,
        (screen_h - win_h) / 2,
        win_w,
        win_h,
        nullptr,
        nullptr,
        instance_,
        this);
    if (!hwnd_) return 1;

    apply_window_theme();
    add_tray_icon();
    start_worker();
    SetTimer(hwnd_, kTimerUiTick, 1000, nullptr);

    if (!start_minimized) {
      ShowWindow(hwnd_, SW_SHOW);
      UpdateWindow(hwnd_);
    }

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
      if (msg.message == WM_KEYDOWN && msg.wParam == VK_RETURN && GetFocus() == edit_token_) {
        on_save_or_update();
        continue;
      }
      TranslateMessage(&msg);
      DispatchMessageW(&msg);
    }

    remove_tray_icon();
    stop_worker();
    return 0;
  }

 private:
  int s(int px) const { return MulDiv(px, static_cast<int>(dpi_), 96); }

  void load_embedded_inter() {
    HRSRC res = FindResourceW(instance_, MAKEINTRESOURCEW(kResFontInter), RT_RCDATA);
    if (!res) return;
    DWORD size = SizeofResource(instance_, res);
    HGLOBAL loaded = LoadResource(instance_, res);
    if (!loaded || size == 0) return;
    const void* data = LockResource(loaded);
    if (!data) return;

    DWORD gdi_count = 0;
    gdi_font_handle_ = AddFontMemResourceEx(const_cast<void*>(data), size, nullptr, &gdi_count);

    ComPtr<IDWriteFactory5> dw5;
    if (!dwrite_ || FAILED(dwrite_.As(&dw5))) return;
    if (FAILED(dw5->CreateInMemoryFontFileLoader(&mem_font_loader_))) return;
    if (FAILED(dw5->RegisterFontFileLoader(mem_font_loader_.Get()))) {
      mem_font_loader_.Reset();
      return;
    }

    ComPtr<IDWriteFontFile> font_file;
    ComPtr<IDWriteFontSetBuilder1> builder;
    ComPtr<IDWriteFontSet> font_set;
    if (FAILED(mem_font_loader_->CreateInMemoryFontFileReference(dw5.Get(), data, size, nullptr, &font_file)) ||
        FAILED(dw5->CreateFontSetBuilder(&builder)) ||
        FAILED(builder->AddFontFile(font_file.Get())) ||
        FAILED(builder->CreateFontSet(&font_set)) ||
        FAILED(dw5->CreateFontCollectionFromFontSet(font_set.Get(), &embedded_fonts_))) {
      return;
    }

    if (embedded_fonts_ && embedded_fonts_->GetFontFamilyCount() > 0) {
      ComPtr<IDWriteFontFamily> family;
      ComPtr<IDWriteLocalizedStrings> names;
      if (SUCCEEDED(embedded_fonts_->GetFontFamily(0, &family)) &&
          SUCCEEDED(family->GetFamilyNames(&names))) {
        UINT32 len = 0;
        if (SUCCEEDED(names->GetStringLength(0, &len)) && len > 0) {
          std::wstring name(len + 1, L'\0');
          if (SUCCEEDED(names->GetString(0, name.data(), len + 1))) {
            name.resize(len);
            ui_font_family_ = std::move(name);
          }
        }
      }
    }
  }

  void init_direct2d() {
    D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, d2d_factory_.GetAddressOf());
    DWriteCreateFactory(
        DWRITE_FACTORY_TYPE_SHARED,
        __uuidof(IDWriteFactory),
        reinterpret_cast<IUnknown**>(dwrite_.GetAddressOf()));

    ui_font_family_ = pick_available_font(dwrite_.Get(), {L"Inter", L"Segoe UI Variable Text", L"Segoe UI"});
    load_embedded_inter();

    if (d2d_factory_) {
      D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
          D2D1_RENDER_TARGET_TYPE_DEFAULT,
          D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE),
          96.0f,
          96.0f);
      d2d_factory_->CreateDCRenderTarget(&props, &dc_rt_);
    }

    auto make_fmt = [&](float size, DWRITE_FONT_WEIGHT weight, DWRITE_TEXT_ALIGNMENT align, bool ellipsis) {
      ComPtr<IDWriteTextFormat> fmt;
      if (!dwrite_) return fmt;
      dwrite_->CreateTextFormat(
          ui_font_family_.c_str(), embedded_fonts_.Get(), weight, DWRITE_FONT_STYLE_NORMAL,
          DWRITE_FONT_STRETCH_NORMAL, size, L"", &fmt);
      if (fmt) {
        fmt->SetTextAlignment(align);
        fmt->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        fmt->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        if (ellipsis) {
          DWRITE_TRIMMING trim{DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0};
          ComPtr<IDWriteInlineObject> sign;
          dwrite_->CreateEllipsisTrimmingSign(fmt.Get(), &sign);
          fmt->SetTrimming(&trim, sign.Get());
        }
      }
      return fmt;
    };

    fmt_brand_ = make_fmt(15.0f, DWRITE_FONT_WEIGHT_BOLD, DWRITE_TEXT_ALIGNMENT_LEADING, false);
    fmt_eyebrow_ = make_fmt(10.0f, DWRITE_FONT_WEIGHT_BOLD, DWRITE_TEXT_ALIGNMENT_LEADING, false);
    fmt_title_ = make_fmt(14.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_TEXT_ALIGNMENT_LEADING, true);
    fmt_body_ = make_fmt(12.0f, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_LEADING, true);
    fmt_body_right_ = make_fmt(12.0f, DWRITE_FONT_WEIGHT_MEDIUM, DWRITE_TEXT_ALIGNMENT_TRAILING, false);
    fmt_label_ = make_fmt(12.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_TEXT_ALIGNMENT_LEADING, false);
    fmt_btn_ = make_fmt(12.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_TEXT_ALIGNMENT_CENTER, false);
    fmt_badge_ = make_fmt(11.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_TEXT_ALIGNMENT_LEADING, false);
    fmt_meta_right_ = make_fmt(11.0f, DWRITE_FONT_WEIGHT_MEDIUM, DWRITE_TEXT_ALIGNMENT_TRAILING, false);
  }

  void create_gdi_edit_resources() {
    destroy_gdi_edit_resources();
    DWORD quality = theme_.dark ? ANTIALIASED_QUALITY : CLEARTYPE_QUALITY;
    font_edit_ = CreateFontW(
        -s(12), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS, quality, DEFAULT_PITCH | FF_SWISS, ui_font_family_.c_str());
    brush_input_ = CreateSolidBrush(theme_.input_bg);
  }

  void destroy_gdi_edit_resources() {
    if (font_edit_) {
      DeleteObject(font_edit_);
      font_edit_ = nullptr;
    }
    if (brush_input_) {
      DeleteObject(brush_input_);
      brush_input_ = nullptr;
    }
  }

  void destroy_resources() {
    destroy_gdi_edit_resources();
    if (icon_small_) {
      DestroyIcon(icon_small_);
      icon_small_ = nullptr;
    }
    if (icon_large_) {
      DestroyIcon(icon_large_);
      icon_large_ = nullptr;
    }
    fmt_brand_.Reset();
    fmt_eyebrow_.Reset();
    fmt_title_.Reset();
    fmt_body_.Reset();
    fmt_body_right_.Reset();
    fmt_label_.Reset();
    fmt_btn_.Reset();
    fmt_badge_.Reset();
    fmt_meta_right_.Reset();
    embedded_fonts_.Reset();
    if (dwrite_ && mem_font_loader_) {
      dwrite_->UnregisterFontFileLoader(mem_font_loader_.Get());
    }
    mem_font_loader_.Reset();
    if (gdi_font_handle_) {
      RemoveFontMemResourceEx(gdi_font_handle_);
      gdi_font_handle_ = nullptr;
    }
    dc_rt_.Reset();
    dwrite_.Reset();
    d2d_factory_.Reset();
  }

  void apply_window_theme() {
    BOOL dark = theme_.dark ? TRUE : FALSE;
    DwmSetWindowAttribute(hwnd_, 20, &dark, sizeof(dark));
    create_gdi_edit_resources();
    if (edit_token_) {
      SendMessageW(edit_token_, WM_SETFONT, reinterpret_cast<WPARAM>(font_edit_), TRUE);
      InvalidateRect(edit_token_, nullptr, TRUE);
    }
    InvalidateRect(hwnd_, nullptr, TRUE);
  }

  void layout_controls() {
    if (edit_token_) {
      MoveWindow(edit_token_, s(28), s(188), s(222), s(18), TRUE);
    }
    btn_rect_ = {s(268), s(178), s(342), s(212)};
    checkbox_rect_ = {s(18), s(238), s(178), s(262)};
    hide_rect_ = {s(258), s(238), s(342), s(262)};
  }

  void add_tray_icon() {
    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof(nid);
    nid.hWnd = hwnd_;
    nid.uID = kTrayIconId;
    nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    nid.uCallbackMessage = WM_APP_TRAY;
    nid.hIcon = icon_small_;
    wcscpy_s(nid.szTip, L"euilscrr Watcher");
    Shell_NotifyIconW(NIM_ADD, &nid);
  }

  void update_tray_tooltip(const std::wstring& tip) {
    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof(nid);
    nid.hWnd = hwnd_;
    nid.uID = kTrayIconId;
    nid.uFlags = NIF_TIP;
    wcsncpy_s(nid.szTip, tip.c_str(), _TRUNCATE);
    Shell_NotifyIconW(NIM_MODIFY, &nid);
  }

  void remove_tray_icon() {
    if (!hwnd_) return;
    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof(nid);
    nid.hWnd = hwnd_;
    nid.uID = kTrayIconId;
    Shell_NotifyIconW(NIM_DELETE, &nid);
  }

  void show_window() {
    ShowWindow(hwnd_, SW_RESTORE);
    ShowWindow(hwnd_, SW_SHOW);
    SetForegroundWindow(hwnd_);
  }

  void hide_to_tray() { ShowWindow(hwnd_, SW_HIDE); }

  void show_tray_menu() {
    HMENU menu = CreatePopupMenu();
    if (!menu) return;
    AppendMenuW(menu, MF_STRING, kMenuOpen, L"Open euilscrr Watcher");
    AppendMenuW(menu, MF_STRING | (startup_enabled_ ? MF_CHECKED : MF_UNCHECKED), kMenuStartup, L"Start with Windows");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kMenuQuit, L"Quit");

    POINT pt{};
    GetCursorPos(&pt);
    SetForegroundWindow(hwnd_);
    UINT cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd_, nullptr);
    DestroyMenu(menu);

    if (cmd == kMenuOpen) {
      show_window();
    } else if (cmd == kMenuStartup) {
      toggle_startup();
    } else if (cmd == kMenuQuit) {
      quitting_ = true;
      DestroyWindow(hwnd_);
    }
  }

  void toggle_startup() {
    startup_enabled_ = !startup_enabled_;
    set_startup_enabled(startup_enabled_);
    InvalidateRect(hwnd_, &checkbox_rect_, FALSE);
  }

  bool is_button_primary() const { return token_dirty_ || saved_token_empty_; }

  std::string read_edit_token() const {
    if (!edit_token_) return {};
    int len = GetWindowTextLengthW(edit_token_);
    if (len <= 0) return {};
    std::wstring buf(static_cast<size_t>(len) + 1, L'\0');
    int copied = GetWindowTextW(edit_token_, buf.data(), len + 1);
    buf.resize(static_cast<size_t>(copied));
    return trim_copy(wide_to_utf8(buf));
  }

  void on_save_or_update() {
    if (!is_button_primary()) {
      SetFocus(edit_token_);
      SendMessageW(edit_token_, EM_SETSEL, 0, -1);
      token_dirty_ = true;
      InvalidateRect(hwnd_, &btn_rect_, FALSE);
      return;
    }

    std::string next = read_edit_token();
    save_token_file(next);
    {
      std::lock_guard<std::mutex> lock(mu_);
      token_ = next;
      token_changed_ = true;
      if (next.empty()) {
        ui_ = UiSnapshot{};
        ui_.state = SyncState::TokenNeeded;
      } else if (ui_.state == SyncState::TokenNeeded || ui_.state == SyncState::InvalidToken) {
        ui_.state = SyncState::Idle;
      }
    }
    saved_token_empty_ = next.empty();
    token_dirty_ = false;
    cv_.notify_all();
    InvalidateRect(hwnd_, nullptr, FALSE);
  }

  void start_worker() {
    stop_flag_ = false;
    worker_ = std::thread([this]() { worker_loop(); });
  }

  void stop_worker() {
    {
      std::lock_guard<std::mutex> lock(mu_);
      stop_flag_ = true;
    }
    cv_.notify_all();
    if (worker_.joinable()) worker_.join();
  }

  void worker_loop() {
    winrt::init_apartment(winrt::apartment_type::multi_threaded);
    SmtcReader smtc;
    PlaybackTracker tracker;
    bool had_published_np = false;

    while (true) {
      std::string token;
      bool verify_now = false;
      {
        std::lock_guard<std::mutex> lock(mu_);
        if (stop_flag_) break;
        token = token_;
        if (token_changed_) {
          token_changed_ = false;
          verify_now = !token.empty();
          tracker.force_resync();
        }
      }

      if (token.empty()) {
        had_published_np = false;
        tracker.force_resync();
        std::unique_lock<std::mutex> lock(mu_);
        cv_.wait_for(lock, std::chrono::milliseconds(1000), [this]() { return stop_flag_ || token_changed_; });
        continue;
      }

      int64_t now_ms = epoch_now_ms();
      SmtcSample sample = smtc.poll();
      TrackerAction action = tracker.update(sample, now_ms);

      std::optional<SyncState> override_error;
      auto handle_result = [&](const HttpResult& res) {
        if (!res.transport_ok) {
          override_error = SyncState::Offline;
        } else if (res.status == 401) {
          override_error = SyncState::InvalidToken;
        } else if (res.status >= 400) {
          override_error = SyncState::Offline;
        }
      };

      if (action.has_now_playing) {
        if (action.now_playing.has_value()) {
          const auto& np = *action.now_playing;
          std::optional<int64_t> pos = np.position_ms;
          if (pos.has_value() && np.is_playing) {
            int64_t elapsed = std::max<int64_t>(0, epoch_now_ms() - now_ms);
            int64_t adjusted = *pos + elapsed;
            if (np.duration_ms.has_value()) adjusted = std::min(*np.duration_ms, adjusted);
            pos = adjusted;
          }
          HttpResult res = post_json(api_url_, build_now_playing_json(token, np, pos));
          handle_result(res);
          if (res.transport_ok && res.status >= 200 && res.status < 300) {
            had_published_np = true;
          }
        } else {
          HttpResult res = post_json(api_url_, build_now_playing_json(token, std::nullopt, std::nullopt));
          handle_result(res);
          had_published_np = false;
        }
      } else if (verify_now) {
        HttpResult res = post_json(api_url_, build_now_playing_json(token, std::nullopt, std::nullopt));
        handle_result(res);
      }

      if (action.scrobble.has_value()) {
        HttpResult res = post_json(api_url_, build_scrobble_json(token, *action.scrobble));
        handle_result(res);
      }

      UiSnapshot next_ui;
      if (override_error.has_value()) {
        next_ui.state = *override_error;
      } else if (tracker.synced().has_value()) {
        const auto& np = *tracker.synced();
        next_ui.state = np.is_playing ? SyncState::Listening : SyncState::Paused;
        next_ui.title = utf8_to_wide(np.title);
        next_ui.artist = utf8_to_wide(np.artist);
        next_ui.duration_ms = np.duration_ms;
        next_ui.position_ms = sample.position_ms.has_value() ? sample.position_ms : np.position_ms;
        next_ui.position_at_ms = now_ms;
        next_ui.is_playing = np.is_playing;
        next_ui.scrobbled = tracker.is_scrobbled();
      } else {
        next_ui.state = SyncState::Idle;
      }

      {
        std::lock_guard<std::mutex> lock(mu_);
        if (ui_.state == SyncState::InvalidToken && !action.has_now_playing && !verify_now &&
            !action.scrobble.has_value()) {
          next_ui.state = SyncState::InvalidToken;
        }
        ui_ = std::move(next_ui);
      }

      if (hwnd_) PostMessageW(hwnd_, WM_APP_STATE, 0, 0);

      std::unique_lock<std::mutex> lock(mu_);
      cv_.wait_for(lock, std::chrono::milliseconds(1500), [this]() { return stop_flag_ || token_changed_; });
    }

    if (had_published_np) {
      std::string token;
      {
        std::lock_guard<std::mutex> lock(mu_);
        token = token_;
      }
      if (!token.empty()) {
        post_json(api_url_, build_now_playing_json(token, std::nullopt, std::nullopt), 3000);
      }
    }
    winrt::uninit_apartment();
  }

  float measure_text_width(const std::wstring& text, IDWriteTextFormat* fmt) const {
    if (!dwrite_ || !fmt || text.empty()) return 0.0f;
    ComPtr<IDWriteTextLayout> layout;
    if (FAILED(dwrite_->CreateTextLayout(
            text.c_str(), static_cast<UINT32>(text.size()), fmt, 320.0f, 40.0f, &layout))) {
      return 0.0f;
    }
    DWRITE_TEXT_METRICS m{};
    layout->GetMetrics(&m);
    return m.widthIncludingTrailingWhitespace;
  }

  void paint_window(HDC target_dc) {
    RECT client{};
    GetClientRect(hwnd_, &client);
    int w = client.right - client.left;
    int h = client.bottom - client.top;
    if (w <= 0 || h <= 0 || !dc_rt_) return;

    HDC mem_dc = CreateCompatibleDC(target_dc);
    HBITMAP bmp = CreateCompatibleBitmap(target_dc, w, h);
    HGDIOBJ old_bmp = SelectObject(mem_dc, bmp);

    if (SUCCEEDED(dc_rt_->BindDC(mem_dc, &client))) {
      const float dpi_f = static_cast<float>(dpi_);
      dc_rt_->SetDpi(dpi_f, dpi_f);
      dc_rt_->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
      dc_rt_->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
      dc_rt_->BeginDraw();

      ComPtr<ID2D1SolidColorBrush> brush;
      dc_rt_->CreateSolidColorBrush(to_d2d(theme_.bg), &brush);
      auto set_col = [&](COLORREF c) { brush->SetColor(to_d2d(c)); };
      auto fill_r = [&](float l, float t, float r, float b, COLORREF c) {
        set_col(c);
        dc_rt_->FillRectangle(D2D1::RectF(l, t, r, b), brush.Get());
      };
      auto stroke_r = [&](float l, float t, float r, float b, COLORREF c) {
        set_col(c);
        dc_rt_->DrawRectangle(D2D1::RectF(l + 0.5f, t + 0.5f, r - 0.5f, b - 0.5f), brush.Get(), 1.0f);
      };
      auto draw_txt = [&](const std::wstring& txt, IDWriteTextFormat* fmt, float l, float t, float r, float b, COLORREF c) {
        if (!fmt || txt.empty()) return;
        set_col(c);
        dc_rt_->DrawText(txt.c_str(), static_cast<UINT32>(txt.size()), fmt, D2D1::RectF(l, t, r, b), brush.Get());
      };

      dc_rt_->Clear(to_d2d(theme_.bg));

      UiSnapshot snap;
      {
        std::lock_guard<std::mutex> lock(mu_);
        snap = ui_;
      }

      // Header: "Spotify Desktop" + Status Pill
      draw_txt(L"Spotify Desktop", fmt_brand_.Get(), 18.0f, 16.0f, 210.0f, 40.0f, theme_.text);

      std::wstring badge_label = L"Idle";
      COLORREF badge_bg = theme_.badge_idle_bg;
      COLORREF badge_fg = theme_.badge_idle_fg;
      COLORREF dot_col = theme_.dot_idle;
      switch (snap.state) {
        case SyncState::TokenNeeded:
          badge_label = L"Token needed";
          break;
        case SyncState::Listening:
          badge_label = L"Listening";
          badge_bg = theme_.badge_ok_bg;
          badge_fg = theme_.badge_ok_fg;
          dot_col = theme_.dot_ok;
          break;
        case SyncState::Paused:
          badge_label = L"Paused";
          break;
        case SyncState::InvalidToken:
          badge_label = L"Invalid token";
          badge_bg = theme_.badge_err_bg;
          badge_fg = theme_.badge_err_fg;
          dot_col = theme_.dot_err;
          break;
        case SyncState::Offline:
          badge_label = L"Offline";
          badge_bg = theme_.badge_err_bg;
          badge_fg = theme_.badge_err_fg;
          dot_col = theme_.dot_err;
          break;
        case SyncState::Idle:
          badge_label = L"Idle";
          break;
      }

      float badge_text_w = measure_text_width(badge_label, fmt_badge_.Get());
      float pill_w = std::ceil(badge_text_w) + 29.0f;
      float pill_l = 342.0f - pill_w;
      fill_r(pill_l, 17.0f, 342.0f, 39.0f, badge_bg);

      set_col(dot_col);
      dc_rt_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(pill_l + 12.0f, 28.0f), 3.0f, 3.0f), brush.Get());
      draw_txt(badge_label, fmt_badge_.Get(), pill_l + 20.0f, 17.0f, 334.0f, 39.0f, badge_fg);

      // Now Playing Card
      fill_r(18.0f, 52.0f, 342.0f, 142.0f, theme_.card);
      stroke_r(18.0f, 52.0f, 342.0f, 142.0f, theme_.border);

      draw_txt(L"NOW PLAYING", fmt_eyebrow_.Get(), 32.0f, 62.0f, 150.0f, 76.0f, theme_.dim);

      double pct = 0.0;
      std::wstring right_meta = L"Idle";
      COLORREF right_col = theme_.dim;
      bool has_track = !snap.title.empty();
      if (has_track) {
        int64_t live_pos = snap.position_ms.value_or(0);
        if (snap.is_playing && snap.position_ms.has_value()) {
          live_pos += std::max<int64_t>(0, epoch_now_ms() - snap.position_at_ms);
        }
        if (snap.duration_ms.has_value() && *snap.duration_ms > 0) {
          live_pos = std::clamp<int64_t>(live_pos, 0, *snap.duration_ms);
          pct = static_cast<double>(live_pos) / static_cast<double>(*snap.duration_ms);
          std::wstring clock = format_clock_w(live_pos) + L" / " + format_clock_w(*snap.duration_ms);
          if (snap.scrobbled) {
            right_meta = L"Scrobbled \u00B7 " + clock;
            right_col = theme_.badge_ok_fg;
          } else if (!snap.is_playing) {
            right_meta = L"Paused \u00B7 " + clock;
          } else {
            right_meta = clock;
          }
        } else {
          right_meta = snap.scrobbled ? L"Scrobbled" : (snap.is_playing ? L"Playing" : L"Paused");
          if (snap.scrobbled) right_col = theme_.badge_ok_fg;
        }
      }

      draw_txt(right_meta, fmt_meta_right_.Get(), 145.0f, 62.0f, 328.0f, 76.0f, right_col);

      std::wstring display_title = has_track ? snap.title : L"Waiting for Spotify Desktop...";
      draw_txt(display_title, fmt_title_.Get(), 32.0f, 81.0f, 328.0f, 102.0f, has_track ? theme_.text : theme_.muted);

      std::wstring display_artist;
      if (has_track) {
        display_artist = snap.artist;
      } else if (snap.state == SyncState::TokenNeeded) {
        display_artist = L"Paste your Scrobble Token below to start syncing";
      } else if (snap.state == SyncState::InvalidToken) {
        display_artist = L"Token rejected — paste a fresh token from euilscrr";
      } else {
        display_artist = L"Play a song on Spotify Desktop to start syncing";
      }
      draw_txt(display_artist, fmt_body_.Get(), 32.0f, 103.0f, 328.0f, 122.0f, theme_.muted);

      // 2px progress line flush at bottom inside Now Playing card
      fill_r(19.0f, 139.0f, 341.0f, 141.0f, theme_.track_bg);
      if (has_track && pct > 0.0) {
        float fill_w = static_cast<float>(322.0 * pct);
        if (fill_w > 0.5f) {
          fill_r(19.0f, 139.0f, 19.0f + fill_w, 141.0f, theme_.green);
        }
      }

      // Scrobble Token section
      draw_txt(L"Scrobble Token", fmt_label_.Get(), 18.0f, 156.0f, 200.0f, 173.0f, theme_.muted);

      fill_r(18.0f, 178.0f, 260.0f, 212.0f, theme_.input_bg);
      stroke_r(18.0f, 178.0f, 260.0f, 212.0f, theme_.border);

      // Save / Update button (Direct2D + DirectWrite)
      bool primary = is_button_primary();
      COLORREF btn_fill = primary ? (hover_btn_ ? theme_.btn_bg_hover : theme_.btn_bg)
                                  : (hover_btn_ ? theme_.card : theme_.bg);
      COLORREF btn_fg = primary ? theme_.btn_text : theme_.text;
      fill_r(268.0f, 178.0f, 342.0f, 212.0f, btn_fill);
      if (!primary) {
        stroke_r(268.0f, 178.0f, 342.0f, 212.0f, theme_.btn_border);
      }
      draw_txt(primary ? L"Save" : L"Update", fmt_btn_.Get(), 268.0f, 178.0f, 342.0f, 212.0f, btn_fg);

      // Footer divider
      fill_r(18.0f, 228.0f, 342.0f, 229.0f, theme_.border);

      // Start with Windows checkbox
      if (startup_enabled_) {
        fill_r(18.0f, 242.0f, 33.0f, 257.0f, theme_.check_bg);
        set_col(theme_.check_fg);
        dc_rt_->DrawLine(D2D1::Point2F(21.5f, 249.5f), D2D1::Point2F(24.2f, 252.2f), brush.Get(), 1.6f);
        dc_rt_->DrawLine(D2D1::Point2F(24.2f, 252.2f), D2D1::Point2F(29.8f, 246.2f), brush.Get(), 1.6f);
      } else {
        fill_r(18.0f, 242.0f, 33.0f, 257.0f, theme_.input_bg);
        stroke_r(18.0f, 242.0f, 33.0f, 257.0f, theme_.btn_border);
      }

      draw_txt(L"Start with Windows", fmt_body_.Get(), 41.0f, 239.0f, 190.0f, 260.0f, theme_.text);
      draw_txt(L"Hide to tray", fmt_body_right_.Get(), 258.0f, 239.0f, 342.0f, 260.0f,
               hover_hide_ ? theme_.text : theme_.muted);

      dc_rt_->EndDraw();
    }

    BitBlt(target_dc, 0, 0, w, h, mem_dc, 0, 0, SRCCOPY);
    SelectObject(mem_dc, old_bmp);
    DeleteObject(bmp);
    DeleteDC(mem_dc);
  }

  static LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    WatcherApp* self = nullptr;
    if (msg == WM_NCCREATE) {
      auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
      self = static_cast<WatcherApp*>(cs->lpCreateParams);
      SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
      self->hwnd_ = hwnd;
    } else {
      self = reinterpret_cast<WatcherApp*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    return self ? self->handle_message(msg, wp, lp) : DefWindowProcW(hwnd, msg, wp, lp);
  }

  LRESULT handle_message(UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
      case WM_CREATE: {
        create_gdi_edit_resources();
        edit_token_ = CreateWindowExW(
            0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL | ES_PASSWORD,
            0, 0, 10, 10, hwnd_, reinterpret_cast<HMENU>(static_cast<intptr_t>(kCtrlTokenEdit)),
            instance_, nullptr);
        SendMessageW(edit_token_, WM_SETFONT, reinterpret_cast<WPARAM>(font_edit_), FALSE);
        SendMessageW(edit_token_, EM_SETPASSWORDCHAR, static_cast<WPARAM>(L'\u2022'), 0);
        SendMessageW(edit_token_, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(L"euil_live_..."));
        if (!token_.empty()) {
          std::wstring wide = utf8_to_wide(token_);
          SetWindowTextW(edit_token_, wide.c_str());
          saved_token_empty_ = false;
        } else {
          saved_token_empty_ = true;
        }
        token_dirty_ = false;
        layout_controls();
        return 0;
      }

      case WM_DPICHANGED: {
        dpi_ = HIWORD(wp);
        auto* suggested = reinterpret_cast<const RECT*>(lp);
        SetWindowPos(
            hwnd_, nullptr, suggested->left, suggested->top,
            suggested->right - suggested->left, suggested->bottom - suggested->top,
            SWP_NOZORDER | SWP_NOACTIVATE);
        create_gdi_edit_resources();
        SendMessageW(edit_token_, WM_SETFONT, reinterpret_cast<WPARAM>(font_edit_), TRUE);
        layout_controls();
        InvalidateRect(hwnd_, nullptr, TRUE);
        return 0;
      }

      case WM_SETTINGCHANGE: {
        bool dark = is_windows_dark_mode();
        if (dark != theme_.dark) {
          theme_ = make_theme(dark);
          apply_window_theme();
        }
        return 0;
      }

      case WM_CTLCOLOREDIT: {
        HDC hdc = reinterpret_cast<HDC>(wp);
        SetTextColor(hdc, theme_.text);
        SetBkColor(hdc, theme_.input_bg);
        return reinterpret_cast<LRESULT>(brush_input_);
      }

      case WM_COMMAND: {
        int id = LOWORD(wp);
        int code = HIWORD(wp);
        if (id == kCtrlTokenEdit && code == EN_CHANGE) {
          if (!token_dirty_) {
            token_dirty_ = true;
            InvalidateRect(hwnd_, &btn_rect_, FALSE);
          }
          return 0;
        }
        break;
      }

      case WM_MOUSEMOVE: {
        POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        bool over_hide = PtInRect(&hide_rect_, pt) != 0;
        bool over_btn = PtInRect(&btn_rect_, pt) != 0;
        if (over_hide != hover_hide_) {
          hover_hide_ = over_hide;
          InvalidateRect(hwnd_, &hide_rect_, FALSE);
        }
        if (over_btn != hover_btn_) {
          hover_btn_ = over_btn;
          InvalidateRect(hwnd_, &btn_rect_, FALSE);
        }
        TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, hwnd_, 0};
        TrackMouseEvent(&tme);
        break;
      }

      case WM_MOUSELEAVE: {
        if (hover_hide_ || hover_btn_) {
          hover_hide_ = false;
          hover_btn_ = false;
          InvalidateRect(hwnd_, nullptr, FALSE);
        }
        return 0;
      }

      case WM_SETCURSOR: {
        if (LOWORD(lp) == HTCLIENT) {
          POINT pt{};
          GetCursorPos(&pt);
          ScreenToClient(hwnd_, &pt);
          if (PtInRect(&btn_rect_, pt) || PtInRect(&checkbox_rect_, pt) || PtInRect(&hide_rect_, pt)) {
            SetCursor(LoadCursorW(nullptr, IDC_HAND));
            return TRUE;
          }
        }
        break;
      }

      case WM_LBUTTONUP: {
        POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        if (PtInRect(&btn_rect_, pt)) {
          on_save_or_update();
          return 0;
        }
        if (PtInRect(&checkbox_rect_, pt)) {
          toggle_startup();
          return 0;
        }
        if (PtInRect(&hide_rect_, pt)) {
          hide_to_tray();
          return 0;
        }
        break;
      }

      case WM_TIMER: {
        if (wp == kTimerUiTick && IsWindowVisible(hwnd_)) {
          InvalidateRect(hwnd_, nullptr, FALSE);
        }
        return 0;
      }

      case WM_APP_STATE: {
        std::wstring tip = L"euilscrr Watcher";
        {
          std::lock_guard<std::mutex> lock(mu_);
          if (!ui_.title.empty()) {
            tip = L"euilscrr \u2014 " + ui_.title + L" - " + ui_.artist;
          }
        }
        update_tray_tooltip(tip);
        if (IsWindowVisible(hwnd_)) InvalidateRect(hwnd_, nullptr, FALSE);
        return 0;
      }

      case WM_APP_SHOW:
        show_window();
        return 0;

      case WM_APP_TRAY: {
        if (lp == WM_LBUTTONUP || lp == WM_LBUTTONDBLCLK) {
          show_window();
        } else if (lp == WM_RBUTTONUP || lp == WM_CONTEXTMENU) {
          show_tray_menu();
        }
        return 0;
      }

      case WM_SYSCOMMAND: {
        if ((wp & 0xFFF0) == SC_MINIMIZE) {
          hide_to_tray();
          return 0;
        }
        break;
      }

      case WM_CLOSE: {
        if (!quitting_) {
          hide_to_tray();
          return 0;
        }
        break;
      }

      case WM_ERASEBKGND:
        return 1;

      case WM_PAINT: {
        PAINTSTRUCT ps{};
        HDC hdc = BeginPaint(hwnd_, &ps);
        paint_window(hdc);
        EndPaint(hwnd_, &ps);
        return 0;
      }

      case WM_DESTROY:
        KillTimer(hwnd_, kTimerUiTick);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd_, msg, wp, lp);
  }

  HINSTANCE instance_ = nullptr;
  HWND hwnd_ = nullptr;
  HWND edit_token_ = nullptr;
  UINT dpi_ = 96;

  std::wstring api_url_;
  std::wstring ui_font_family_ = L"Segoe UI";
  Theme theme_;

  ComPtr<ID2D1Factory> d2d_factory_;
  ComPtr<ID2D1DCRenderTarget> dc_rt_;
  ComPtr<IDWriteFactory> dwrite_;
  ComPtr<IDWriteInMemoryFontFileLoader> mem_font_loader_;
  ComPtr<IDWriteFontCollection1> embedded_fonts_;
  HANDLE gdi_font_handle_ = nullptr;
  ComPtr<IDWriteTextFormat> fmt_brand_;
  ComPtr<IDWriteTextFormat> fmt_eyebrow_;
  ComPtr<IDWriteTextFormat> fmt_title_;
  ComPtr<IDWriteTextFormat> fmt_body_;
  ComPtr<IDWriteTextFormat> fmt_body_right_;
  ComPtr<IDWriteTextFormat> fmt_label_;
  ComPtr<IDWriteTextFormat> fmt_btn_;
  ComPtr<IDWriteTextFormat> fmt_badge_;
  ComPtr<IDWriteTextFormat> fmt_meta_right_;

  HICON icon_small_ = nullptr;
  HICON icon_large_ = nullptr;
  HFONT font_edit_ = nullptr;
  HBRUSH brush_input_ = nullptr;

  RECT btn_rect_{};
  RECT checkbox_rect_{};
  RECT hide_rect_{};
  bool hover_btn_ = false;
  bool hover_hide_ = false;
  bool startup_enabled_ = false;
  bool saved_token_empty_ = true;
  bool token_dirty_ = false;
  bool quitting_ = false;

  std::mutex mu_;
  std::condition_variable cv_;
  std::thread worker_;
  bool stop_flag_ = false;
  bool token_changed_ = false;
  std::string token_;
  UiSnapshot ui_;
};

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR cmd_line, int) {
  SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

  HANDLE mutex = CreateMutexW(nullptr, TRUE, kMutexName);
  if (mutex && GetLastError() == ERROR_ALREADY_EXISTS) {
    if (HWND existing = FindWindowW(kWindowClass, nullptr)) {
      PostMessageW(existing, WM_APP_SHOW, 0, 0);
    }
    CloseHandle(mutex);
    return 0;
  }

  INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_STANDARD_CLASSES};
  InitCommonControlsEx(&icc);

  bool start_minimized = cmd_line && wcsstr(cmd_line, L"--minimized") != nullptr;
  WatcherApp app;
  int code = app.run(instance, start_minimized);

  if (mutex) CloseHandle(mutex);
  return code;
}
