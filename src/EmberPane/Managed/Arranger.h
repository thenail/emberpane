#pragma once

#include <compare>
#include <map>
#include <memory>
#include <set>
#include <string>
#include "LayoutPresets.h"
#include "LayoutState.h"
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

        /// The windows are moving to the areas of a layout just chosen, put
        /// back to its sizes, or just switched on: until they are all there,
        /// a frame drawn round one would be drawn where it no longer is.
        bool Rearranging() const;

        /// They have all arrived.
        Event<> Rearranged;

        /// The layout in use.
        Core::LayoutPreset Preset() const { return m_preset; }

        /// The layout in use changed here, not in the settings: a window was
        /// snapped to a place only another preset has. The setting follows.
        Event<> PresetChosen;

        /// The areas of the layout the panel shows, on the panel's square, when
        /// they have been resized away from the preset's. That is the layout of
        /// the taskbar's monitor, on the desktop being looked at, as the panel
        /// opens over it. None when they have not, or with arranging off.
        std::optional<std::vector<Core::Rect>> ResizedPicture();

        /// That layout's areas back to the preset's. Its windows keep their
        /// areas; only the sizes change.
        void ResetAreas();

        /// Some layout's areas were resized, put back or made again.
        Event<> AreasChanged;

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

            /// Edges the user dragged in from its area where no split could
            /// follow them. It keeps that shape inside the area as long as it
            /// stays in it.
            Core::Insets Insets;

            /// What this window refused to go below, in visible pixels.
            Core::Size Minimum;

            /// The size it last came back at when it was larger than asked, not
            /// yet taken for a minimum: an app restoring its own saved size a
            /// moment late looks the same once. It is asked again, and only a
            /// second refusal at the same size is learned.
            std::optional<Core::Size> Refused;
            int Refusals{ 0 };
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

            /// The mouse button has been down during it. Snap placing a window
            /// from the layouts on its maximize button moves it too, after the
            /// click, and that is not a drag: no guide, and no drop where the
            /// pointer happens to be.
            bool ByHand{ false };

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

        /// Not in a test run; see the .cpp.
        bool Persisting() const;

        /// When Windows started, to tell this boot's handles from another's.
        static std::int64_t BootTime();

        /// To disk, when anything has changed since it was last written.
        void SaveState();

        /// What the last run saved, as arranging starts: each window that is
        /// still there back in its area, and the areas their sizes.
        void Restore(Core::LayoutState const& state);

        Layout& LayoutFor(TreeKey const& key);

        /// The layout the panel shows; see ResizedPicture.
        std::optional<TreeKey> ShownTree();
        Core::WindowId EmptiestArea(Layout const& layout) const;
        size_t CountIn(Layout const& layout, Core::WindowId area) const;

        /// The area a window is already in, as arranging starts: the one
        /// holding most of it, if that is most of the window.
        std::optional<Core::WindowId> AreaHolding(HWND hwnd);

        /// Into the layout of its own monitor, or of `tree` when given.
        void AddToLayout(HWND hwnd, std::optional<Core::WindowId> area, std::optional<TreeKey> tree = std::nullopt);
        void RemoveFromLayout(HWND hwnd);
        void Float(HWND hwnd);

        /// The exe that names a window's app, in lower case; empty when it
        /// cannot be told, as for a UWP frame whose app has not shown yet.
        std::wstring AppOf(HWND hwnd) const;

        /// Where the user last dropped a window of this app, under this preset.
        std::optional<Core::WindowId> RememberedArea(HWND hwnd) const;
        void RememberArea(HWND hwnd, Core::WindowId area);

        /// Into the layout of the desktop it is on, if that is known now and
        /// is not the one it is in.
        void FollowDesktop(HWND hwnd);

        /// Dropped near a gap: out of the layout, where the user let it go.
        void Loosen(HWND hwnd);

        /// Dropped on an area again, after Loosen.
        void Rejoin(HWND hwnd, Drop const& drop);

        /// A window that goes to a given area whatever else is dealt.
        struct Pinned
        {
            HWND Hwnd{};
            TreeKey Tree;
            Core::WindowId Area{ 0 };
        };

        /// The preset changed: everything is dealt into the new areas,
        /// frontmost first, so the windows in use are the ones on show.
        void Rebuild(std::vector<Pinned> const& pinned = {});

        /// Windows' Snap put windows somewhere: each into the area that is
        /// that place, changing preset if only another one has them. `hint` is
        /// the window known to be snapped; every other window freshly snapped
        /// is taken with it, since Snap places several at once for the layouts
        /// it suggests, and arranging one layout would pull the others out of
        /// their places before they were read.
        void TakeSnaps(std::vector<HWND> hint);

        /// Windows of the layout marked as snapped and away from where they
        /// were put.
        std::vector<HWND> FreshSnaps();

        /// After a move not made by hand: Snap marks the window as snapped as
        /// the move ends, or a moment later. Looked at twice, then put back.
        void WatchForSnap(HWND hwnd);
        void CheckForSnap(bool last);

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

        /// After a whole layout is placed: Rearranging until Settle has read
        /// every window back.
        void BeginRearranging();
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
        /// Moves the split behind each edge that moved. Returns the edges that
        /// moved with no split behind them.
        std::vector<Core::Edge> ApplyResize(HWND hwnd, Placement& placement, RECT const& now);

        /// The window keeps those edges where the user left them, inside its
        /// area. One left close to the area's own edge goes back to it.
        void KeepInsets(HWND hwnd, Placement& placement, std::vector<Core::Edge> const& edges);

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

        /// Until then, a window found moved was moved by Windows, as the
        /// displays changed or woke, and does not count against its app.
        ULONGLONG m_displaysSettleAt{ 0 };
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

        /// The area each app's windows were last dropped in, per preset: a
        /// window it opens later goes there. By exe name, in lower case.
        std::map<std::pair<std::wstring, Core::LayoutPreset>, Core::WindowId> m_appAreas;

        /// Windows the user dropped near a gap: floating, until dropped on an
        /// area again. Kept while arranging is off, as their floating is.
        std::set<HWND> m_loose;

        std::set<HWND> m_settling;
        bool m_rearranging{ false };
        ULONGLONG m_rearrangingSince{ 0 };

        /// What the last run left, until the first start has used it.
        std::optional<Core::LayoutState> m_saved;

        /// As last written, so that an unchanged state is not written again.
        std::wstring m_savedText;

        std::int64_t m_boot{ BootTime() };
        winrt::Windows::System::DispatcherQueueTimer m_settle{ nullptr };

        std::optional<Dragged> m_drag;
        winrt::Windows::System::DispatcherQueueTimer m_dragWatch{ nullptr };

        /// Windows moved but not by hand, until Snap's mark has been looked for.
        std::set<HWND> m_maybeSnapped;
        winrt::Windows::System::DispatcherQueueTimer m_snapWatch{ nullptr };
        bool m_snapWatchLast{ false };
        DropPreview m_preview;
        DropGuide m_guide;

        std::optional<Gap> m_gap;
        std::optional<Gap> m_gapCandidate;
        winrt::Windows::System::DispatcherQueueTimer m_gapWatch{ nullptr };
        std::unique_ptr<GapHandle> m_gapHandle;
    };
}
