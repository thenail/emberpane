#include "pch.h"
#include "Managed/FrameService.h"

namespace EmberPane::Managed::FrameService
{
    namespace
    {
        COLORREF ToColorRef(Core::FrameColor color)
        {
            if (color.None) return DWMWA_COLOR_NONE;
            return color.UseSystemDefault ? DWMWA_COLOR_DEFAULT : RGB(color.R, color.G, color.B);
        }

        DWM_WINDOW_CORNER_PREFERENCE ToPreference(std::optional<Core::CornerStyle> corners)
        {
            if (!corners) return DWMWCP_DEFAULT;
            return *corners == Core::CornerStyle::RoundSmall ? DWMWCP_ROUNDSMALL : DWMWCP_ROUND;
        }

        void Write(HWND hwnd, COLORREF color)
        {
            DwmSetWindowAttribute(hwnd, DWMWA_BORDER_COLOR, &color, sizeof(color));
        }
    }

    void Paint(ManagedWindow& window, Core::FrameColor color)
    {
        if (window.State != WindowState::Normal) return;

        COLORREF value = ToColorRef(color);
        if (window.Painted == value) return;

        Write(window.Hwnd, value);
        window.Painted = value;
    }

    void Round(ManagedWindow& window, std::optional<Core::CornerStyle> corners)
    {
        if (window.Corners == corners) return;

        auto preference = ToPreference(corners);
        DwmSetWindowAttribute(window.Hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &preference, sizeof(preference));
        window.Corners = corners;
    }

    void Release(ManagedWindow& window)
    {
        Write(window.Hwnd, DWMWA_COLOR_DEFAULT);
        window.Painted = DWMWA_COLOR_DEFAULT;

        Round(window, std::nullopt);
    }
}
