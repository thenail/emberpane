#include "pch.h"
#include "Native/WindowFacts.h"
#include "Native/ProcessTree.h"
#include "Native/Shell.h"

namespace EmberPane::Native
{
    namespace
    {
        bool HasCaption(LONG_PTR style)
        {
            // WS_CAPTION is two bits; either alone means something else.
            return (style & WS_CAPTION) == WS_CAPTION;
        }

        std::wstring ClassOf(HWND hwnd)
        {
            wchar_t name[256]{};
            GetClassNameW(hwnd, name, 256);
            return name;
        }

        std::wstring TitleOf(HWND hwnd)
        {
            std::wstring title(512, L'\0');
            title.resize(GetWindowTextW(hwnd, title.data(), static_cast<int>(title.size())));
            return title;
        }

        std::wstring FileName(std::wstring const& path)
        {
            return path.substr(path.find_last_of(L'\\') + 1);
        }
    }

    bool CouldBeAppWindow(HWND hwnd)
    {
        if (!IsWindowVisible(hwnd) || GetAncestor(hwnd, GA_ROOT) != hwnd) return false;
        if (GetWindow(hwnd, GW_OWNER)) return false;

        auto ex = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
        return !(ex & (WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE));
    }

    WindowReading ReadWindow(HWND hwnd, VirtualDesktops& desktops)
    {
        WindowReading reading;
        reading.ProcessId = ProcessTree::OwnerOf(hwnd);
        reading.ExePath = ProcessTree::ExePath(reading.ProcessId);

        auto style = GetWindowLongPtrW(hwnd, GWL_STYLE);
        auto ex = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);

        DWORD cloaked = 0;
        DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked));

        auto& f = reading.Facts;
        f.Visible = IsWindowVisible(hwnd) != 0;
        f.HasOwner = GetWindow(hwnd, GW_OWNER) != nullptr;
        f.ToolWindow = (ex & WS_EX_TOOLWINDOW) != 0;
        f.NoActivate = (ex & WS_EX_NOACTIVATE) != 0;
        f.HasCaption = HasCaption(style);
        f.Resizable = (style & WS_THICKFRAME) != 0;
        f.OwnProcess = _wcsicmp(reading.ExePath.c_str(), Shell::ExecutablePath().c_str()) == 0;
        f.CloakedHere = cloaked != 0 && desktops.IsOnCurrent(hwnd);
        f.ClassName = ClassOf(hwnd);
        f.ExeName = FileName(reading.ExePath);
        f.Title = TitleOf(hwnd);
        return reading;
    }

    bool IsFullscreen(HWND hwnd)
    {
        if (HasCaption(GetWindowLongPtrW(hwnd, GWL_STYLE))) return false;

        RECT window{};
        if (!GetWindowRect(hwnd, &window)) return false;

        MONITORINFO monitor{ sizeof(monitor) };
        if (!GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &monitor)) return false;

        return window.left <= monitor.rcMonitor.left && window.top <= monitor.rcMonitor.top &&
               window.right >= monitor.rcMonitor.right && window.bottom >= monitor.rcMonitor.bottom;
    }
}
