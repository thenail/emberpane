#pragma once

#include <compare>
#include <map>
#include <memory>
#include <set>
#include <string>
#include "LayoutPresets.h"
#include "Managed/Overlays.h"
#include "Managed/WindowTracker.h"
#include "Native/Monitors.h"

namespace EmberPane::Managed
{
    /// One layout per monitor and virtual desktop.
    struct TreeKey
    {
        std::wstring Monitor;
        std::wstring Desktop;

        auto operator<=>(TreeKey const&) const = default;
    };

    /// A window of the layout, on the desktop being looked at, and not in the
    /// user's hand.
    struct ShownWindow
    {
        HWND Hwnd{};
        std::wstring Monitor;
        Core::Rect Bounds;
        UINT Dpi{ 96 };
        int Gap{ 0 };

        /// Another window of its area is in front of it.
        bool Behind{ false };

        /// Not yet where it was asked to go, as a window just dropped is.
        bool Settling{ false };
    };

    /// Puts windows in the areas of the chosen layout, and keeps them there:
    /// places them, reads them back, learns what they refuse, and follows what
    /// the user drags. Nothing here runs unless arranging is on.
    class Arranger
    {
    public:
        Arranger(winrt::Windows::System::DispatcherQueue const& dispatcher, WindowTracker& tracker,
                 Core::AppSettings const& settings);
        ~Arranger();

        Arranger(Arranger const&) = delete;
        Arranger& operator=(Arranger const&) = delete;

        /// Arranging was switched on or off, or the layout, gap or margin moved.
        void SettingsChanged();

        /// With the sampling tick. Reads only, unless something has moved.
        void Reconcile();

        /// The display arrangement may have changed.
        void DisplaysChanged();

        /// The display went off or came back. Nothing polls while it is off.
        void SetDisplayOn(bool on);

        /// Front to back within each monitor.
        std::vector<ShownWindow> OnShow();

        /// A window or a gap is in the user's hand.
        bool Holding() const;

        /// The layout's areas are changing under the user's hand: a window's
        /// edge or a gap is being dragged, and the windows follow it.
        bool Resizing() const;

        /// What the frames must stay beneath while a window is moved: the
        /// lowest of the window itself, the guide just beneath it and the
        /// preview beneath that. None with nothing being moved.
        HWND InHand() const;

        /// A window has been told to move.
        Event<> Moved;

    private:
        /// What is known about a window's place.
        struct Placement
        {
            TreeKey Tree;

            /// As given to SetWindowPos, and as the window ended up.
            RECT Asked{};
            RECT Settled{};
            RECT Before{};

            /// The scale of the monitor it was on when asked.
            UINT AskedDpi{ 96 };

            /// The window has not shown yet what it does: many apps restore
            /// their saved geometry just after showing.
            bool Fresh{ true };

            int Retries{ 0 };
            int Unmoved{ 0 };

            int Corrections{ 0 };
            ULONGLONG LastCorrection{ 0 };

            /// Tries at clearing Windows' snapped mark; see Place.
            int Unsnaps{ 0 };

            /// What this window refused to go below, in visible pixels.
            Core::Size Minimum;
        };

        /// A preset's areas, and which window is in which. Windows beyond the
        /// number of areas share one, in front of each other.
        struct Layout
        {
            Core::LayoutTree Areas;
            std::map<HWND, Core::WindowId> Members;
        };

        struct Space
        {
            Core::Rect Bounds;
            int Gap{ 0 };
            Core::MinimumSizes Minimums;
        };

        /// Where a dragged window would land.
        struct Drop
        {
            TreeKey Tree;
            Core::WindowId Area{ 0 };
        };

        struct Dragged
        {
            HWND Hwnd{};
            RECT Start{};
            bool Moving{ false };
            std::optional<Drop> Landing;

            /// Near a gap between two areas: let go here, it leaves the layout.
            bool Loosening{ false };

            /// A resize the layout follows as it goes: where it last was, and
            /// the areas as they were before it, for Esc or for a drag that
            /// turns out to be a move.
            RECT Resized{};
            std::optional<Core::LayoutTree> AreasBefore;
        };

        struct Gap
        {
            TreeKey Tree;
            Core::SplitId Split{ 0 };
            Core::Axis Direction{ Core::Axis::SideBySide };
            Core::Rect Bounds;
        };

        bool Arranging() const;

        /// Windows' Snap answers the same drag as the preview, so it is off
        /// while arranging is on. See Native/WindowDocking.h.
        void HoldSnap();
        void ReleaseSnap();

