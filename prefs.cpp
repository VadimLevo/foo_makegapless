#include "prefs.h"
#include "resource.h"
#include <uxtheme.h>
#include <shlobj.h>
#pragma comment(lib, "uxtheme.lib")
#pragma comment(lib, "shell32.lib")

static HBRUSH g_hbrBg = NULL, g_hbrEdit = NULL;

static bool IsDarkMode() { return ui_config_manager::g_is_dark_mode(); }
static HBRUSH BgBrush() { if (!g_hbrBg)   g_hbrBg = CreateSolidBrush(RGB(45, 45, 45)); return g_hbrBg; }
static HBRUSH EditBrush() { if (!g_hbrEdit) g_hbrEdit = CreateSolidBrush(RGB(30, 30, 30)); return g_hbrEdit; }

static void CleanupDarkBrushes() {
    if (g_hbrBg) { DeleteObject(g_hbrBg); g_hbrBg = NULL; }
    if (g_hbrEdit) { DeleteObject(g_hbrEdit); g_hbrEdit = NULL; }
}

static BOOL CALLBACK EnumChildApplyDarkTheme(HWND hChild, LPARAM) {
    wchar_t cls[64] = {};
    GetClassNameW(hChild, cls, 64);

    if (_wcsicmp(cls, L"Button") == 0) {
        LONG style = GetWindowLong(hChild, GWL_STYLE);
        LONG type = style & 0x0F;

        if (type == BS_AUTORADIOBUTTON || type == BS_RADIOBUTTON ||
            type == BS_AUTOCHECKBOX || type == BS_CHECKBOX ||
            type == BS_AUTO3STATE || type == BS_3STATE) {
            SetWindowTheme(hChild, L" ", L" ");
        }
        else if (type == BS_GROUPBOX) {
            SetWindowTheme(hChild, L" ", L" ");
        }
        else {
            SetWindowTheme(hChild, L"DarkMode_Explorer", NULL);
        }
    }
    else if (_wcsicmp(cls, L"Edit") == 0 || _wcsicmp(cls, L"ComboBox") == 0) {
        SetWindowTheme(hChild, L"DarkMode_CFD", NULL);
    }
    else if (_wcsicmp(cls, L"Static") == 0) {
        SetWindowTheme(hChild, L" ", L" ");
    }
    return TRUE;
}

static void ApplyDark(HWND hDlg) {
    if (!hDlg) return;
    SetWindowTheme(hDlg, L"DarkMode_Explorer", NULL);
    EnumChildWindows(hDlg, EnumChildApplyDarkTheme, 0);
    RedrawWindow(hDlg, NULL, NULL,
        RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW);
}

// ---------- instance ----------
GaplessMixPrefsInstance::GaplessMixPrefsInstance(HWND parent,
    preferences_page_callback::ptr cb) : m_callback(cb) {
    extern INT_PTR CALLBACK PrefsDlgProc(HWND, UINT, WPARAM, LPARAM);
    m_hWnd = CreateDialogParam(core_api::get_my_instance(),
        MAKEINTRESOURCE(IDD_PREFS), parent, PrefsDlgProc, (LPARAM)this);
}

t_uint32 GaplessMixPrefsInstance::get_state() {
    t_uint32 s = preferences_state::dark_mode_supported | preferences_state::resettable;
    if (m_hWnd && has_changes()) s |= preferences_state::changed;
    return s;
}

void GaplessMixPrefsInstance::notify_changed() {
    if (m_callback.is_valid()) m_callback->on_state_changed();
}

void GaplessMixPrefsInstance::update_control_state(HWND hDlg) {
    if (!hDlg) return;
    BOOL custom = (IsDlgButtonChecked(hDlg, IDC_RADIO_CUSTOM) == BST_CHECKED);
    EnableWindow(GetDlgItem(hDlg, IDC_EDIT_CUSTOM_DIR), custom);
    EnableWindow(GetDlgItem(hDlg, IDC_BTN_BROWSE), custom);
    BOOL subf = (IsDlgButtonChecked(hDlg, IDC_CHK_SUBFOLDER) == BST_CHECKED);
    EnableWindow(GetDlgItem(hDlg, IDC_EDIT_SUBFOLDER), subf);
}

