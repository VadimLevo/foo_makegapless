#pragma once
#include "foo_makegapless.h"

class GaplessMixPrefsInstance : public preferences_page_instance {
public:
    GaplessMixPrefsInstance(HWND parent, preferences_page_callback::ptr cb);
    HWND get_wnd() override { return m_hWnd; }
    t_uint32 get_state() override;
    void apply() override;
    void reset() override;
    static GaplessMixPrefsInstance* from_hwnd(HWND h) {
        return (GaplessMixPrefsInstance*)GetWindowLongPtr(h, GWLP_USERDATA);
    }
    void notify_changed();
    void update_control_state(HWND hDlg);
private:
    bool has_changes();
    HWND m_hWnd = nullptr;
    preferences_page_callback::ptr m_callback;
};