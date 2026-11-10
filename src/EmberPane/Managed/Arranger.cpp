#include "pch.h"
#include "Managed/Arranger.h"
#include "CommandLine.h"
#include "Native/ProcessTree.h"
#include "Native/WindowDocking.h"
#include "Native/WindowFacts.h"
#include "Trace.h"

using namespace std::chrono_literals;

namespace EmberPane::Managed
{
    namespace
    {
        /// DPI-virtualized windows round: one asked for height 460 settled at
        /// 461. Read-back has to allow for it, or a minimum would be learned
        /// from nothing and windows would be chased for a pixel.
        constexpr int TolerancePx = 2;

        constexpr int SettleMs = 200;

        /// Many apps restore their saved geometry just after showing.
        constexpr int FreshSettleMs = 800;

        constexpr int MaxRetries = 2;
        constexpr int MaxUnmoved = 2;

        /// Past this many corrections within the window, stop fighting.
        constexpr int MaxCorrections = 3;
        constexpr ULONGLONG CorrectionWindowMs = 10'000;

        /// After the displays change or wake, while Windows moves windows
        /// itself. A wake has reported monitor changes for 18 s.
        constexpr ULONGLONG DisplaySettleMs = 30'000;

        /// Past this many tries, an app that keeps its snapped mark keeps it.
        constexpr int MaxUnsnaps = 3;

        constexpr int MinimumTileDip = 120;

        /// An edge let go this close to its area's goes back to it, so that a
        /// window can be given its whole area again by hand.
        constexpr int RejoinEdgeDip = 16;

        /// How far into each neighbouring area a gap's drop zone reaches, as a
        /// share of that area's length across the gap: a third.
        constexpr double LooseShare = 1.0 / 3;

        int DipsToPixels(int dips, UINT dpi) { return MulDiv(dips, dpi, 96); }

        Core::Rect ToRect(RECT const& r) { return { r.left, r.top, r.right, r.bottom }; }

        bool Near(int a, int b) { return std::abs(a - b) <= TolerancePx; }

        bool Near(RECT const& a, RECT const& b)
        {
            return Near(a.left, b.left) && Near(a.top, b.top) && Near(a.right, b.right) && Near(a.bottom, b.bottom);
        }

        RECT VisibleFrame(HWND hwnd)
        {
            RECT frame{};
            if (FAILED(DwmGetWindowAttribute(hwnd, DWMWA_EXTENDED_FRAME_BOUNDS, &frame, sizeof(frame))))
                GetWindowRect(hwnd, &frame);
            return frame;
        }

        UINT DpiOf(HWND hwnd)
        {
            UINT dpiX = 96, dpiY = 96;
            GetDpiForMonitor(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), MDT_EFFECTIVE_DPI, &dpiX, &dpiY);
            return dpiX;
        }