bool GaplessMixPrefsInstance::has_changes() {
    if (!m_hWnd) return false;
    auto chk = [&](int id, auto& cfgvar) {
        return (IsDlgButtonChecked(m_hWnd, id) == BST_CHECKED) != (bool)cfgvar;
        };
    if (chk(IDC_CHK_SUBFOLDER, cfg_create_subfolder)) return true;
    if (chk(IDC_CHK_ADD_PL, cfg_add_to_playlist)) return true;

    bool cur_src = (IsDlgButtonChecked(m_hWnd, IDC_RADIO_SRC) == BST_CHECKED);
    if (cur_src != (bool)cfg_use_source_dir) return true;

    if ((int)GetDlgItemInt(m_hWnd, IDC_EDIT_SCAN_DEPTH, nullptr, FALSE) != (int)cfg_scan_depth_sec) return true;
    if ((int)GetDlgItemInt(m_hWnd, IDC_EDIT_MIN_SIL, nullptr, FALSE) != (int)cfg_min_silence_ms) return true;
    if ((int)GetDlgItemInt(m_hWnd, IDC_EDIT_FADE, nullptr, FALSE) != (int)cfg_fade_ms) return true;

    wchar_t buf[1024];
    pfc::stringcvt::string_utf8_from_wide s8;
    pfc::string8 tmp;

    GetDlgItemTextW(m_hWnd, IDC_EDIT_CUSTOM_DIR, buf, 1024);
    s8.convert(buf); tmp = s8.get_ptr();
    if (tmp != cfg_custom_dir.get_ptr()) return true;

    GetDlgItemTextW(m_hWnd, IDC_EDIT_SUBFOLDER, buf, 1024);
    s8.convert(buf); tmp = s8.get_ptr();
    if (tmp != cfg_subfolder_name.get_ptr()) return true;

    GetDlgItemTextW(m_hWnd, IDC_EDIT_IMAGE_NAME, buf, 1024);
    s8.convert(buf); tmp = s8.get_ptr();
    if (tmp != cfg_image_name.get_ptr()) return true;

    GetDlgItemTextW(m_hWnd, IDC_EDIT_CUE_NAME, buf, 1024);
    s8.convert(buf); tmp = s8.get_ptr();
    if (tmp != cfg_cue_name.get_ptr()) return true;

    return false;
}

void GaplessMixPrefsInstance::apply() {
    if (!m_hWnd) return;
    cfg_create_subfolder = (IsDlgButtonChecked(m_hWnd, IDC_CHK_SUBFOLDER) == BST_CHECKED);
    cfg_add_to_playlist = (IsDlgButtonChecked(m_hWnd, IDC_CHK_ADD_PL) == BST_CHECKED);
    cfg_use_source_dir = (IsDlgButtonChecked(m_hWnd, IDC_RADIO_SRC) == BST_CHECKED);
    cfg_scan_depth_sec = (int)GetDlgItemInt(m_hWnd, IDC_EDIT_SCAN_DEPTH, nullptr, FALSE);
    cfg_min_silence_ms = (int)GetDlgItemInt(m_hWnd, IDC_EDIT_MIN_SIL, nullptr, FALSE);
    cfg_fade_ms = (int)GetDlgItemInt(m_hWnd, IDC_EDIT_FADE, nullptr, FALSE);

    wchar_t buf[1024];
    pfc::stringcvt::string_utf8_from_wide s8;

    GetDlgItemTextW(m_hWnd, IDC_EDIT_CUSTOM_DIR, buf, 1024);
    s8.convert(buf); cfg_custom_dir.set(s8.get_ptr());

    GetDlgItemTextW(m_hWnd, IDC_EDIT_SUBFOLDER, buf, 1024);
    s8.convert(buf); cfg_subfolder_name.set(s8.get_ptr());

    GetDlgItemTextW(m_hWnd, IDC_EDIT_IMAGE_NAME, buf, 1024);
    s8.convert(buf); cfg_image_name.set(s8.get_ptr());

    GetDlgItemTextW(m_hWnd, IDC_EDIT_CUE_NAME, buf, 1024);
    s8.convert(buf); cfg_cue_name.set(s8.get_ptr());

    notify_changed();
}

