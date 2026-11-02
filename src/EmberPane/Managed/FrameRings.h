#pragma once

#include <map>
#include <memory>
#include <string>
#include "Managed/Arranger.h"
#include "Managed/Overlays.h"
#include "Managed/WindowTracker.h"

namespace EmberPane::Managed
{
    /// Each window's frame, as Settings asks for it. Windows' own border does
    /// it wherever it can, as with no dark line, and then nothing here draws or
    /// watches anything. Only windows in a layout can have more: a frame goes
    /// in the gap around a window, and a window on its own has none.
    class FrameRings
    {
    public:
        FrameRings(winrt::Windows::System::DispatcherQueue const& dispatcher, WindowTracker& tracker,
                   Arranger& arranger, Core::AppSettings const& settings);
        ~FrameRings();

        FrameRings(FrameRings const&) = delete;
        FrameRings& operator=(FrameRings const&) = delete;

        /// Brings every window's frame up to date. With the tick, and whenever
        /// colours or settings have changed.
        void Update();

    private:
        /// Something on screen is changing: frames follow it for a moment.
        void Follow();
        void OnWatch();

        WindowTracker& m_tracker;
        Arranger& m_arranger;
        Core::AppSettings const& m_settings;

        std::map<std::wstring, std::unique_ptr<FrameLayer>> m_layers;

        winrt::Windows::System::DispatcherQueueTimer m_watch{ nullptr };
        ULONGLONG m_quietAt{ 0 };
    };
}
