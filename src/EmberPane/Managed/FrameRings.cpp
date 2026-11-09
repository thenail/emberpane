#include "pch.h"
#include "Managed/FrameRings.h"
#include "FramePlan.h"
#include "Managed/FrameService.h"
#include "Trace.h"

using namespace std::chrono_literals;

namespace EmberPane::Managed
{
    namespace
    {
        /// Windows settle within this of being told to move.
        constexpr ULONGLONG FollowMs = 500;

        constexpr int RoundDip = 8;
        constexpr int RoundSmallDip = 4;

        /// Differs by scale: 2 pixels at 150%.
        int BorderPixels(HWND hwnd)
        {
            UINT thickness = 0;
            bool known = SUCCEEDED(DwmGetWindowAttribute(hwnd, DWMWA_VISIBLE_FRAME_BORDER_THICKNESS, &thickness,
                                                         sizeof(thickness)));
            return known && thickness > 0 ? static_cast<int>(thickness) : 1;
        }

        Core::Rect VisibleFrame(HWND hwnd)
        {
            RECT frame{};
            if (FAILED(DwmGetWindowAttribute(hwnd, DWMWA_EXTENDED_FRAME_BOUNDS, &frame, sizeof(frame))))
                GetWindowRect(hwnd, &frame);
            return { frame.left, frame.top, frame.right, frame.bottom };
        }

        COLORREF ToColorRef(Core::FrameColor color)
        {
            return RGB(color.R, color.G, color.B);
        }

        bool Overlap(Core::Rect const& a, Core::Rect const& b)
        {
            return a.Left < b.Right && b.Left < a.Right && a.Top < b.Bottom && b.Top < a.Bottom;
        }