        void Start();
        void Stop();

        Layout& LayoutFor(TreeKey const& key);
        Core::WindowId EmptiestArea(Layout const& layout) const;
        size_t CountIn(Layout const& layout, Core::WindowId area) const;

        /// An area with nothing in it takes a window from the fullest one, if
        /// that has one to spare. `except` stays where it is: moved out of an
        /// area and into another, a window trades places this way.
        void Fill(Layout& layout, Core::WindowId area, std::optional<HWND> except);

        /// Into the layout of its own monitor, or of `tree` when given.
        void AddToLayout(HWND hwnd, std::optional<Core::WindowId> area, std::optional<TreeKey> tree = std::nullopt);
        void RemoveFromLayout(HWND hwnd);
        void Float(HWND hwnd);

        /// Dropped near a gap: out of the layout, where the user let it go.
        void Loosen(HWND hwnd);

        /// Dropped on an area again, after Loosen.
        void Rejoin(HWND hwnd, Drop const& drop);

        /// The preset changed: everything is dealt into the new areas,
        /// frontmost first, so the windows in use are the ones on show.
        void Rebuild();

        void OnAdded(HWND hwnd);
        void OnRemoved(HWND hwnd);
        void OnStateChanged(HWND hwnd, WindowState previous);
        void OnActivated(HWND hwnd);
        void OnDragStarted(HWND hwnd);
        void OnDragEnded(HWND hwnd);

        Native::Monitor const* MonitorOf(TreeKey const& key) const;
        Native::Monitor const* MonitorAt(Core::Pixel point) const;
        TreeKey KeyFor(HWND hwnd);
        Space SpaceOf(Native::Monitor const& monitor, Layout const& layout) const;

        void ArrangeTree(TreeKey const& key);
        void ArrangeAll();
        void Place(ManagedWindow& window, Placement& placement, Core::Rect const& tile);
        void ArmSettle(int milliseconds);
        void Settle();
        void MoveTo(HWND hwnd, RECT const& rect);

        void MonitorsChanged(std::vector<Native::Monitor> monitors);

        void WatchDrag();
        void FollowResize(RECT const& now);
        void UndoFollowedResize();

        /// The layout under the cursor, on the dragged window's desktop.
        std::optional<TreeKey> TreeAt(HWND dragged, Core::Pixel cursor);
        bool NearGapAt(HWND dragged, Core::Pixel cursor);
        std::optional<Drop> DropAt(HWND dragged, Core::Pixel cursor);
        void ShowGuide(HWND dragged, Core::Pixel cursor);
        void ApplyDrop(HWND dragged, Drop const& drop);
        void ApplyResize(HWND hwnd, Placement& placement, RECT const& now);

        void WatchGap();
        std::optional<Gap> GapAt(Core::Pixel point);
        bool GapIsClear(Gap const& gap, Core::Pixel point) const;
        void DragGap(Core::Pixel cursor);
        void StopWatchingGap();
        void UpdateGapWatch();

        winrt::Windows::System::DispatcherQueue m_dispatcher;
        WindowTracker& m_tracker;
        Core::AppSettings const& m_settings;

        bool m_running{ false };
        bool m_displayOn{ true };
        Core::LayoutPreset m_preset{ Core::LayoutPreset::TwoColumns };

        std::vector<Native::Monitor> m_monitors;
        std::wstring m_signature;

        std::map<TreeKey, Layout> m_layouts;

        /// The layouts of monitors that have gone, kept to restore on their
        /// return: docking should be painless.
        std::map<TreeKey, Layout> m_parked;

        std::map<HWND, Placement> m_placements;

        /// Minimized windows, and the area to return to.
        std::map<HWND, Core::WindowId> m_returning;

        /// Windows the user dropped near a gap: floating, until dropped on an
        /// area again. Kept while arranging is off, as their floating is.
        std::set<HWND> m_loose;

        std::set<HWND> m_settling;
        winrt::Windows::System::DispatcherQueueTimer m_settle{ nullptr };

        std::optional<Dragged> m_drag;
        winrt::Windows::System::DispatcherQueueTimer m_dragWatch{ nullptr };
        DropPreview m_preview;
        DropGuide m_guide;

        std::optional<Gap> m_gap;
        std::optional<Gap> m_gapCandidate;
        winrt::Windows::System::DispatcherQueueTimer m_gapWatch{ nullptr };
        std::unique_ptr<GapHandle> m_gapHandle;
    };
}
