#pragma once

#include <string>
#include <vector>

namespace EmberPane::Native
{
    struct Monitor
    {
        /// The monitor's device path, which survives a replug and a reboot,
        /// unlike the HMONITOR.
        std::wstring Key;

        HMONITOR Handle{};
        RECT Bounds{};
        RECT Work{};
        UINT Dpi{ 96 };
        bool Primary{ false };
    };

    /// The monitors Windows has active.
    std::vector<Monitor> ListMonitors();

    /// As one comparable string: each monitor's key, work area and scale.
    /// Compared on the tick as well as following Windows' messages, because
    /// some docks and KVMs send none.
    std::wstring SignatureOf(std::vector<Monitor> const& monitors);
}
