#pragma once

#include <shobjidl_core.h>

namespace EmberPane::Native
{
    /// IVirtualDesktopManager: the documented interface. The undocumented ones
    /// that can list and switch desktops change between Windows builds, and
    /// nothing here needs them.
    class VirtualDesktops
    {
    public:
        /// True when it cannot be told: most windows are, and a window wrongly
        /// thought absent would lose its frame.
        bool IsOnCurrent(HWND hwnd);

        /// Whether a window is on the desktop being looked at, or nothing when
        /// that cannot be told.
        std::optional<bool> OnCurrent(HWND hwnd);

        /// The desktop a window is on, as text to key a layout by. Empty when
        /// it cannot be told, as for a hidden UWP window, which reports the
        /// null GUID, and for system windows, where the call fails. Also for
        /// some windows that are plainly on a desktop: a UWP frame or a
        /// packaged WinUI app's window can fail with TYPE_E_ELEMENTNOTFOUND
        /// for as long as it is open, and a window just shown may not have
        /// been given a desktop yet. Measured on Windows 11 build 26300.
        std::wstring DesktopOf(HWND hwnd);

    private:
        IVirtualDesktopManager* Manager();

        /// It dies with Explorer, and is made again on the next call.
        winrt::com_ptr<IVirtualDesktopManager> m_manager;
    };
}