        /// A window that hides what is behind it: shown, on this desktop, and
        /// not invisible. Topmost windows are in front of the frame layer
        /// anyway, and the layer and the other click-through overlays here
        /// cover nothing.
        bool Covers(HWND hwnd)
        {
            if (!IsWindowVisible(hwnd) || IsIconic(hwnd)) return false;

            auto ex = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
            if (ex & WS_EX_TOPMOST) return false;

            DWORD process = 0;
            GetWindowThreadProcessId(hwnd, &process);
            if (process == GetCurrentProcessId() && (ex & WS_EX_TRANSPARENT)) return false;

            BYTE alpha = 255;
            DWORD flags = 0;
            if ((ex & WS_EX_LAYERED) && GetLayeredWindowAttributes(hwnd, nullptr, &alpha, &flags) &&
                (flags & LWA_ALPHA) && alpha == 0)
                return false;

            DWORD cloaked = 0;
            DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked));
            return cloaked == 0;
        }

        /// What a window hides. A WinUI popup, such as a tooltip or a menu, is
        /// drawn by the app inside a window with room around it for its shadow,
        /// and Windows cannot say where. The margins were measured on a
        /// tooltip at 150% in November 2026, rounded down so that the hole is
        /// never smaller than the bubble.
        Core::Rect CoveredBy(HWND hwnd)
        {
            auto rect = VisibleFrame(hwnd);

            wchar_t name[64]{};
            GetClassNameW(hwnd, name, 64);
            if (wcscmp(name, L"Microsoft.UI.Content.PopupWindowSiteBridge") != 0) return rect;

            UINT dpi = GetDpiForWindow(hwnd);
            auto px = [dpi](int dip) { return dip * static_cast<int>(dpi) / 96; };
            Core::Rect bubble{ rect.Left + px(5), rect.Top + px(3), rect.Right - px(5), rect.Bottom - px(9) };
            return bubble.Width() > 0 && bubble.Height() > 0 ? bubble : rect;
        }

        struct Cover
        {
            HWND Hwnd{};
            Core::Rect Rect;
        };

        /// The windows that cover something, front to back, as far as the last
        /// of `wanted`, and for each of `wanted` how many of them are in front
        /// of it.
        std::vector<Cover> CoversDownTo(std::set<HWND> const& wanted, std::map<HWND, size_t>& inFront)
        {
            std::vector<HWND> order;
            EnumWindows([](HWND hwnd, LPARAM list)
            {
                reinterpret_cast<std::vector<HWND>*>(list)->push_back(hwnd);
                return TRUE;
            }, reinterpret_cast<LPARAM>(&order));

            std::vector<Cover> covers;
            for (auto hwnd : order)
            {
                if (inFront.size() == wanted.size()) break;
                if (wanted.contains(hwnd)) inFront[hwnd] = covers.size();
                if (Covers(hwnd)) covers.push_back({ hwnd, CoveredBy(hwnd) });
            }
            return covers;
        }
    }

    FrameRings::FrameRings(winrt::Windows::System::DispatcherQueue const& dispatcher, WindowTracker& tracker,
                           Arranger& arranger, Core::AppSettings const& settings)
        : m_tracker(tracker), m_arranger(arranger), m_settings(settings)
    {
        m_watch = dispatcher.CreateTimer();
        m_watch.Interval(33ms);
        m_watch.Tick([this](auto&&, auto&&) { OnWatch(); });

        m_tracker.Added.Add([this](HWND) { Follow(); });
        m_tracker.Removed.Add([this](HWND) { Follow(); });
        m_tracker.StateChanged.Add([this](HWND, WindowState) { Follow(); });
        m_tracker.Activated.Add([this](HWND) { Follow(); });
        m_tracker.DragStarted.Add([this](HWND) { Follow(); });
        m_tracker.DragEnded.Add([this](HWND) { Follow(); });
        m_tracker.OtherChanged.Add([this](HWND hwnd) { if (Touches(hwnd)) Follow(); });
        m_arranger.Moved.Add([this] { Follow(); });
        m_arranger.Rearranged.Add([this] { Follow(); });
    }

    FrameRings::~FrameRings()
    {
        m_watch.Stop();
    }

    void FrameRings::Update()
    {
        // The frames cannot keep up with windows being resized, and one left
        // behind looks worse than none. Windows' own borders are left as they
        // are, and the frames are back once the edge or gap is let go, or the
        // windows of a layout just chosen have all arrived.
        if (m_arranger.Resizing() || m_arranger.Rearranging())
        {
            for (auto const& [monitor, layer] : m_layers)
                layer->Hide();
            return;
        }

        auto const& frames = m_settings.Frames;
        int roundDip = m_settings.Arrange.Corners == Core::CornerStyle::RoundSmall ? RoundSmallDip : RoundDip;

        std::map<HWND, Core::FramePlan> plans;
        std::map<std::wstring, FrameLayer::Scene> scenes;
        std::map<std::wstring, HWND> fronts;
        std::map<std::wstring, HWND> firsts;
        std::set<HWND> behind;

        m_covering.clear();
        m_reach.clear();
        m_framed.clear();

        if (frames.Enabled)
        {
            auto onShow = m_arranger.OnShow();

            // The layer is in front of the layout's front window, and so of
            // everything behind it: a window's own tooltips and menus, which
            // sit just in front of it, and other windows between. Each band
            // leaves out what is between the layer and its window. What is in
            // front of the layer covers the band already, and a window being
            // dragged is, so a hole for it would only be redrawn as it moves,
            // and fall behind it.
            std::set<HWND> marks;
            for (auto const& shown : onShow)
                if (!shown.Behind) marks.insert(shown.Hwnd);
            for (auto const& [monitor, layer] : m_layers)
                if (layer->Hwnd()) marks.insert(layer->Hwnd());

            std::map<HWND, size_t> inFront;
            auto covers = marks.empty() ? std::vector<Cover>{} : CoversDownTo(marks, inFront);

            for (auto const& shown : onShow)
            {
                if (shown.Behind)
                {
                    behind.insert(shown.Hwnd);
                    continue;
                }

                // The layer goes just in front of the frontmost window at rest.
                // One still on its way to its area, just dropped, crosses
                // the others' frames, and they must stay beneath it.
                firsts.try_emplace(shown.Monitor, shown.Hwnd);
                if (!shown.Settling)
                    fronts.try_emplace(shown.Monitor, shown.Hwnd);

                // Resting, or not known: Windows' own border.
                auto window = m_tracker.Windows().find(shown.Hwnd);
                if (window == m_tracker.Windows().end() || window->second.Heat.UseSystemDefault) continue;

                auto plan = Core::PlanFrame(frames.ColorPx, frames.InnerPx, BorderPixels(shown.Hwnd), shown.Gap,
                                            frames.ClearInner);
                if (!plan.Layered) continue;

                plans[shown.Hwnd] = plan;

                auto rect = VisibleFrame(shown.Hwnd);
                int radius = MulDiv(roundDip, shown.Dpi, 96);

                int out = std::max({ plan.ColorTo, plan.DarkTo, 0 }) + 1;
                Core::Rect reach{ rect.Left - out, rect.Top - out, rect.Right + out, rect.Bottom + out };
                m_reach.push_back(reach);
                m_framed.insert(shown.Hwnd);

                // With no layer yet, everything in front of the window.
                size_t from = 0;
                if (auto layer = m_layers.find(shown.Monitor); layer != m_layers.end())
                    if (auto at = inFront.find(layer->second->Hwnd()); at != inFront.end())
                        from = at->second;

                std::vector<Core::Rect> holes;
                if (auto ahead = inFront.find(shown.Hwnd); ahead != inFront.end())
                {
                    for (size_t i = from; i < ahead->second; ++i)
                    {
                        if (!Overlap(covers[i].Rect, reach)) continue;
                        holes.push_back(covers[i].Rect);
                        m_covering.insert(covers[i].Hwnd);
                    }
                }

                auto& scene = scenes[shown.Monitor];
                scene.Area = shown.Bounds;
                if (plan.DarkTo > plan.DarkFrom)
                    scene.Bands.push_back(
                        { rect, plan.DarkFrom, plan.DarkTo, ToColorRef(Core::DarkLine), radius, holes });
                scene.Bands.push_back(
                    { rect, plan.ColorFrom, plan.ColorTo, ToColorRef(window->second.Heat), radius, holes });
            }
        }

        for (auto& [hwnd, window] : m_tracker.Windows())
        {
            // Windows' own border is partly see-through, so the colour of a
            // window stacked behind would show in the border of the one in
            // front of it. With the front one's border off, so would the whole
            // of that strip, so theirs go too and it shows what is behind them.
            auto border = window.Heat;
            if (behind.contains(hwnd))
                border = frames.ClearInner && frames.InnerPx > 0 ? Core::NoBorder : Core::FrameColor{};

            if (auto plan = plans.find(hwnd); plan != plans.end())
            {
                if (plan->second.DarkBorder) border = Core::DarkLine;
                else if (plan->second.NoBorder) border = Core::NoBorder;
            }

            FrameService::Paint(window, border);
        }

        // A window stays on the desktop it was made on, so one left behind on
        // another is made again here.
        std::erase_if(m_layers, [&](auto const& entry)
        {
            return !scenes.contains(entry.first) || !m_tracker.IsOnCurrentDesktop(entry.second->Hwnd());
        });

        HWND inHand = m_arranger.InHand();
        for (auto const& [monitor, scene] : scenes)
        {
            auto& layer = m_layers[monitor];
            if (!layer) layer = std::make_unique<FrameLayer>();
            // Never above a window in the user's hand, whatever else comes
            // forward meanwhile. With every window still moving, beneath the
            // frontmost of them.
            if (inHand)
                layer->Show(scene, inHand, FrameLayer::Stack::Beneath);
            else if (auto front = fronts.find(monitor); front != fronts.end())
                layer->Show(scene, front->second, FrameLayer::Stack::InFront);
            else
                layer->Show(scene, firsts[monitor], FrameLayer::Stack::Beneath);

            // Colours change on the tick, far too seldom to keep this for.
            if (!m_following)
                layer->Trim();
        }
    }

    bool FrameRings::Touches(HWND hwnd) const
    {
        if (m_covering.contains(hwnd)) return true;
        if (m_reach.empty() || !IsWindowVisible(hwnd)) return false;

        // A window's own tooltips and menus are shown first and put in place
        // after, which nothing reports.
        if (HWND owner = GetWindow(hwnd, GW_OWNER); owner && m_framed.contains(owner)) return true;

        auto rect = VisibleFrame(hwnd);
        return std::any_of(m_reach.begin(), m_reach.end(), [&](Core::Rect const& reach) { return Overlap(rect, reach); });
    }

    void FrameRings::Follow()
    {
        m_quietAt = GetTickCount64() + FollowMs;
        if (!m_following)
        {
            m_following = true;
            m_watch.Start();
        }
    }

    void FrameRings::SetHeld(bool held)
    {
        if (held == m_held) return;
        m_held = held;

        // While windows are being resized, the apps redrawing them come first,
        // and at normal priority the frames get a turn every few hundred
        // milliseconds, though they need only a few.
        SetThreadPriority(GetCurrentThread(), held ? THREAD_PRIORITY_ABOVE_NORMAL : THREAD_PRIORITY_NORMAL);

        // Drawn whole now, before anything has moved, so that from here on
        // only what changes is drawn.
        if (held)
            for (auto const& [monitor, layer] : m_layers)
                layer->Warm();
    }

    void FrameRings::OnWatch()
    {
        SetHeld(m_arranger.Holding());

        double started = Trace::NowMs();
        Update();
        double took = Trace::NowMs() - started;

        if (m_layers.empty())
        {
            SetHeld(false);
            m_following = false;
            m_watch.Stop();
            return;
        }

        if (m_held)
        {
            ++m_heldUpdates;
            m_heldTotalMs += took;
            m_heldSlowestMs = std::max(m_heldSlowestMs, took);
            m_quietAt = GetTickCount64() + FollowMs;
            return;
        }

        if (m_heldUpdates > 0)
        {
            Trace::Log(L"frames: " + std::to_wstring(m_heldUpdates) + L" updates while held, mean " +
                       std::to_wstring(std::lround(m_heldTotalMs / m_heldUpdates)) + L" ms, slowest " +
                       std::to_wstring(std::lround(m_heldSlowestMs)) + L" ms");
            m_heldUpdates = 0;
            m_heldTotalMs = 0;
            m_heldSlowestMs = 0;
        }

        if (GetTickCount64() < m_quietAt) return;

        m_following = false;
        m_watch.Stop();
        for (auto const& [monitor, layer] : m_layers)
            layer->Trim();
    }
}
