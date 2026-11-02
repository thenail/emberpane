#pragma once

#include <string>
#include "Native/VirtualDesktops.h"
#include "WindowRules.h"

namespace EmberPane::Native
{
    struct WindowReading
    {
        Core::WindowFacts Facts;
        DWORD ProcessId{ 0 };
        std::wstring ExePath;
    };

    /// False for what cannot be an app window, from its styles alone: nearly
    /// every window event is a tooltip or a menu, and this answers without
    /// opening a process.
    bool CouldBeAppWindow(HWND hwnd);

    /// Everything Core::Classify needs to know about a window. Touches only
    /// what cannot block: GetWindowTextW on another process's window reads
    /// Windows' copy of the title and sends the window no message, so a hung
    /// app cannot hang this.
    WindowReading ReadWindow(HWND hwnd, VirtualDesktops& desktops);

    /// A window covering its whole monitor with no caption: a video, a game, a
    /// slide show.
    bool IsFullscreen(HWND hwnd);
}