        std::vector<HWND> FrontToBack()
        {
            std::vector<HWND> windows;
            EnumWindows([](HWND hwnd, LPARAM list)
            {
                reinterpret_cast<std::vector<HWND>*>(list)->push_back(hwnd);
                return TRUE;
            }, reinterpret_cast<LPARAM>(&windows));
            return windows;
        }
    }

    Arranger::Arranger(winrt::Windows::System::DispatcherQueue const& dispatcher, WindowTracker& tracker,
                       Core::AppSettings const& settings)
        : m_dispatcher(dispatcher), m_tracker(tracker), m_settings(settings)
    {
        m_monitors = Native::ListMonitors();
        m_signature = Native::SignatureOf(m_monitors);

        m_tracker.Added.Add([this](HWND hwnd) { OnAdded(hwnd); });
        m_tracker.Removed.Add([this](HWND hwnd) { OnRemoved(hwnd); });
        m_tracker.StateChanged.Add([this](HWND hwnd, WindowState previous) { OnStateChanged(hwnd, previous); });
        m_tracker.Activated.Add([this](HWND hwnd) { OnActivated(hwnd); });
        m_tracker.DragStarted.Add([this](HWND hwnd) { OnDragStarted(hwnd); });
        m_tracker.DragEnded.Add([this](HWND hwnd) { OnDragEnded(hwnd); });

        m_settle = m_dispatcher.CreateTimer();
        m_settle.IsRepeating(false);
        m_settle.Tick([this](auto&&, auto&&) { Settle(); });

        m_dragWatch = m_dispatcher.CreateTimer();
        m_dragWatch.Interval(30ms);
        m_dragWatch.Tick([this](auto&&, auto&&) { WatchDrag(); });

        m_gapWatch = m_dispatcher.CreateTimer();
        m_gapWatch.Interval(100ms);
        m_gapWatch.Tick([this](auto&&, auto&&) { WatchGap(); });

        GapHandle::Handlers handlers;
        handlers.Dragged = [this](Core::Pixel cursor) { DragGap(cursor); };
        handlers.Finished = [this]
        {
            if (m_gap) ArrangeTree(m_gap->Tree);
            m_gapHandle->Hide();
            m_gap.reset();
            AreasChanged();
        };
        m_gapHandle = std::make_unique<GapHandle>(std::move(handlers));

        // What the last run left, read once: only this first start may take
        // it. Arranging turned on later starts from where the windows are,
        // since they were free to go anywhere while it was off.
        if (Persisting())
        {
            if (auto root = Core::Json::ReadFile(Core::LayoutState::DefaultPath()))
                m_saved = Core::LayoutState::FromJson(*root);

            // Whatever boot it is from: these name apps, not windows.
            if (m_saved)
                for (auto const& app : m_saved->Apps)
                    m_appAreas[{ app.App, app.Preset }] = app.Area;
        }

        if (Arranging())
            Start();
        else
            ReleaseSnap();

        m_saved.reset();
    }

    Arranger::~Arranger()
    {
        // A quit: the next run puts every window back.
        SaveState();
        ReleaseSnap();

        m_settle.Stop();
        m_dragWatch.Stop();
        m_gapWatch.Stop();
    }

    bool Arranger::Arranging() const
    {
        return m_settings.Arrange.Enabled && !CommandLine::Has(L"--no-arrange");
    }

    // ------------------------------------------------------------ on and off

    void Arranger::HoldSnap()
    {
        if (!Native::WindowDocking::IsOn()) return;

        bool held = Native::WindowDocking::Set(false);
        Trace::Log(held ? L"snap: off while arranging" : L"snap: could not be turned off");
    }

    void Arranger::ReleaseSnap()
    {
        // To what the user chose, not what it was: after a crash, what it
        // was is what the last run left.
        bool wanted = Native::WindowDocking::Preference();
        if (Native::WindowDocking::IsOn() == wanted) return;

        bool restored = Native::WindowDocking::Set(wanted);
        Trace::Log(restored ? L"snap: back as it was" : L"snap: could not be put back");
    }

    void Arranger::Start()
    {
        m_running = true;
        m_preset = m_settings.Arrange.Layout;
        HoldSnap();

        // Each window in the area the last run had it in, with the areas the
        // size they were.
        if (m_saved)
            Restore(*m_saved);

        // The rest in the area they are already in, if they are mostly in
        // one; the others frontmost first, so the windows in use are the ones
        // on show.
        for (auto hwnd : FrontToBack())
            AddToLayout(hwnd, AreaHolding(hwnd));

        Trace::Log(L"arranging: " + std::to_wstring(m_placements.size()) + L" windows in " +
                   std::to_wstring(m_layouts.size()) + L" layouts");

        ArrangeAll();
        BeginRearranging();
        UpdateGapWatch();
        AreasChanged();
    }

    void Arranger::Stop()
    {
        // Every window stays where it is. Windows goes on treating them as
        // normal windows.
        m_running = false;
        ReleaseSnap();

        // Free now, the windows may go anywhere: nothing is put back.
        if (Persisting())
            DeleteFileW(Core::LayoutState::DefaultPath().c_str());
        m_savedText.clear();
        m_rearranging = false;

        m_layouts.clear();
        m_parked.clear();
        m_placements.clear();
        m_returning.clear();
        m_settling.clear();

        m_preview.Hide();
        m_guide.Hide();
        StopWatchingGap();
        AreasChanged();
    }

    void Arranger::SettingsChanged()
    {
        if (Arranging() != m_running)
        {
            if (m_running) Stop();
            else Start();
            return;
        }

        if (!m_running) return;

        if (m_settings.Arrange.Layout != m_preset)
        {
            m_preset = m_settings.Arrange.Layout;
            Rebuild();
            return;
        }

        // A rule may have just let a window in, or sent one away.
        for (auto hwnd : FrontToBack())
            AddToLayout(hwnd, std::nullopt);

        std::vector<HWND> gone;
        for (auto const& [hwnd, placement] : m_placements)
        {
            auto window = m_tracker.Windows().find(hwnd);
            if (window == m_tracker.Windows().end() || window->second.Disposition != Core::Disposition::Tile)
                gone.push_back(hwnd);
        }
        for (auto hwnd : gone)
            RemoveFromLayout(hwnd);

        ArrangeAll();
        UpdateGapWatch();
    }

    void Arranger::Rebuild()
    {
        m_layouts.clear();
        m_parked.clear();
        m_returning.clear();

        for (auto hwnd : FrontToBack())
        {
            auto placement = m_placements.find(hwnd);
            if (placement == m_placements.end()) continue;

            auto& layout = LayoutFor(placement->second.Tree);
            layout.Members[hwnd] = EmptiestArea(layout);
        }

        Trace::Log(L"layout: " + std::to_wstring(static_cast<int>(m_preset)));

        ArrangeAll();
        BeginRearranging();
        UpdateGapWatch();
        AreasChanged();
    }

    // ------------------------------------------------------------ layouts

    Arranger::Layout& Arranger::LayoutFor(TreeKey const& key)
    {
        auto found = m_layouts.find(key);
        if (found != m_layouts.end()) return found->second;

        Layout layout;
        layout.Areas = Core::MakeLayout(m_preset);
        return m_layouts.emplace(key, std::move(layout)).first->second;
    }

    std::optional<TreeKey> Arranger::ShownTree()
    {
        if (!m_running) return std::nullopt;

        // The panel opens over the taskbar, so its monitor is the one meant.
        HWND taskbar = FindWindowW(L"Shell_TrayWnd", nullptr);
        HMONITOR handle = taskbar ? MonitorFromWindow(taskbar, MONITOR_DEFAULTTOPRIMARY)
                                  : MonitorFromPoint({ 0, 0 }, MONITOR_DEFAULTTOPRIMARY);

        std::wstring monitor;
        for (auto const& candidate : m_monitors)
            if (candidate.Handle == handle) monitor = candidate.Key;
        if (monitor.empty()) return std::nullopt;

        // A layout's desktop is told by its windows. One with none left can
        // only be told when it is the monitor's only layout.
        std::optional<TreeKey> empty;
        int count = 0;
        for (auto const& [key, layout] : m_layouts)
        {
            if (key.Monitor != monitor) continue;
            ++count;

            if (layout.Members.empty())
                empty = key;
            else if (m_tracker.IsOnCurrentDesktop(layout.Members.begin()->first))
                return key;
        }

        return count == 1 ? empty : std::nullopt;
    }

    std::optional<std::vector<Core::Rect>> Arranger::ResizedPicture()
    {
        auto key = ShownTree();
        if (!key) return std::nullopt;

        auto layout = m_layouts.find(*key);
        if (layout == m_layouts.end()) return std::nullopt;

        // A window made smaller than its area counts too: the reset is one
        // way to give it the whole of it back.
        bool inset = std::ranges::any_of(layout->second.Members, [&](auto const& member)
        {
            auto placement = m_placements.find(member.first);
            return placement != m_placements.end() && !placement->second.Insets.Empty();
        });
        if (!inset && !Core::IsResized(layout->second.Areas, m_preset)) return std::nullopt;

        return Core::PictureOf(layout->second.Areas);
    }

    void Arranger::ResetAreas()
    {
        auto key = ShownTree();
        if (!key) return;

        auto layout = m_layouts.find(*key);
        if (layout == m_layouts.end()) return;

        Trace::Log(L"layout: areas reset");

        // The same preset numbers its areas the same, so every window keeps
        // the one it is in.
        layout->second.Areas = Core::MakeLayout(m_preset);
        for (auto const& [hwnd, area] : layout->second.Members)
            if (auto placement = m_placements.find(hwnd); placement != m_placements.end())
                placement->second.Insets = {};

        ArrangeTree(*key);
        BeginRearranging();
        AreasChanged();
    }

    std::optional<Core::WindowId> Arranger::AreaHolding(HWND hwnd)
    {
        auto window = m_tracker.Windows().find(hwnd);
        if (window == m_tracker.Windows().end() || window->second.Disposition != Core::Disposition::Tile ||
            window->second.State != WindowState::Normal)
            return std::nullopt;

        auto key = KeyFor(hwnd);
        auto monitor = MonitorOf(key);
        if (!monitor) return std::nullopt;

        RECT frame = VisibleFrame(hwnd);
        long long size = static_cast<long long>(frame.right - frame.left) * (frame.bottom - frame.top);
        if (size <= 0) return std::nullopt;

        auto& layout = LayoutFor(key);
        auto space = SpaceOf(*monitor, layout);

        std::optional<Core::WindowId> best;
        long long most = 0;
        for (auto const& tile : layout.Areas.Arrange(space.Bounds, space.Gap, space.Minimums))
        {
            long long width = std::min<long>(frame.right, tile.Bounds.Right) - std::max<long>(frame.left, tile.Bounds.Left);
            long long height = std::min<long>(frame.bottom, tile.Bounds.Bottom) - std::max<long>(frame.top, tile.Bounds.Top);
            if (width <= 0 || height <= 0 || width * height <= most) continue;

            most = width * height;
            best = tile.Id;
        }

        // Most of it, so an area dragged wider in a run before, and now back
        // to the preset's size, still holds its window. A window across the
        // split, or covering the screen, is in none.
        return most * 10 >= size * 6 ? best : std::nullopt;
    }

    size_t Arranger::CountIn(Layout const& layout, Core::WindowId area) const
    {
        size_t count = 0;
        for (auto const& [hwnd, member] : layout.Members)
            if (member == area) ++count;
        return count;
    }

    Core::WindowId Arranger::EmptiestArea(Layout const& layout) const
    {
        Core::WindowId emptiest = 1;
        size_t fewest = SIZE_MAX;

        for (auto area : layout.Areas.Windows())
        {
            auto count = CountIn(layout, area);
            if (count < fewest)
            {
                fewest = count;
                emptiest = area;
            }
        }

        return emptiest;
    }

    void Arranger::AddToLayout(HWND hwnd, std::optional<Core::WindowId> area, std::optional<TreeKey> tree)
    {
        auto window = m_tracker.Windows().find(hwnd);
        if (window == m_tracker.Windows().end()) return;

        auto& managed = window->second;
        if (managed.Disposition != Core::Disposition::Tile || managed.Immovable ||
            managed.State == WindowState::Minimized || m_placements.contains(hwnd))
            return;

        auto key = tree ? *tree : KeyFor(hwnd);
        if (!MonitorOf(key)) return;

        auto& layout = LayoutFor(key);

        // With nothing better to go by, where the user last put this app.
        if (!area)
            area = RememberedArea(hwnd);
        layout.Members[hwnd] = area && layout.Areas.Contains(*area) ? *area : EmptiestArea(layout);

        Placement placement;
        placement.Tree = key;
        GetWindowRect(hwnd, &placement.Settled);
        m_placements[hwnd] = placement;
    }

    void Arranger::RemoveFromLayout(HWND hwnd)
    {
        auto placement = m_placements.find(hwnd);
        if (placement == m_placements.end()) return;

        auto key = placement->second.Tree;
        m_placements.erase(placement);
        m_settling.erase(hwnd);

        // The area it leaves stays as it is, even with nothing in it: no window
        // is moved from one area to another but by the user.
        if (auto layout = m_layouts.find(key); layout != m_layouts.end())
        {
            layout->second.Members.erase(hwnd);
            ArrangeTree(key);
        }

        UpdateGapWatch();
    }

    void Arranger::Float(HWND hwnd)
    {
        m_tracker.SetDisposition(hwnd, Core::Disposition::Float);
        RemoveFromLayout(hwnd);
    }

    std::wstring Arranger::AppOf(HWND hwnd) const
    {
        auto window = m_tracker.Windows().find(hwnd);
        if (window == m_tracker.Windows().end()) return {};

        auto name = window->second.ExeName;

        // Every UWP app's frame belongs to ApplicationFrameHost; the app is
        // the process behind it, once known.
        if (_wcsicmp(name.c_str(), L"ApplicationFrameHost.exe") == 0)
        {
            auto path = Native::ProcessTree::ExePath(window->second.ProcessId);
            name = path.substr(path.find_last_of(L'\\') + 1);
            if (_wcsicmp(name.c_str(), L"ApplicationFrameHost.exe") == 0) return {};
        }

        for (auto& c : name) c = static_cast<wchar_t>(towlower(c));
        return name;
    }

    std::optional<Core::WindowId> Arranger::RememberedArea(HWND hwnd) const
    {
        auto app = AppOf(hwnd);
        if (app.empty()) return std::nullopt;

        auto found = m_appAreas.find({ app, m_preset });
        if (found == m_appAreas.end()) return std::nullopt;
        return found->second;
    }

    void Arranger::RememberArea(HWND hwnd, Core::WindowId area)
    {
        auto app = AppOf(hwnd);
        if (app.empty()) return;

        m_appAreas[{ app, m_preset }] = area;
    }

    void Arranger::Loosen(HWND hwnd)
    {
        Trace::Log(L"drag: dropped near a gap - out of the layout");
        m_loose.insert(hwnd);
        Float(hwnd);
    }

    void Arranger::Rejoin(HWND hwnd, Drop const& drop)
    {
        Trace::Log(L"drag: dropped on an area - back in the layout");
        m_loose.erase(hwnd);
        m_tracker.SetDisposition(hwnd, Core::Disposition::Tile);

        AddToLayout(hwnd, drop.Area, drop.Tree);
        RememberArea(hwnd, drop.Area);
        if (m_placements.contains(hwnd))
            ArrangeTree(drop.Tree);
        UpdateGapWatch();
    }

    Native::Monitor const* Arranger::MonitorOf(TreeKey const& key) const
    {
        for (auto const& monitor : m_monitors)
            if (monitor.Key == key.Monitor) return &monitor;
        return nullptr;
    }

    Native::Monitor const* Arranger::MonitorAt(Core::Pixel point) const
    {
        HMONITOR handle = MonitorFromPoint({ point.X, point.Y }, MONITOR_DEFAULTTONEAREST);
        for (auto const& monitor : m_monitors)
            if (monitor.Handle == handle) return &monitor;
        return m_monitors.empty() ? nullptr : &m_monitors.front();
    }

    TreeKey Arranger::KeyFor(HWND hwnd)
    {
        HMONITOR handle = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);

        TreeKey key;
        for (auto const& monitor : m_monitors)
            if (monitor.Handle == handle) key.Monitor = monitor.Key;
        if (key.Monitor.empty() && !m_monitors.empty())
            key.Monitor = m_monitors.front().Key;

        key.Desktop = m_tracker.DesktopOf(hwnd);
        return key;
    }

    Arranger::Space Arranger::SpaceOf(Native::Monitor const& monitor, Layout const& layout) const
    {
        int margin = DipsToPixels(m_settings.Arrange.MarginDip, monitor.Dpi);
        int minimum = DipsToPixels(MinimumTileDip, monitor.Dpi);

        Space space;
        space.Bounds = { monitor.Work.left + margin, monitor.Work.top + margin, monitor.Work.right - margin,
                         monitor.Work.bottom - margin };
        space.Gap = DipsToPixels(m_settings.Arrange.GapDip, monitor.Dpi);
        space.Minimums.Default = { minimum, minimum };

        // An area must take the largest window in it.
        for (auto const& [hwnd, area] : layout.Members)
        {
            auto placement = m_placements.find(hwnd);
            if (placement == m_placements.end() || placement->second.Minimum.Width <= 0) continue;

            auto& learned = space.Minimums.Learned[area];
            learned.Width = std::max(learned.Width, placement->second.Minimum.Width);
            learned.Height = std::max(learned.Height, placement->second.Minimum.Height);
        }

        return space;
    }

    void Arranger::ArrangeAll()
    {
        std::vector<TreeKey> keys;
        for (auto const& [key, layout] : m_layouts)
            keys.push_back(key);

        for (auto const& key : keys)
            ArrangeTree(key);
    }

    void Arranger::ArrangeTree(TreeKey const& key)
    {
        auto layout = m_layouts.find(key);
        auto monitor = MonitorOf(key);
        if (layout == m_layouts.end() || !monitor) return;

        auto space = SpaceOf(*monitor, layout->second);

        for (auto const& tile : layout->second.Areas.Arrange(space.Bounds, space.Gap, space.Minimums))
        {
            for (auto const& [hwnd, area] : layout->second.Members)
            {
                if (area != tile.Id) continue;

                // The one in the user's hand goes where they take it.
                if (m_drag && m_drag->Hwnd == hwnd) continue;

                auto window = m_tracker.Windows().find(hwnd);
                auto placement = m_placements.find(hwnd);
                if (window == m_tracker.Windows().end() || placement == m_placements.end()) continue;

                Place(window->second, placement->second,
                      Core::InsetTile(tile.Bounds, placement->second.Insets, space.Minimums.Default));
            }
        }
    }

    void Arranger::Place(ManagedWindow& window, Placement& placement, Core::Rect const& tile)
    {
        // A maximized or fullscreen window keeps its area and is not moved.
        if (window.State != WindowState::Normal) return;

        RECT now{};
        if (!GetWindowRect(window.Hwnd, &now)) return;

        // The tile is the visible frame; the rectangle Windows wants is larger
        // by the invisible resize border, which differs per window and DPI.
        RECT frame = VisibleFrame(window.Hwnd);
        RECT asked{ tile.Left - (frame.left - now.left), tile.Top - (frame.top - now.top),
                    tile.Right + (now.right - frame.right), tile.Bottom + (now.bottom - frame.bottom) };

        placement.Asked = asked;
        placement.AskedDpi = DpiOf(window.Hwnd);

        // Windows never rounds a window it thinks is snapped, and moving one
        // does not clear the mark, which also makes it jump to its old size
        // when dragged. Restoring does. It goes back to its old place for a
        // moment, and the move right behind it puts it here.
        bool snapped = placement.Unsnaps < MaxUnsnaps && IsWindowArranged(window.Hwnd);

        if (EqualRect(&now, &asked) && !snapped)
        {
            placement.Settled = now;
            return;
        }

        if (snapped)
        {
            ++placement.Unsnaps;
            ShowWindowAsync(window.Hwnd, SW_RESTORE);
        }

        placement.Before = now;
        MoveTo(window.Hwnd, asked);
        m_settling.insert(window.Hwnd);
        ArmSettle(SettleMs);
    }

    void Arranger::MoveTo(HWND hwnd, RECT const& rect)
    {
        // Never waits on the window: a hung app must not freeze the tray or
        // stop the others being arranged. For the same reason, not
        // BeginDeferWindowPos, where one bad window can hold up the batch.
        SetWindowPos(hwnd, nullptr, rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top,
                     SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_ASYNCWINDOWPOS);
        Moved();
    }

    std::vector<ShownWindow> Arranger::OnShow()
    {
        std::vector<ShownWindow> shown;
        if (!m_running) return shown;

        auto order = FrontToBack();

        for (auto const& [key, layout] : m_layouts)
        {
            auto monitor = MonitorOf(key);
            if (!monitor || layout.Members.empty()) continue;

            // The layout on the desktop being looked at.
            if (!m_tracker.IsOnCurrentDesktop(layout.Members.begin()->first)) continue;

            // Never the whole monitor: Windows takes that for a fullscreen app,
            // even with nothing to see, and lowers the taskbar beneath the
            // windows, where their shadows fall on it. With an auto-hiding
            // taskbar the work area is the whole monitor.
            auto bounds = ToRect(monitor->Work);
            if (EqualRect(&monitor->Work, &monitor->Bounds))
                --bounds.Bottom;

            std::set<Core::WindowId> taken;
            for (auto hwnd : order)
            {
                auto member = layout.Members.find(hwnd);
                if (member == layout.Members.end()) continue;

                auto window = m_tracker.Windows().find(hwnd);
                if (window == m_tracker.Windows().end()) continue;

                if (m_drag && m_drag->Hwnd == hwnd && m_drag->Moving) continue;

                bool behind = !taken.insert(member->second).second;

                // In front of its area and maximized: it has no frame, and
                // nothing behind it shows.
                if (!behind && window->second.State != WindowState::Normal) continue;

                shown.push_back({ hwnd, key.Monitor, bounds, monitor->Dpi,
                                  DipsToPixels(m_settings.Arrange.GapDip, monitor->Dpi), behind,
                                  m_settling.contains(hwnd) });
            }
        }

        return shown;
    }

    bool Arranger::Holding() const
    {
        return m_drag.has_value() || (m_gapHandle && m_gapHandle->Dragging());
    }

    HWND Arranger::InHand() const
    {
        if (!m_drag || !m_drag->Moving) return nullptr;
        if (HWND preview = m_preview.Hwnd()) return preview;
        if (HWND guide = m_guide.Hwnd()) return guide;
        return m_drag->Hwnd;
    }

    bool Arranger::Resizing() const
    {
        return (m_drag && m_drag->AreasBefore) || (m_gapHandle && m_gapHandle->Dragging());
    }

    void Arranger::ArmSettle(int milliseconds)
    {
        m_settle.Stop();
        m_settle.Interval(std::chrono::milliseconds(milliseconds));
        m_settle.Start();
    }

    void Arranger::Settle()
    {
        struct Timed
        {
            double Started = Trace::NowMs();
            ~Timed()
            {
                if (double took = Trace::NowMs() - Started; took > 30)
                    Trace::Log(L"arrange: settled in " + std::to_wstring(std::lround(took)) + L" ms");
            }
        } timed;

        // While a window or the gap is held, the layout moves under the
        // windows several times a second, and an app a few requests behind
        // would read as one that refuses to move. They are judged once it is
        // let go, against where they were last asked to be.
        if (Holding())
        {
            ArmSettle(SettleMs);
            return;
        }

        auto settling = std::exchange(m_settling, {});
        std::set<TreeKey> rearrange;

        for (auto hwnd : settling)
        {
            auto window = m_tracker.Windows().find(hwnd);
            auto found = m_placements.find(hwnd);
            if (window == m_tracker.Windows().end() || found == m_placements.end()) continue;

            auto& placement = found->second;
            if (window->second.State != WindowState::Normal) continue;

            RECT now{};
            if (!GetWindowRect(hwnd, &now)) continue;

            auto const& asked = placement.Asked;
            int widthOver = (now.right - now.left) - (asked.right - asked.left);
            int heightOver = (now.bottom - now.top) - (asked.bottom - asked.top);

            if (Near(now, asked))
            {
                placement.Settled = now;
                placement.Unmoved = 0;
                placement.Retries = 0;
                placement.Refused.reset();
                placement.Refusals = 0;
            }
            else if (Near(now, placement.Before))
            {
                // Nothing happened. A window of an elevated app cannot be
                // moved by this process, and SWP_ASYNCWINDOWPOS may report
                // success for a request that is dropped, so this is found by
                // reading back rather than by predicting it.
                if (++placement.Unmoved >= MaxUnmoved)
                {
                    Trace::Log(L"arrange: " + window->second.ExeName + L" cannot be moved");
                    window->second.Immovable = true;
                    rearrange.insert(placement.Tree);
                    m_dispatcher.TryEnqueue([this, hwnd] { Float(hwnd); });
                    continue;
                }
                MoveTo(hwnd, asked);
                m_settling.insert(hwnd);
            }
            else if (DpiOf(hwnd) != placement.AskedDpi)
            {
                // The monitor's scale changed under it, as it can for a moment
                // while a display wakes, and Windows rescaled it. Its size says
                // nothing about its minimum.
                if (placement.Retries++ < MaxRetries)
                    rearrange.insert(placement.Tree);
                else
                    placement.Settled = now;
            }
            else if (widthOver > TolerancePx || heightOver > TolerancePx)
            {
                // Larger than asked: it has a minimum size. WM_GETMINMAXINFO
                // cannot be asked across processes, so this is how it is
                // learned.
                RECT frame = VisibleFrame(hwnd);
                Core::Size refused{ frame.right - frame.left, frame.bottom - frame.top };

                // Once is not proof: an app restoring its saved size late, or
                // one still coming back from a snap, is larger than asked too,
                // and a minimum learned from that holds its area wide for as
                // long as it is in it. Asked again; a real minimum refuses the
                // same way. Bounded, for an app whose size keeps changing.
                bool again = placement.Refused && Near(placement.Refused->Width, refused.Width) &&
                             Near(placement.Refused->Height, refused.Height);
                if (!again && placement.Refusals++ < MaxRetries)
                {
                    placement.Refused = refused;
                    MoveTo(hwnd, asked);
                    m_settling.insert(hwnd);
                    continue;
                }

                Trace::Log(L"arrange: " + window->second.ExeName + L" refuses below " +
                           std::to_wstring(refused.Width) + L"x" + std::to_wstring(refused.Height));
                placement.Refused.reset();
                placement.Refusals = 0;
                placement.Minimum = { std::max(placement.Minimum.Width, refused.Width),
                                      std::max(placement.Minimum.Height, refused.Height) };
                placement.Settled = now;
                rearrange.insert(placement.Tree);
            }
            else if ((now.right - now.left) < (asked.right - asked.left) - TolerancePx ||
                     (now.bottom - now.top) < (asked.bottom - asked.top) - TolerancePx)
            {
                // Smaller: a maximum size or a character grid, as in terminals.
                placement.Settled = now;
            }
            else if (placement.Retries++ < MaxRetries)
            {
                // It rescaled on the way, moving to a monitor of another DPI.
                rearrange.insert(placement.Tree);
            }
            else
            {
                placement.Settled = now;
            }

            // Once more, for the apps that restore their geometry late.
            if (placement.Fresh)
            {
                placement.Fresh = false;
                m_settling.insert(hwnd);
            }
        }

        for (auto const& key : rearrange)
            ArrangeTree(key);

        if (!m_settling.empty())
        {
            ArmSettle(FreshSettleMs);
            return;
        }

        // Every window moved by the change of layout is where it was asked
        // to be, or as near as it will go: the frames can be drawn again.
        if (m_rearranging)
        {
            m_rearranging = false;
            Rearranged();
        }
    }

    void Arranger::BeginRearranging()
    {
        // Nothing moved, nothing to wait for.
        if (m_settling.empty()) return;

        m_rearranging = true;
        m_rearrangingSince = GetTickCount64();
    }

    bool Arranger::Rearranging() const
    {
        // However the windows answer, not longer than this: an app slow to
        // say where it is must not cost every window its frame for good.
        constexpr ULONGLONG LongestMs = 3000;
        return m_rearranging && GetTickCount64() - m_rearrangingSince < LongestMs;
    }

    // ------------------------------------------------------------ what the tracker sees

    void Arranger::OnAdded(HWND hwnd)
    {
        if (!m_running) return;

        AddToLayout(hwnd, std::nullopt);
        if (auto found = m_placements.find(hwnd); found != m_placements.end())
            ArrangeTree(found->second.Tree);

        UpdateGapWatch();
    }

    void Arranger::OnRemoved(HWND hwnd)
    {
        // Gone in the user's hand: the end of its drag would name a window no
        // longer known, and the drag would never end.
        if (m_drag && m_drag->Hwnd == hwnd)
        {
            m_dragWatch.Stop();
            m_preview.Hide();
            m_guide.Hide();
            m_drag.reset();
        }

        m_loose.erase(hwnd);
        if (!m_running) return;

        m_returning.erase(hwnd);
        RemoveFromLayout(hwnd);
    }

    void Arranger::OnStateChanged(HWND hwnd, WindowState previous)
    {
        if (!m_running) return;

        auto window = m_tracker.Windows().find(hwnd);
        if (window == m_tracker.Windows().end()) return;

        auto state = window->second.State;

        if (state == WindowState::Minimized)
        {
            auto found = m_placements.find(hwnd);
            if (found == m_placements.end()) return;

            // Remembered, to return to the same part of the screen.
            if (auto layout = m_layouts.find(found->second.Tree); layout != m_layouts.end())
                if (auto member = layout->second.Members.find(hwnd); member != layout->second.Members.end())
                    m_returning[hwnd] = member->second;

            RemoveFromLayout(hwnd);
            return;
        }

        if (previous == WindowState::Minimized)
        {
            std::optional<Core::WindowId> area;
            if (auto hint = m_returning.find(hwnd); hint != m_returning.end())
            {
                area = hint->second;
                m_returning.erase(hint);
            }

            AddToLayout(hwnd, area);
            if (auto found = m_placements.find(hwnd); found != m_placements.end())
                ArrangeTree(found->second.Tree);
            UpdateGapWatch();
            return;
        }

        // Back from maximized or fullscreen: to its area.
        if (state == WindowState::Normal)
            if (auto found = m_placements.find(hwnd); found != m_placements.end())
                ArrangeTree(found->second.Tree);
    }

    void Arranger::OnActivated(HWND hwnd)
    {
        if (!m_running) return;

        // There is no event for a window moved to another desktop.
        FollowDesktop(hwnd);
    }

    void Arranger::FollowDesktop(HWND hwnd)
    {
        auto found = m_placements.find(hwnd);
        if (found == m_placements.end()) return;

        auto desktop = m_tracker.DesktopOf(hwnd);
        if (desktop.empty() || desktop == found->second.Tree.Desktop) return;

        auto monitor = found->second.Tree.Monitor;
        RemoveFromLayout(hwnd);

        TreeKey key{ monitor, desktop };
        auto& layout = LayoutFor(key);
        layout.Members[hwnd] = EmptiestArea(layout);

        Placement placement;
        placement.Tree = key;
        GetWindowRect(hwnd, &placement.Settled);
        m_placements[hwnd] = placement;

        ArrangeTree(key);
        UpdateGapWatch();
    }

    // ------------------------------------------------------------ the reconcile tick

    void Arranger::Reconcile()
    {
        auto monitors = Native::ListMonitors();
        auto signature = Native::SignatureOf(monitors);
        if (signature != m_signature)
        {
            m_signature = signature;
            MonitorsChanged(std::move(monitors));
        }

        if (!m_running) return;

        // Arranged before Windows would say which desktop it is on, or put back
        // so by a run before this one: until it is told, it sits in a layout
        // of its own, over the windows of the desktop it is really on.
        std::vector<HWND> unplaced;
        for (auto const& [hwnd, placement] : m_placements)
            if (placement.Tree.Desktop.empty() && !(m_drag && m_drag->Hwnd == hwnd))
                unplaced.push_back(hwnd);
        for (auto hwnd : unplaced)
            FollowDesktop(hwnd);

        std::vector<HWND> stubborn;
        std::set<TreeKey> corrected;
        auto now = GetTickCount64();

        for (auto& [hwnd, placement] : m_placements)
        {
            if ((m_drag && m_drag->Hwnd == hwnd) || m_settling.contains(hwnd)) continue;

            auto window = m_tracker.Windows().find(hwnd);
            if (window == m_tracker.Windows().end() || window->second.State != WindowState::Normal) continue;

            // Snapped since it was placed, perhaps with Win and an arrow key.
            if (IsWindowArranged(hwnd))
            {
                if (placement.Unsnaps < MaxUnsnaps)
                    corrected.insert(placement.Tree);
                continue;
            }
            placement.Unsnaps = 0;

            RECT actual{};
            if (!GetWindowRect(hwnd, &actual) || Near(actual, placement.Settled)) continue;

            // Windows itself, as monitors come and go: put back, but not held
            // against the app.
            if (now < m_displaysSettleAt)
            {
                placement.Corrections = 0;
                corrected.insert(placement.Tree);
                continue;
            }

            // Something moved it: perhaps the app itself.
            if (now - placement.LastCorrection > CorrectionWindowMs)
                placement.Corrections = 0;

            placement.LastCorrection = now;
            if (++placement.Corrections >= MaxCorrections)
            {
                Trace::Log(L"arrange: " + window->second.ExeName + L" keeps moving itself - leaving it");
                stubborn.push_back(hwnd);
            }
            else
            {
                corrected.insert(placement.Tree);
            }
        }

        for (auto hwnd : stubborn)
            Float(hwnd);

        for (auto const& key : corrected)
            ArrangeTree(key);

        // On the tick rather than at each change: there are many, and a few
        // seconds lost to a crash is nothing a drag cannot put right.
        SaveState();
    }

    // ------------------------------------------------------------ across a restart

    bool Arranger::Persisting() const
    {
        // A test run arranges a few windows by title, and must not take the
        // real desktop's state, or leave its own in place of it.
        return !CommandLine::Value(L"--only-title");
    }

    std::int64_t Arranger::BootTime()
    {
        FILETIME now{};
        GetSystemTimeAsFileTime(&now);
        auto ms = static_cast<std::int64_t>((static_cast<ULONGLONG>(now.dwHighDateTime) << 32 | now.dwLowDateTime) / 10'000);
        return ms - static_cast<std::int64_t>(GetTickCount64());
    }

    void Arranger::SaveState()
    {
        if (!m_running || !Persisting()) return;

        auto saved = [](HWND hwnd, Core::WindowId area)
        {
            DWORD process = 0;
            GetWindowThreadProcessId(hwnd, &process);
            return Core::SavedWindow{ static_cast<std::uint64_t>(reinterpret_cast<UINT_PTR>(hwnd)), process, area };
        };

        Core::LayoutState state;
        state.Preset = m_preset;
        state.Boot = m_boot;

        for (auto const* layouts : { &m_layouts, &m_parked })
        {
            for (auto const& [key, layout] : *layouts)
            {
                Core::SavedLayout entry{ key.Monitor, key.Desktop, layout.Areas.Ratios(), {} };
                for (auto const& [hwnd, area] : layout.Members)
                {
                    entry.Members.push_back(saved(hwnd, area));
                    if (auto placement = m_placements.find(hwnd); placement != m_placements.end())
                        entry.Members.back().Insets = placement->second.Insets;
                }
                state.Layouts.push_back(std::move(entry));
            }
        }

        for (auto const& [hwnd, area] : m_returning)
            state.Returning.push_back(saved(hwnd, area));

        for (auto const& [app, area] : m_appAreas)
            state.Apps.push_back({ app.first, app.second, area });

        // Written only when it changed, which on most ticks it has not.
        auto text = Core::Json::Write(state.ToJson());
        if (text == m_savedText) return;

        if (Core::Json::WriteFile(Core::LayoutState::DefaultPath(), text))
            m_savedText = std::move(text);
    }

    void Arranger::Restore(Core::LayoutState const& state)
    {
        // Windows has restarted since, and every handle names something else,
        // or the areas were another preset's. The clock may have been set
        // since, so not to the millisecond.
        constexpr std::int64_t BootSlackMs = 2 * 60 * 1000;
        if (state.Preset != m_preset || std::abs(state.Boot - m_boot) > BootSlackMs)
        {
            Trace::Log(L"layout: saved state is from another boot or layout - not used");
            return;
        }

        // Still the same window: a handle can be given again to a later one,
        // but not in the same process.
        auto alive = [](Core::SavedWindow const& saved) -> HWND
        {
            HWND hwnd = reinterpret_cast<HWND>(static_cast<UINT_PTR>(saved.Window));
            DWORD process = 0;
            if (!IsWindow(hwnd) || !GetWindowThreadProcessId(hwnd, &process) || process != saved.Process)
                return nullptr;
            return hwnd;
        };

        size_t restored = 0;
        for (auto const& saved : state.Layouts)
        {
            TreeKey key{ saved.Monitor, saved.Desktop };

            Layout layout;
            layout.Areas = Core::MakeLayout(m_preset);
            layout.Areas.SetRatios(saved.Ratios);

            // Its monitor is not here: kept for its return, as when one is
            // unplugged. Its windows go where they are, for the moment.
            if (!MonitorOf(key))
            {
                for (auto const& member : saved.Members)
                    if (HWND hwnd = alive(member); hwnd && layout.Areas.Contains(member.Area))
                        layout.Members[hwnd] = member.Area;
                m_parked.insert_or_assign(key, std::move(layout));
                continue;
            }

            m_layouts.insert_or_assign(key, std::move(layout));
            for (auto const& member : saved.Members)
            {
                HWND hwnd = alive(member);
                if (!hwnd) continue;

                AddToLayout(hwnd, member.Area, key);
                if (auto placement = m_placements.find(hwnd); placement != m_placements.end())
                {
                    placement->second.Insets = member.Insets;
                    ++restored;
                    continue;
                }

                // Minimized meanwhile: it returns to the same area.
                if (IsIconic(hwnd))
                    m_returning[hwnd] = member.Area;
            }
        }

        for (auto const& member : state.Returning)
            if (HWND hwnd = alive(member))
                m_returning[hwnd] = member.Area;

        Trace::Log(L"layout: " + std::to_wstring(restored) + L" windows back in their areas");
    }

    void Arranger::DisplaysChanged()
    {
        auto monitors = Native::ListMonitors();
        m_signature = Native::SignatureOf(monitors);
        MonitorsChanged(std::move(monitors));
    }

    void Arranger::MonitorsChanged(std::vector<Native::Monitor> monitors)
    {
        Trace::Log(L"monitors: " + std::to_wstring(monitors.size()));
        m_monitors = std::move(monitors);
        m_displaysSettleAt = GetTickCount64() + DisplaySettleMs;

        if (!m_running || m_monitors.empty()) return;

        // Minimums are learned in pixels, at the scale of the monitor then.
        // After a change they may be wrong, so they are learned again.
        for (auto& [hwnd, placement] : m_placements)
            placement.Minimum = {};

        std::wstring primary = m_monitors.front().Key;
        for (auto const& monitor : m_monitors)
            if (monitor.Primary) primary = monitor.Key;

        // A monitor that has gone: its layout is kept, and its windows move to
        // the primary, for the moment.
        std::vector<TreeKey> lost;
        for (auto const& [key, layout] : m_layouts)
            if (!MonitorOf(key)) lost.push_back(key);

        for (auto const& key : lost)
        {
            auto entry = m_layouts.extract(key);
            auto members = entry.mapped().Members;
            m_parked.insert_or_assign(key, std::move(entry.mapped()));

            TreeKey home{ primary, key.Desktop };
            auto& destination = LayoutFor(home);
            for (auto const& [hwnd, area] : members)
            {
                if (auto placement = m_placements.find(hwnd); placement != m_placements.end())
                {
                    destination.Members[hwnd] = EmptiestArea(destination);
                    placement->second.Tree = home;
                }
            }
        }

        // A monitor that has returned: its layout comes back, sizes and all,
        // with the windows that still exist.
        std::vector<TreeKey> returned;
        for (auto const& [key, layout] : m_parked)
            if (MonitorOf(key)) returned.push_back(key);

        for (auto const& key : returned)
        {
            auto entry = m_parked.extract(key);
            auto layout = std::move(entry.mapped());

            for (auto it = layout.Members.begin(); it != layout.Members.end();)
            {
                auto placement = m_placements.find(it->first);
                if (placement == m_placements.end())
                {
                    it = layout.Members.erase(it);
                    continue;
                }

                if (auto now = m_layouts.find(placement->second.Tree); now != m_layouts.end())
                    now->second.Members.erase(it->first);

                placement->second.Tree = key;
                ++it;
            }

            m_layouts.insert_or_assign(key, std::move(layout));
        }

        ArrangeAll();
        AreasChanged();
    }

    void Arranger::SetDisplayOn(bool on)
    {
        m_displayOn = on;
        if (on) m_displaysSettleAt = GetTickCount64() + DisplaySettleMs;
        UpdateGapWatch();
    }

    // ------------------------------------------------------------ dragging a window

    void Arranger::OnDragStarted(HWND hwnd)
    {
        if (!m_running) return;

        Dragged drag;
        drag.Hwnd = hwnd;
        GetWindowRect(hwnd, &drag.Start);
        drag.Resized = drag.Start;
        m_drag = drag;

        Trace::Log(L"drag: started");

        // Floating windows show no preview, but for those dropped near a gap,
        // which can be dropped on an area again. The drag still has to be
        // known, so that a layout being arranged leaves it alone.
        if (m_placements.contains(hwnd) || m_loose.contains(hwnd))
            m_dragWatch.Start();
    }

    void Arranger::WatchDrag()
    {
        if (!m_drag) return;

        RECT now{};
        if (!GetWindowRect(m_drag->Hwnd, &now)) return;

        // A resize leaves at least one edge of each pair where it was, even at
        // a corner. Opposite edges moving together is the window going
        // somewhere, and once it is, it stays a move. The size says nothing:
        // Windows gives a window it snapped, or one being pulled out of
        // maximized, its old size as soon as the drag begins, and a change of
        // DPI between monitors resizes it on the way.
        auto const& start = m_drag->Start;
        bool sideways = now.left != start.left && now.right != start.right;
        bool upOrDown = now.top != start.top && now.bottom != start.bottom;

        bool wasMoving = m_drag->Moving;
        m_drag->Moving = m_drag->Moving || sideways || upOrDown;
        if (!m_drag->Moving)
        {
            FollowResize(now);
            return;
        }

        if (!wasMoving)
            UndoFollowedResize();

        POINT cursor{};
        GetCursorPos(&cursor);

        m_drag->Loosening = NearGapAt(m_drag->Hwnd, { cursor.x, cursor.y });
        m_drag->Landing = m_drag->Loosening ? std::nullopt : DropAt(m_drag->Hwnd, { cursor.x, cursor.y });

        double started = Trace::NowMs();
        ShowGuide(m_drag->Hwnd, { cursor.x, cursor.y });
        if (double took = Trace::NowMs() - started; took > 50)
            Trace::Log(L"drag: guide took " + std::to_wstring(std::lround(took)) + L" ms");
    }

    void Arranger::FollowResize(RECT const& now)
    {
        if (EqualRect(&now, &m_drag->Resized)) return;
        m_drag->Resized = now;

        auto placement = m_placements.find(m_drag->Hwnd);
        if (placement == m_placements.end()) return;

        // Only from its own place: a window that was somewhere else, or not
        // normal, gives no edges to follow.
        auto window = m_tracker.Windows().find(m_drag->Hwnd);
        if (window == m_tracker.Windows().end() || window->second.State != WindowState::Normal ||
            !Near(m_drag->Start, placement->second.Settled))
            return;

        auto layout = m_layouts.find(placement->second.Tree);
        if (layout == m_layouts.end()) return;

        if (!m_drag->AreasBefore)
            m_drag->AreasBefore = layout->second.Areas;

        // The windows beside it follow the edge as it goes, not once it is
        // let go. The one being resized is left to the user's hand.
        ApplyResize(m_drag->Hwnd, placement->second, now);
        ArrangeTree(placement->second.Tree);
    }

    void Arranger::UndoFollowedResize()
    {
        if (!m_drag || !m_drag->AreasBefore) return;

        auto areas = *std::exchange(m_drag->AreasBefore, std::nullopt);

        auto placement = m_placements.find(m_drag->Hwnd);
        if (placement == m_placements.end()) return;

        auto layout = m_layouts.find(placement->second.Tree);
        if (layout == m_layouts.end()) return;

        layout->second.Areas = std::move(areas);
        ArrangeTree(placement->second.Tree);
    }

    std::optional<TreeKey> Arranger::TreeAt(HWND dragged, Core::Pixel cursor)
    {
        auto monitor = MonitorAt(cursor);
        if (!monitor) return std::nullopt;

        // A loose window has no tree of its own to name the desktop.
        if (auto placement = m_placements.find(dragged); placement != m_placements.end())
            return TreeKey{ monitor->Key, placement->second.Tree.Desktop };
        if (m_loose.contains(dragged))
            return TreeKey{ monitor->Key, m_tracker.DesktopOf(dragged) };
        return std::nullopt;
    }

    bool Arranger::NearGapAt(HWND dragged, Core::Pixel cursor)
    {
        auto key = TreeAt(dragged, cursor);
        auto monitor = key ? MonitorOf(*key) : nullptr;
        if (!monitor) return false;

        auto& layout = LayoutFor(*key);
        auto space = SpaceOf(*monitor, layout);
        return layout.Areas.NearGap(cursor, space.Bounds, space.Gap, space.Minimums, LooseShare);
    }

    std::optional<Arranger::Drop> Arranger::DropAt(HWND dragged, Core::Pixel cursor)
    {
        auto key = TreeAt(dragged, cursor);
        auto monitor = key ? MonitorOf(*key) : nullptr;
        if (!monitor) return std::nullopt;

        auto& layout = LayoutFor(*key);

        // In a margin, the nearest area. Near a gap, NearGapAt has already
        // answered: let go there, the window leaves the layout.
        auto space = SpaceOf(*monitor, layout);
        auto tile = layout.Areas.NearestTile(cursor, space.Bounds, space.Gap, space.Minimums);
        if (!tile) return std::nullopt;

        return Drop{ *key, tile->Id };
    }

    void Arranger::ShowGuide(HWND dragged, Core::Pixel cursor)
    {
        auto key = TreeAt(dragged, cursor);
        auto monitor = key ? MonitorOf(*key) : nullptr;
        if (!monitor) return;

        auto& layout = LayoutFor(*key);
        auto space = SpaceOf(*monitor, layout);

        DropGuide::Scene scene;
        scene.Area = ToRect(monitor->Work);
        scene.Dpi = monitor->Dpi;

        std::optional<Core::Rect> landing;
        for (auto const& tile : layout.Areas.Arrange(space.Bounds, space.Gap, space.Minimums))
        {
            scene.Places.push_back(tile.Bounds);
            if (m_drag && m_drag->Landing && m_drag->Landing->Area == tile.Id)
                landing = tile.Bounds;
        }

        // In the user's hand, then the guide, then the preview: nothing of
        // ours goes over the window being dragged.
        m_guide.Show(scene, dragged);

        if (landing)
            m_preview.Show(*landing, m_guide.Hwnd() ? m_guide.Hwnd() : dragged);
        else
            m_preview.Hide();
    }

    void Arranger::ApplyDrop(HWND dragged, Drop const& drop)
    {
        auto placement = m_placements.find(dragged);
        if (placement == m_placements.end()) return;

        auto from = placement->second.Tree;
        auto& source = LayoutFor(from);
        auto member = source.Members.find(dragged);
        if (member == source.Members.end()) return;

        // Only the window dropped moves. The area it left stays, even empty,
        // and the windows already in the one it joined stay there, behind it.
        // Into another area it fills that area: its insets were for the old.
        if (from != drop.Tree || member->second != drop.Area)
            placement->second.Insets = {};

        // The app's next window opens here too.
        RememberArea(dragged, drop.Area);

        if (from == drop.Tree)
        {
            member->second = drop.Area;
        }
        else
        {
            source.Members.erase(member);
            LayoutFor(drop.Tree).Members[dragged] = drop.Area;
            placement->second.Tree = drop.Tree;
            ArrangeTree(drop.Tree);
        }

        ArrangeTree(from);
    }

    std::vector<Core::Edge> Arranger::ApplyResize(HWND hwnd, Placement& placement, RECT const& now)
    {
        std::vector<Core::Edge> unsplit;

        auto layout = m_layouts.find(placement.Tree);
        auto monitor = MonitorOf(placement.Tree);
        if (layout == m_layouts.end() || !monitor) return unsplit;

        auto member = layout->second.Members.find(hwnd);
        if (member == layout->second.Members.end()) return unsplit;

        RECT const& was = placement.Settled;
        RECT frame = VisibleFrame(hwnd);
        auto space = SpaceOf(*monitor, layout->second);

        auto move = [&](Core::Edge edge, int position)
        {
            if (!layout->second.Areas.MoveEdge(member->second, edge, position, space.Bounds, space.Gap,
                                               space.Minimums))
                unsplit.push_back(edge);
        };

        // Each edge that moved. An edge against the screen has no split; what
        // becomes of it is up to the caller.
        if (!Near(now.left, was.left)) move(Core::Edge::Left, frame.left);
        if (!Near(now.top, was.top)) move(Core::Edge::Top, frame.top);
        if (!Near(now.right, was.right)) move(Core::Edge::Right, frame.right);
        if (!Near(now.bottom, was.bottom)) move(Core::Edge::Bottom, frame.bottom);

        return unsplit;
    }

    void Arranger::KeepInsets(HWND hwnd, Placement& placement, std::vector<Core::Edge> const& edges)
    {
        auto layout = m_layouts.find(placement.Tree);
        auto monitor = MonitorOf(placement.Tree);
        if (layout == m_layouts.end() || !monitor || edges.empty()) return;

        auto member = layout->second.Members.find(hwnd);
        if (member == layout->second.Members.end()) return;

        auto space = SpaceOf(*monitor, layout->second);
        std::optional<Core::Rect> tile;
        for (auto const& candidate : layout->second.Areas.Arrange(space.Bounds, space.Gap, space.Minimums))
            if (candidate.Id == member->second) tile = candidate.Bounds;
        if (!tile) return;

        RECT frame = VisibleFrame(hwnd);
        int rejoin = DipsToPixels(RejoinEdgeDip, monitor->Dpi);
        auto inset = [&](int distance) { return distance <= rejoin ? 0 : distance; };

        auto& insets = placement.Insets;
        for (auto edge : edges)
        {
            switch (edge)
            {
            case Core::Edge::Left: insets.Left = inset(frame.left - tile->Left); break;
            case Core::Edge::Top: insets.Top = inset(frame.top - tile->Top); break;
            case Core::Edge::Right: insets.Right = inset(tile->Right - frame.right); break;
            case Core::Edge::Bottom: insets.Bottom = inset(tile->Bottom - frame.bottom); break;
            }
        }
    }

    void Arranger::OnDragEnded(HWND hwnd)
    {
        m_dragWatch.Stop();
        m_preview.Hide();
        m_guide.Hide();

        auto drag = std::exchange(m_drag, std::nullopt);
        if (!m_running || !drag || drag->Hwnd != hwnd) return;

        // A tray click waits behind this, so a slow one is worth knowing.
        struct Timed
        {
            double Started = Trace::NowMs();
            ~Timed()
            {
                if (double took = Trace::NowMs() - Started; took > 30)
                    Trace::Log(L"drag: handled in " + std::to_wstring(std::lround(took)) + L" ms");
            }
        } timed;

        Trace::Log(std::wstring(L"drag: ended, moving ") + (drag->Moving ? L"yes" : L"no") + L", landing " +
                   (drag->Landing ? L"yes" : L"no") + L", loosening " + (drag->Loosening ? L"yes" : L"no"));

        // Loose: it stays where it was let go, unless that was on an area.
        if (m_loose.contains(hwnd))
        {
            if (drag->Moving && drag->Landing && !IsZoomed(hwnd))
                Rejoin(hwnd, *drag->Landing);
            return;
        }

        auto found = m_placements.find(hwnd);
        if (found == m_placements.end()) return;

        auto& placement = found->second;

        RECT now{};
        if (!GetWindowRect(hwnd, &now)) return;

        // Esc returns the window to where it started, and nothing else
        // happens: what the windows beside it followed is undone.
        if (Near(now, placement.Settled))
        {
            if (drag->AreasBefore)
                if (auto layout = m_layouts.find(placement.Tree); layout != m_layouts.end())
                {
                    layout->second.Areas = std::move(*drag->AreasBefore);
                    ArrangeTree(placement.Tree);
                    AreasChanged();
                }
            return;
        }

        // Maximized by dragging to the top of the screen: left alone, and it
        // keeps its area for later.
        if (IsZoomed(hwnd)) return;

        bool leftMoved = !Near(now.left, placement.Settled.left);
        bool rightMoved = !Near(now.right, placement.Settled.right);
        bool topMoved = !Near(now.top, placement.Settled.top);
        bool bottomMoved = !Near(now.bottom, placement.Settled.bottom);

        // A resize moves one edge of each pair. Both moving is the window
        // going somewhere.
        bool resized = !(leftMoved && rightMoved) && !(topMoved && bottomMoved);

        // An edge with no split to move, as a column's top or bottom, stays
        // where it was let go; the window no longer fills its area that way.
        if (resized)
            KeepInsets(hwnd, placement, ApplyResize(hwnd, placement, now));
        else if (drag->Loosening)
        {
            Loosen(hwnd);
            return;
        }
        else if (drag->Landing)
            ApplyDrop(hwnd, *drag->Landing);

        // Dropped back on its own area, or a resize done: to wherever the
        // layout says.
        if (auto still = m_placements.find(hwnd); still != m_placements.end())
            ArrangeTree(still->second.Tree);

        if (resized)
            AreasChanged();
    }

    // ------------------------------------------------------------ the gap line

    void Arranger::UpdateGapWatch()
    {
        bool anyGap = false;
        for (auto const& [key, layout] : m_layouts)
            anyGap = anyGap || (layout.Areas.Count() >= 2 && !layout.Members.empty());

        bool wanted = m_running && m_settings.Arrange.GapLine && m_displayOn && anyGap;

        if (wanted && !m_gapWatch.IsRunning())
            m_gapWatch.Start();
        else if (!wanted)
            StopWatchingGap();
    }

    void Arranger::StopWatchingGap()
    {
        m_gapWatch.Stop();
        m_gapCandidate.reset();
        m_gap.reset();
        if (m_gapHandle) m_gapHandle->Hide();
    }

    std::optional<Arranger::Gap> Arranger::GapAt(Core::Pixel point)
    {
        auto monitor = MonitorAt(point);
        if (!monitor) return std::nullopt;

        for (auto const& [key, layout] : m_layouts)
        {
            if (key.Monitor != monitor->Key || layout.Areas.Count() < 2 || layout.Members.empty()) continue;

            // The layout on the desktop being looked at.
            if (!m_tracker.IsOnCurrentDesktop(layout.Members.begin()->first)) continue;

            auto space = SpaceOf(*monitor, layout);
            if (auto hit = layout.Areas.GapAt(point, space.Bounds, space.Gap, space.Minimums))
                return Gap{ key, hit->Split, hit->Direction, hit->Gap };
        }

        return std::nullopt;
    }

    bool Arranger::GapIsClear(Gap const& gap, Core::Pixel point) const
    {
        auto layout = m_layouts.find(gap.Tree);

        // A member maximized or fullscreen stays in the layout but covers all
        // of it. Read live: the tracker sees it only on the reconcile tick.
        // Asked before the window under the cursor, which may be the line.
        if (layout != m_layouts.end())
            for (auto const& [hwnd, area] : layout->second.Members)
                if (!IsIconic(hwnd) && (IsZoomed(hwnd) || Native::IsFullscreen(hwnd))) return false;

        // Otherwise a floating or maximized window is covering the gap.
        HWND under = GetAncestor(WindowFromPoint({ point.X, point.Y }), GA_ROOT);
        if (m_gapHandle->Hwnd() && under == m_gapHandle->Hwnd()) return true;

        if (layout != m_layouts.end() && layout->second.Members.contains(under)) return true;

        wchar_t name[32]{};
        GetClassNameW(under, name, 32);
        return wcscmp(name, L"Progman") == 0 || wcscmp(name, L"WorkerW") == 0;
    }

    void Arranger::WatchGap()
    {
        if (m_gapHandle->Dragging()) return;

        POINT cursor{};
        GetCursorPos(&cursor);
        Core::Pixel point{ cursor.x, cursor.y };

        auto gap = GapAt(point);
        if (gap && !GapIsClear(*gap, point)) gap.reset();

        if (!gap)
        {
            m_gapCandidate.reset();
            if (m_gapHandle->Visible())
            {
                m_gapHandle->Hide();
                m_gap.reset();
            }
            return;
        }

        if (m_gap && m_gapHandle->Visible() && m_gap->Split == gap->Split) return;

        // Two hits in a row, so that crossing a gap on the way somewhere does
        // not flash the line.
        bool again = m_gapCandidate && m_gapCandidate->Split == gap->Split;
        m_gapCandidate = gap;
        if (!again) return;

        auto monitor = MonitorOf(gap->Tree);
        if (!monitor) return;

        m_gap = gap;
        m_gapHandle->Show(gap->Bounds, gap->Direction, monitor->Dpi);
    }

    void Arranger::DragGap(Core::Pixel cursor)
    {
        if (!m_gap) return;

        auto layout = m_layouts.find(m_gap->Tree);
        auto monitor = MonitorOf(m_gap->Tree);
        if (layout == m_layouts.end() || !monitor) return;

        auto space = SpaceOf(*monitor, layout->second);

        bool sideBySide = m_gap->Direction == Core::Axis::SideBySide;
        int position = (sideBySide ? cursor.X : cursor.Y) - space.Gap / 2;
        layout->second.Areas.MoveSplit(m_gap->Split, position, space.Bounds, space.Gap, space.Minimums);

        ArrangeTree(m_gap->Tree);

        // Where the split really went: minimums may have stopped it.
        int across = sideBySide ? (m_gap->Bounds.Top + m_gap->Bounds.Bottom) / 2
                                : (m_gap->Bounds.Left + m_gap->Bounds.Right) / 2;
        for (int offset = 0; offset < 4; ++offset)
        {
            Core::Pixel probe = sideBySide ? Core::Pixel{ position + offset, across }
                                           : Core::Pixel{ across, position + offset };
            if (auto hit = layout->second.Areas.GapAt(probe, space.Bounds, space.Gap, space.Minimums);
                hit && hit->Split == m_gap->Split)
            {
                m_gap->Bounds = hit->Gap;
                m_gapHandle->Show(hit->Gap, hit->Direction, monitor->Dpi);
                break;
            }
        }
    }
}
