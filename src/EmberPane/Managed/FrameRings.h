#pragma once

#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>
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

        /// A window not tracked here makes a hole in a band, or did, or may
        /// now: one shown, hidden or moved near a frame.
        bool Touches(HWND hwnd) const;
        void OnWatch();
        void SetHeld(bool held);

        WindowTracker& m_tracker;
        Arranger& m_arranger;
        Core::AppSettings const& m_settings;

        std::map<std::wstring, std::unique_ptr<FrameLayer>> m_layers;

        /// As last drawn: the windows with a band, the windows making holes in
        /// the bands, and how far out each band can reach.
        std::set<HWND> m_framed;
        std::set<HWND> m_covering;
        std::vector<Core::Rect> m_reach;

        winrt::Windows::System::DispatcherQueueTimer m_watch{ nullptr };
        ULONGLONG m_quietAt{ 0 };

        /// The watch is on. Not the timer's IsRunning, which reads false
        /// inside its own tick, where the surface would be let go every time.
        bool m_following{ false };

        /// While a window or gap is held: how the frames kept up.
        bool m_held{ false };
        int m_heldUpdates{ 0 };
        double m_heldTotalMs{ 0 };
        double m_heldSlowestMs{ 0 };
    };
}