void GaplessMixPrefsInstance::reset() {
    if (!m_hWnd) return;

    CheckDlgButton(m_hWnd, IDC_RADIO_CUSTOM, BST_UNCHECKED);
    CheckDlgButton(m_hWnd, IDC_RADIO_SRC, BST_CHECKED);
    CheckDlgButton(m_hWnd, IDC_CHK_SUBFOLDER, BST_CHECKED);
    CheckDlgButton(m_hWnd, IDC_CHK_ADD_PL, BST_CHECKED);

    SetDlgItemInt(m_hWnd, IDC_EDIT_SCAN_DEPTH, 5, FALSE);
    SetDlgItemInt(m_hWnd, IDC_EDIT_MIN_SIL, 5, FALSE);
    SetDlgItemInt(m_hWnd, IDC_EDIT_FADE, 0, FALSE);

    pfc::stringcvt::string_wide_from_utf8 w;
    SetDlgItemTextW(m_hWnd, IDC_EDIT_CUSTOM_DIR, L""); // при сбросе путь очищается
    w.convert("GaplessMix"); SetDlgItemTextW(m_hWnd, IDC_EDIT_SUBFOLDER, w);
    w.convert("gaplessmix"); SetDlgItemTextW(m_hWnd, IDC_EDIT_IMAGE_NAME, w);
    w.convert("gaplessmix"); SetDlgItemTextW(m_hWnd, IDC_EDIT_CUE_NAME, w);

    update_control_state(m_hWnd);
    notify_changed();
}

