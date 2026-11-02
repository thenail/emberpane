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

        /// The desktop a window is on, as text to key a layout by. Empty when
        /// it cannot be told, as for a hidden UWP window, which reports the
        /// null GUID, and for system windows, where the call fails.
        std::wstring DesktopOf(HWND hwnd);

    private:
        IVirtualDesktopManager* Manager();

        /// It dies with Explorer, and is made again on the next call.
        winrt::com_ptr<IVirtualDesktopManager> m_manager;
    };
}
