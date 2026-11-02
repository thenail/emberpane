#include "pch.h"
#include "Managed/FrameRings.h"
#include "FramePlan.h"
#include "Managed/FrameService.h"

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
        m_arranger.Moved.Add([this] { Follow(); });
    }

    FrameRings::~FrameRings()
    {
        m_watch.Stop();
    }

    void FrameRings::Update()
    {
        auto const& frames = m_settings.Frames;
        int roundDip = m_settings.Arrange.Corners == Core::CornerStyle::RoundSmall ? RoundSmallDip : RoundDip;

        std::map<HWND, Core::FramePlan> plans;
        std::map<std::wstring, FrameLayer::Scene> scenes;
        std::map<std::wstring, HWND> fronts;
        std::set<HWND> behind;

        if (frames.Enabled)
        {
            for (auto const& shown : m_arranger.OnShow())
            {
                if (shown.Behind)
                {
                    behind.insert(shown.Hwnd);
                    continue;
                }

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

                auto& scene = scenes[shown.Monitor];
                scene.Area = shown.Bounds;
                if (plan.DarkTo > plan.DarkFrom)
                    scene.Bands.push_back({ rect, plan.DarkFrom, plan.DarkTo, ToColorRef(Core::DarkLine), radius });
                scene.Bands.push_back({ rect, plan.ColorFrom, plan.ColorTo, ToColorRef(window->second.Heat), radius });
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

        for (auto const& [monitor, scene] : scenes)
        {
            auto& layer = m_layers[monitor];
            if (!layer) layer = std::make_unique<FrameLayer>();
            layer->Show(scene, fronts[monitor]);

            // Colours change on the tick, far too seldom to keep this for.
            if (!m_watch.IsRunning())
                layer->Trim();
        }
    }

    void FrameRings::Follow()
    {
        m_quietAt = GetTickCount64() + FollowMs;
        if (!m_watch.IsRunning())
            m_watch.Start();
    }

    void FrameRings::OnWatch()
    {
        Update();

        if (m_layers.empty())
        {
            m_watch.Stop();
            return;
        }

        if (m_arranger.Holding())
        {
            m_quietAt = GetTickCount64() + FollowMs;
            return;
        }

        if (GetTickCount64() < m_quietAt) return;

        m_watch.Stop();
        for (auto const& [monitor, layer] : m_layers)
            layer->Trim();
    }
}