// ---------- dialog proc ----------
INT_PTR CALLBACK PrefsDlgProc(HWND hDlg, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_INITDIALOG: {
        SetWindowLongPtr(hDlg, GWLP_USERDATA, lp);
        auto* self = GaplessMixPrefsInstance::from_hwnd(hDlg);

        SendDlgItemMessage(hDlg, IDC_SPIN_SCAN_DEPTH, UDM_SETBUDDY, (WPARAM)GetDlgItem(hDlg, IDC_EDIT_SCAN_DEPTH), 0);
        SendDlgItemMessage(hDlg, IDC_SPIN_MIN_SIL, UDM_SETBUDDY, (WPARAM)GetDlgItem(hDlg, IDC_EDIT_MIN_SIL), 0);
        SendDlgItemMessage(hDlg, IDC_SPIN_FADE, UDM_SETBUDDY, (WPARAM)GetDlgItem(hDlg, IDC_EDIT_FADE), 0);

        SendDlgItemMessage(hDlg, IDC_SPIN_SCAN_DEPTH, UDM_SETRANGE32, (WPARAM)1, (LPARAM)3600);
        SendDlgItemMessage(hDlg, IDC_SPIN_MIN_SIL, UDM_SETRANGE32, (WPARAM)1, (LPARAM)60000);
        SendDlgItemMessage(hDlg, IDC_SPIN_FADE, UDM_SETRANGE32, (WPARAM)0, (LPARAM)1000);

        SetDlgItemInt(hDlg, IDC_EDIT_SCAN_DEPTH, (UINT)cfg_scan_depth_sec, FALSE);
        SetDlgItemInt(hDlg, IDC_EDIT_MIN_SIL, (UINT)cfg_min_silence_ms, FALSE);
        SetDlgItemInt(hDlg, IDC_EDIT_FADE, (UINT)cfg_fade_ms, FALSE);

        CheckDlgButton(hDlg, IDC_CHK_SUBFOLDER, cfg_create_subfolder ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(hDlg, IDC_CHK_ADD_PL, cfg_add_to_playlist ? BST_CHECKED : BST_UNCHECKED);

        if (cfg_use_source_dir) CheckDlgButton(hDlg, IDC_RADIO_SRC, BST_CHECKED);
        else                    CheckDlgButton(hDlg, IDC_RADIO_CUSTOM, BST_CHECKED);

        {
            pfc::stringcvt::string_wide_from_utf8 w;
            w.convert(cfg_custom_dir.get_ptr()); SetDlgItemTextW(hDlg, IDC_EDIT_CUSTOM_DIR, w);
            w.convert(cfg_subfolder_name.get_ptr()); SetDlgItemTextW(hDlg, IDC_EDIT_SUBFOLDER, w);
            w.convert(cfg_image_name.get_ptr()); SetDlgItemTextW(hDlg, IDC_EDIT_IMAGE_NAME, w);
            w.convert(cfg_cue_name.get_ptr()); SetDlgItemTextW(hDlg, IDC_EDIT_CUE_NAME, w);
        }

        if (self) self->update_control_state(hDlg);
        if (IsDarkMode()) ApplyDark(hDlg);
        return TRUE;
    }
    case WM_COMMAND: {
        const WORD id = LOWORD(wp), code = HIWORD(wp);
        auto* self = GaplessMixPrefsInstance::from_hwnd(hDlg);

        if (id == IDC_RADIO_SRC || id == IDC_RADIO_CUSTOM || id == IDC_CHK_SUBFOLDER) {
            if (self) self->update_control_state(hDlg);
            if (self) self->notify_changed();
            return TRUE;
        }
        if (id == IDC_BTN_BROWSE && code == BN_CLICKED) {
            BROWSEINFOW bi = {};
            bi.hwndOwner = hDlg;
            bi.lpszTitle = L"Select output directory";
            bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE | BIF_USENEWUI;

            LPITEMIDLIST pidl = SHBrowseForFolderW(&bi);
            if (pidl) {
                wchar_t path[MAX_PATH];
                if (SHGetPathFromIDListW(pidl, path)) {
                    SetDlgItemTextW(hDlg, IDC_EDIT_CUSTOM_DIR, path);
                    CheckDlgButton(hDlg, IDC_RADIO_CUSTOM, BST_CHECKED);
                    if (self) self->update_control_state(hDlg);
                    if (self) self->notify_changed();
                }
                CoTaskMemFree(pidl);
            }
            return TRUE;
        }
        if (code == EN_CHANGE || code == BN_CLICKED) {
            if (self) self->notify_changed();
        }
        break;
    }
    case WM_CTLCOLORDLG:
        if (IsDarkMode()) return (INT_PTR)BgBrush();
        break;
    case WM_CTLCOLORBTN:
    case WM_CTLCOLORSTATIC: {
        if (IsDarkMode()) {
            HDC hdc = (HDC)wp;
            HWND hWnd = (HWND)lp;
            if (!IsWindowEnabled(hWnd)) {
                SetTextColor(hdc, RGB(120, 120, 120));
            }
            else {
                SetTextColor(hdc, RGB(220, 220, 220));
            }
            SetBkMode(hdc, TRANSPARENT);
            return (INT_PTR)BgBrush();
        }
        break;
    }
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX:
        if (IsDarkMode()) {
            HDC hdc = (HDC)wp;
            SetTextColor(hdc, RGB(220, 220, 220));
            SetBkColor(hdc, RGB(30, 30, 30));
            return (INT_PTR)EditBrush();
        }
        break;
    case WM_DESTROY:
        CleanupDarkBrushes();
        break;
    }
    return FALSE;
}

// ---------- page ----------
class GaplessMixPrefsPage : public preferences_page_v3 {
public:
    const char* get_name() override { return "Gapless Mix"; }
    GUID get_guid() override { return guid_makegapless_prefs; }
    GUID get_parent_guid() override { return preferences_page::guid_tools; }
    preferences_page_instance::ptr instantiate(HWND parent,
        preferences_page_callback::ptr cb) override {
        return fb2k::service_new<GaplessMixPrefsInstance>(parent, cb);
    }
};

static preferences_page_factory_t<GaplessMixPrefsPage> g_prefs_factory;