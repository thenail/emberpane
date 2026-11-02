#include "pch.h"
#include "Native/SystemEvents.h"

#include <Dbt.h>

namespace EmberPane::Native
{
    namespace
    {
        constexpr UINT_PTR DebounceTimer = 1;

        /// How long to wait for a burst of display messages to settle. Docking
        /// and resume can churn for seconds, but two covers the common
        /// unplug/replug case without feeling sluggish.
        constexpr UINT DebounceMs = 2000;

        /// The display attached to the session in use. Unlike
        /// GUID_MONITOR_POWER_ON (deprecated after Windows 7) this reports
        /// "dimmed" separately and is the one Windows still maintains.
        constexpr GUID ConsoleDisplayState = { 0x6fe69556, 0x704a, 0x47a0, { 0x8f, 0x24, 0xc2, 0x8d, 0x93, 0x6f, 0xda, 0x47 } };
    }

    SystemEvents::SystemEvents()
    {
        WNDCLASSEXW wc{ sizeof(wc) };
        wc.lpfnWndProc = WndProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = L"EmberPane.SystemEvents";
        RegisterClassExW(&wc);

        m_hwnd = CreateWindowExW(WS_EX_TOOLWINDOW, wc.lpszClassName, L"EmberPane events", WS_POPUP,
                                 0, 0, 0, 0, nullptr, nullptr, wc.hInstance, this);

        m_powerRegistration = RegisterPowerSettingNotification(m_hwnd, &ConsoleDisplayState,
                                                               DEVICE_NOTIFY_WINDOW_HANDLE);
    }

    SystemEvents::~SystemEvents()
    {
        if (m_powerRegistration)
            UnregisterPowerSettingNotification(m_powerRegistration);
        if (m_hwnd)
            DestroyWindow(m_hwnd);
    }

    void SystemEvents::ScheduleDisplaysChanged()
    {
        // Re-arming the timer collapses a burst into one notification.
        SetTimer(m_hwnd, DebounceTimer, DebounceMs, nullptr);
    }

    LRESULT SystemEvents::Handle(UINT message, WPARAM wParam, LPARAM lParam)
    {
        switch (message)
        {
        case WM_DISPLAYCHANGE:
            ScheduleDisplaysChanged();
            break;

        case WM_DEVICECHANGE:
            // Covers monitors appearing over USB-C and docks, which do not
            // always raise WM_DISPLAYCHANGE.
            if (wParam == DBT_DEVNODES_CHANGED)
                ScheduleDisplaysChanged();
            break;

        case WM_TIMER:
            if (wParam == DebounceTimer)
            {
                KillTimer(m_hwnd, DebounceTimer);
                if (DisplaysChanged) DisplaysChanged();
            }
            break;

        case WM_POWERBROADCAST:
            if (wParam == PBT_APMRESUMESUSPEND || wParam == PBT_APMRESUMEAUTOMATIC)
            {
                // Monitors can come back in a different arrangement.
                ScheduleDisplaysChanged();
            }
            else if (wParam == PBT_POWERSETTINGCHANGE)
            {
                auto setting = reinterpret_cast<POWERBROADCAST_SETTING const*>(lParam);
                if (setting && setting->PowerSetting == ConsoleDisplayState && setting->DataLength >= sizeof(DWORD))
                {
                    auto state = static_cast<DisplayPower>(*reinterpret_cast<DWORD const*>(setting->Data));
                    if (state != m_power)
                    {
                        m_power = state;
                        if (DisplayPowerChanged) DisplayPowerChanged(state);
                    }
                }
            }
            return TRUE;

        case WM_SETTINGCHANGE:
            if (wParam == SPI_SETWORKAREA)
                if (WorkAreaChanged) WorkAreaChanged();

            // Light/dark switches arrive as this setting name.
            if (lParam && wcscmp(reinterpret_cast<wchar_t const*>(lParam), L"ImmersiveColorSet") == 0)
                if (ThemeChanged) ThemeChanged();
            break;
        }

        return DefWindowProcW(m_hwnd, message, wParam, lParam);
    }

    LRESULT CALLBACK SystemEvents::WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
    {
        if (message == WM_NCCREATE)
        {
            auto create = reinterpret_cast<CREATESTRUCTW*>(lParam);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
        }

        auto self = reinterpret_cast<SystemEvents*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (!self || !self->m_hwnd)
            return DefWindowProcW(hwnd, message, wParam, lParam);

        return self->Handle(message, wParam, lParam);
    }
}
