#pragma once

#include <optional>
#include <string>
#include "HeatRamp.h"
#include "Settings.h"

namespace EmberPane::Managed
{
    enum class WindowState
    {
        Normal,
        Minimized,
        Maximized,
        Fullscreen,
    };

    /// What the host knows about one window.
    struct ManagedWindow
    {
        HWND Hwnd{};

        /// The real one: for a UWP app, not ApplicationFrameHost's.
        DWORD ProcessId{ 0 };

        std::wstring ExeName;
        std::wstring ClassName;

        /// The name to show a person.
        std::wstring AppName;

        Core::Disposition Disposition{ Core::Disposition::Tile };
        WindowState State{ WindowState::Normal };

        /// It would not move: usually an app running as administrator.
        bool Immovable{ false };

        /// The colour for how hard its app is working, as last sampled.
        Core::FrameColor Heat;

        /// Last border colour written. Empty forces a write, which is how a
        /// restart cleans up after a crash: a colour outlives the process
        /// that set it.
        std::optional<COLORREF> Painted;

        /// Last corner preference written; empty when it is Windows' own.
        std::optional<Core::CornerStyle> Corners;
    };
}
