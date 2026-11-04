#include "pch.h"
#include "FlyoutWindow.xaml.h"
#include "Strings.h"
#if __has_include("FlyoutWindow.g.cpp")
#include "FlyoutWindow.g.cpp"
#endif
#include "FlyoutPlacement.h"
#include "LayoutPresets.h"
#include "Services.h"
#include "Trace.h"
#include <commctrl.h>
#pragma comment(lib, "comctl32.lib")

using namespace winrt;
using namespace Microsoft::UI;
using namespace Microsoft::UI::Windowing;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;
using Windows::Graphics::PointInt32;
namespace Core = ::EmberPane::Core;

namespace winrt::EmberPane::implementation
{
    namespace
    {
        /// Gap between the panel and the taskbar / screen edges, in DIPs.
        constexpr double Gap = 12;

        /// A layout's picture, in DIPs.
        constexpr float PictureWidth = 84, PictureHeight = 48;

        /// Quick Settings decelerates hard into place, and drops back faster.
        constexpr double OpenMs = 300;
        constexpr double CloseMs = 180;

        /// Must exceed the close so the whole close is covered, while staying
        /// short enough that a deliberate reopen still feels immediate.
        constexpr double ToggleGuardMs = 250;

        double QuinticOut(double t) { double u = 1 - t; return 1 - u * u * u * u * u; }
        double CubicIn(double t) { return t * t * t; }

        double NowMs() { return ::EmberPane::Trace::NowMs(); }

        /// The shell's own answer, used only when the work area cannot say -
        /// an auto-hiding taskbar reserves none. Checked for having been
        /// filled in: on Windows 11 this call reports success and writes
        /// nothing, and a zeroed uEdge reads as "left".
        std::optional<Core::ScreenEdge> AskShellForEdge()
        {
            APPBARDATA data{ sizeof(data) };
            if (!SHAppBarMessage(ABM_GETTASKBARPOS, &data))
                return std::nullopt;
            if (data.rc.right <= data.rc.left || data.rc.bottom <= data.rc.top)
                return std::nullopt;

            switch (data.uEdge)
            {
            case ABE_LEFT: return Core::ScreenEdge::Left;
            case ABE_TOP: return Core::ScreenEdge::Top;
            case ABE_RIGHT: return Core::ScreenEdge::Right;
            default: return Core::ScreenEdge::Bottom;
            }
        }

        wchar_t const* NameKey(Core::LayoutPreset preset)
        {
            using enum Core::LayoutPreset;
            switch (preset)
            {
            case OneAtATime: return L"LayoutOneAtATime";
            case ThreeColumns: return L"LayoutThreeColumns";
            case LargeAndSmall: return L"LayoutLargeAndSmall";
            case LargeAndTwoSmall: return L"LayoutLargeAndTwoSmall";
            case FourAreas: return L"LayoutFourAreas";
            default: return L"LayoutTwoColumns";
            }
        }

        LRESULT CALLBACK NoFrameProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR, DWORD_PTR)
        {
            if (msg == WM_NCCALCSIZE && wParam)
                return 0;
            return DefSubclassProc(hwnd, msg, wParam, lParam);
        }

    }

    FlyoutWindow::FlyoutWindow()
    {
        InitializeComponent();

        m_hwnd = GetWindowFromWindowId(AppWindow().Id());

        // A borderless, always-on-top panel that stays out of the taskbar and
        // Alt+Tab, like Quick Settings.
        auto presenter = OverlappedPresenter::Create();
        presenter.SetBorderAndTitleBar(false, false);
        presenter.IsResizable(false);
        presenter.IsMaximizable(false);
        presenter.IsMinimizable(false);
        presenter.IsAlwaysOnTop(true);
        AppWindow().SetPresenter(presenter);
        AppWindow().IsShownInSwitchers(false);

        // Taking the border away still leaves WS_DLGFRAME - and the presenter
        // puts it back on every show - whose frame paints a classic #F0F0F0
        // line just inside DWM's border: lost against a light panel, a bright
        // rim round a dark one. Claiming the whole window as client area
        // leaves that frame nowhere to paint; DWM's own border and rounding
        // are drawn over the window regardless.
        SetWindowSubclass(m_hwnd, &NoFrameProc, 0, 0);
        SetWindowPos(m_hwnd, nullptr, 0, 0, 0, 0,
                     SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);

        // Without a border Windows 11 stops rounding the window by itself.
        DWM_WINDOW_CORNER_PREFERENCE corner = DWMWCP_ROUND;
        DwmSetWindowAttribute(m_hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &corner, sizeof(corner));

        // The frame DWM draws round the panel is its light one - a bright
        // outline round a dark panel. On a window without a caption the dark
        // mode flag alone does not change it (DWM accepts it and draws light
        // regardless), so in dark the colour is given outright: Fluent's
        // SurfaceStrokeColorDefault (#757575 at 40%) over the dark page
        // colour #202020, as DWM takes only an opaque colour. Light keeps
        // DWM's own. Redone whenever the theme moves, since the panel lives
        // all session.
        auto paintFrame = [this]
        {
            BOOL dark = FocusSink().ActualTheme() == ElementTheme::Dark;
            DwmSetWindowAttribute(m_hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
            COLORREF border = dark ? RGB(0x42, 0x42, 0x42) : DWMWA_COLOR_DEFAULT;
            DwmSetWindowAttribute(m_hwnd, DWMWA_BORDER_COLOR, &border, sizeof(border));
        };
        paintFrame();
        FocusSink().ActualThemeChanged([paintFrame](auto&&, auto&&) { paintFrame(); });

        Activated([this](auto&&, WindowActivatedEventArgs const& args)
        {
            if (args.WindowActivationState() == WindowActivationState::Deactivated && !m_pinned)
            {
                ::EmberPane::Trace::Log(L"flyout: deactivated -> hiding");
                HideFlyout();
            }
        });

        SettingsButton().Click([](auto&&, auto&&)
        {
            if (auto& open = ::EmberPane::Services::Get().OpenSettings)
                open();
        });

        auto& services = ::EmberPane::Services::Get();

        services.StatusChanged.Add([this](std::wstring const& message)
        {
            m_status = message;
            UpdateStatus();
        });

        services.SettingsChanged.Add([this] { UpdateLayouts(); });
        services.AreasChanged.Add([this] { UpdateLayouts(); });

        BuildLayouts();
        UpdateStatus();
    }

    // ------------------------------------------------------------ content

    void FlyoutWindow::BuildLayouts()
    {
        auto resetName = hstring{ ::EmberPane::Strings::Get(L"LayoutResetSizes") };

        int index = 0;
        for (auto preset : Core::AllPresets())
        {
            Canvas picture;
            picture.Width(PictureWidth);
            picture.Height(PictureHeight);
            picture.HorizontalAlignment(HorizontalAlignment::Center);

            // No caption: the picture says it. The name is still there for a
            // pointer that rests on it and for a screen reader.
            auto name = hstring{ ::EmberPane::Strings::Get(NameKey(preset)) };

            Button button;
            button.Content(picture);
            button.HorizontalAlignment(HorizontalAlignment::Stretch);
            button.HorizontalContentAlignment(HorizontalAlignment::Center);
            button.VerticalAlignment(VerticalAlignment::Stretch);
            button.VerticalContentAlignment(VerticalAlignment::Center);
            button.Padding(ThicknessHelper::FromLengths(4, 12, 4, 12));
            Automation::AutomationProperties::SetName(button, name);
            ToolTipService::SetToolTip(button, box_value(name));

            // Choosing a layout says you want one, so it turns arranging on.
            button.Click([preset](auto&&, auto&&)
            {
                auto& services = ::EmberPane::Services::Get();
                services.Settings.Arrange.Layout = preset;
                services.Settings.Arrange.Enabled = true;
                services.SaveAndNotify();
            });

            // Over the picture's corner, and only once its areas have been
            // resized: a sibling of the layout's button, not inside it, so
            // the two are two controls to the keyboard and to a screen reader.
            FontIcon glyph;
            glyph.Glyph(L""); // Refresh
            glyph.FontSize(10);

            Button reset;
            reset.Content(glyph);
            reset.Width(22);
            reset.Height(22);
            reset.Padding(ThicknessHelper::FromUniformLength(0));
            reset.CornerRadius(CornerRadiusHelper::FromUniformRadius(4));
            reset.HorizontalAlignment(HorizontalAlignment::Right);
            reset.VerticalAlignment(VerticalAlignment::Top);
            reset.Margin(ThicknessHelper::FromLengths(0, 4, 4, 0));
            reset.Visibility(Visibility::Collapsed);
            Automation::AutomationProperties::SetName(reset, resetName);
            ToolTipService::SetToolTip(reset, box_value(resetName));

            reset.Click([this](auto&&, auto&&)
            {
                // It goes as the host answers; focus is not left to fall on
                // the first layout, with a focus rectangle.
                FocusSink().Focus(FocusState::Programmatic);
                if (auto& resetAreas = ::EmberPane::Services::Get().ResetAreas)
                    resetAreas();
            });

            Grid tile;
            tile.Children().Append(button);
            tile.Children().Append(reset);
            Grid::SetColumn(tile, index % 3);
            Grid::SetRow(tile, index / 3);

            Layouts().Children().Append(tile);
            m_pictures.push_back(picture);
            m_resets.push_back(reset);
            ++index;
        }

        UpdateLayouts();
    }

    void FlyoutWindow::UpdateLayouts()
    {
        auto const& services = ::EmberPane::Services::Get();
        auto const& settings = services.Settings;
        auto presets = Core::AllPresets();

        for (size_t i = 0; i < m_pictures.size() && i < presets.size(); ++i)
        {
            // The one in use is in the accent colour while arranging is on,
            // and drawn as its areas are now, if they have been resized.
            bool chosen = settings.Arrange.Enabled && presets[i] == settings.Arrange.Layout;
            bool resized = chosen && services.AreasPreset == presets[i] && !services.ResizedAreas.empty();

            auto key = chosen ? L"AccentFillColorDefaultBrush" : L"TextFillColorTertiaryBrush";
            auto brush = Application::Current().Resources().Lookup(box_value(key)).as<Media::Brush>();

            auto picture = m_pictures[i];
            picture.Children().Clear();
            for (auto const& area : resized ? services.ResizedAreas : Core::PictureOf(presets[i]))
            {
                constexpr float Inset = 2;

                Shapes::Rectangle shape;
                shape.RadiusX(3);
                shape.RadiusY(3);
                shape.Width(std::max(0.0f, area.Width() * PictureWidth / 1000 - Inset * 2));
                shape.Height(std::max(0.0f, area.Height() * PictureHeight / 1000 - Inset * 2));
                Canvas::SetLeft(shape, area.Left * PictureWidth / 1000 + Inset);
                Canvas::SetTop(shape, area.Top * PictureHeight / 1000 + Inset);
                shape.Fill(brush);
                picture.Children().Append(shape);
            }

            m_resets[i].Visibility(resized ? Visibility::Visible : Visibility::Collapsed);
        }
    }

    void FlyoutWindow::UpdateStatus()
    {
        bool show = !m_status.empty();
        Status().Text(m_status);
        Status().Visibility(show ? Visibility::Visible : Visibility::Collapsed);
    }

    // ------------------------------------------------------------ placement

    FlyoutWindow::Placement FlyoutWindow::CurrentPlacement() const
    {
        // The monitor the taskbar is on, which is where the tray icon was.
        HWND taskbar = FindWindowW(L"Shell_TrayWnd", nullptr);
        HMONITOR monitor = taskbar ? MonitorFromWindow(taskbar, MONITOR_DEFAULTTOPRIMARY)
                                   : MonitorFromPoint({ 0, 0 }, MONITOR_DEFAULTTOPRIMARY);

        MONITORINFO info{ sizeof(info) };
        GetMonitorInfoW(monitor, &info);

        UINT dpiX = 96, dpiY = 96;
        GetDpiForMonitor(monitor, MDT_EFFECTIVE_DPI, &dpiX, &dpiY);
        double scale = dpiX / 96.0;

        // The edge comes from how the work area is inset, not from the shell -
        // see FlyoutPlacement::DockedEdge. The shell is only the fallback for
        // an auto-hiding taskbar, which reserves no work area.
        auto edge = Core::FlyoutPlacement::DockedEdge(
            info.rcMonitor.left, info.rcMonitor.top, info.rcMonitor.right, info.rcMonitor.bottom,
            info.rcWork.left, info.rcWork.top, info.rcWork.right, info.rcWork.bottom);

        if (!edge) edge = AskShellForEdge();

        return { info.rcWork, scale, static_cast<int>(edge.value_or(Core::ScreenEdge::Bottom)) };
    }

    PointInt32 FlyoutWindow::RestingPosition(Placement const& p, int width, int height) const
    {
        // The arithmetic is done in DIPs, as the tests pin it, and converted
        // to pixels only at the end.
        double s = p.Scale;
        auto rest = Core::FlyoutPlacement::Rest(static_cast<Core::ScreenEdge>(p.Edge), p.Work.left / s, p.Work.top / s,
                                                p.Work.right / s, p.Work.bottom / s, width / s, height / s, Gap);
        return { static_cast<int32_t>(std::lround(rest.Left * s)), static_cast<int32_t>(std::lround(rest.Top * s)) };
    }

    PointInt32 FlyoutWindow::SlideOrigin(Placement const& p, PointInt32 rest) const
    {
        double s = p.Scale;
        auto origin = Core::FlyoutPlacement::SlideOrigin(static_cast<Core::ScreenEdge>(p.Edge), rest.X / s,
                                                         rest.Y / s, m_width / s, m_height / s, p.Work.left / s,
                                                         p.Work.top / s, p.Work.right / s, p.Work.bottom / s);
        return { static_cast<int32_t>(std::lround(origin.Left * s)), static_cast<int32_t>(std::lround(origin.Top * s)) };
    }

    RECT FlyoutWindow::VisiblePart(PointInt32 at) const
    {
        RECT window{ at.X, at.Y, at.X + m_width, at.Y + m_height };
        RECT visible{};
        IntersectRect(&visible, &window, &m_workArea);
        return visible;
    }

    void FlyoutWindow::ClipTo(PointInt32 at)
    {
        // Quick Settings is cut off at the taskbar's edge rather than passing
        // under it: the taskbar is translucent, and a panel behind it shows
        // through as a pale patch. So the window is clipped to the work area
        // while any of it lies outside - a region, redone every frame of the
        // slide - and whole again once it is clear.
        RECT visible = VisiblePart(at);
        RECT window{ at.X, at.Y, at.X + m_width, at.Y + m_height };
        if (EqualRect(&visible, &window))
        {
            if (m_clipped) SetWindowRgn(m_hwnd, nullptr, TRUE);
            m_clipped = false;
            return;
        }

        // A region takes DWM's rounding away, so the corners are cut into it:
        // Windows 11 rounds by 8 pixels at 100%.
        int diameter = static_cast<int>(std::lround(16 * m_scale));
        HRGN shape = CreateRoundRectRgn(0, 0, m_width + 1, m_height + 1, diameter, diameter);
        OffsetRect(&visible, -at.X, -at.Y);
        HRGN cut = CreateRectRgnIndirect(&visible);
        CombineRgn(shape, shape, cut, RGN_AND);
        DeleteObject(cut);

        // The window owns the region from here.
        SetWindowRgn(m_hwnd, shape, TRUE);
        m_clipped = true;
    }

    void FlyoutWindow::MoveClipped(PointInt32 to)
    {
        // The move and the clip land separately, so they go in the order
        // that never shows more than the new clip allows: a panel coming out
        // moves first and then shows more; one going back shows less first.
        auto area = [](RECT const& r) { return static_cast<long long>(r.right - r.left) * (r.bottom - r.top); };
        bool shrinking = area(VisiblePart(to)) < area(VisiblePart(AppWindow().Position()));

        if (shrinking) ClipTo(to);
        AppWindow().Move(to);
        if (!shrinking) ClipTo(to);
    }

    void FlyoutWindow::TuckUnderTaskbar()
    {
        // The slide starts and ends behind the taskbar, so the panel has to
        // be under it: just below it among the always-on-top windows, where
        // the taskbar covers whatever part has not come out yet. Activation
        // lifts the panel above everything, so this follows it.
        if (HWND taskbar = FindWindowW(L"Shell_TrayWnd", nullptr))
            SetWindowPos(m_hwnd, taskbar, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }

    int FlyoutWindow::MeasuredHeight()
    {
        Panel().Measure({ static_cast<float>(Panel().Width()), std::numeric_limits<float>::infinity() });
        return static_cast<int>(std::ceil(Panel().DesiredSize().Height));
    }

    void FlyoutWindow::RepositionIfVisible()
    {
        if (!m_visible || m_closing) return;

        // No animation: this is a correction, not a transition.
        auto p = CurrentPlacement();
        m_width = static_cast<int>(std::lround(Panel().Width() * p.Scale));
        m_height = static_cast<int>(std::lround(MeasuredHeight() * p.Scale));

        auto rest = RestingPosition(p, m_width, m_height);
        m_animation.reset();
        m_workArea = p.Work;
        m_scale = p.Scale;
        AppWindow().MoveAndResize({ rest.X, rest.Y, m_width, m_height });
        ClipTo(rest);
    }

    // ------------------------------------------------------------ show / hide

    double FlyoutWindow::MsSinceClosed() const
    {
        return NowMs() - m_closeStartedAt;
    }

    void FlyoutWindow::Toggle()
    {
        double sinceClose = NowMs() - m_closeStartedAt;
        ::EmberPane::Trace::Log(L"flyout: toggle, open=" + std::to_wstring(m_visible) + L" closing=" +
                                  std::to_wstring(m_closing) + L" sinceClose=" + std::to_wstring(std::lround(sinceClose)));

        if (m_closing || sinceClose < ToggleGuardMs)
            return;

        if (m_visible) HideFlyout();
        else ShowFlyout();
    }

    void FlyoutWindow::ShowFlyout()
    {
        m_showStartedAt = NowMs();
        m_closing = false;
        m_visible = true;

        // The first show in this process meets a window XAML has never laid
        // out or drawn: sliding it in straight away would slide in an empty
        // pane that fills in on the way. It is activated cloaked instead -
        // DWM keeps it off the screen while XAML loads and renders it - and
        // the slide starts once there is a frame to show.
        if (!m_rendered)
            Cloak(true);

        PlaceAtSlideOrigin();
        Activate();

        // The click on our own tray icon is what grants the right to take the
        // foreground; without it the panel would open behind the taskbar's
        // focus and never receive the deactivation that closes it.
        SetForegroundWindow(m_hwnd);
        TuckUnderTaskbar();

        // Reopening would otherwise restore focus to the last control used,
        // with its focus rectangle.
        FocusSink().Focus(FocusState::Programmatic);

        if (m_rendered)
        {
            StartOpening();
            return;
        }

        WhenRendered([this]
        {
            m_rendered = true;

            // Closed again while it was being drawn: it is hidden already,
            // and only has to be uncloaked for next time.
            if (!m_visible || m_closing)
            {
                Cloak(false);
                return;
            }

            // Measured again now the controls have their templates: buttons
            // measured before that are too short, and so was the panel.
            PlaceAtSlideOrigin();
            Cloak(false);
            StartOpening();
        });
    }

    void FlyoutWindow::PlaceAtSlideOrigin()
    {
        auto p = CurrentPlacement();
        m_width = static_cast<int>(std::lround(Panel().Width() * p.Scale));
        m_height = static_cast<int>(std::lround(MeasuredHeight() * p.Scale));

        m_rest = RestingPosition(p, m_width, m_height);
        m_workArea = p.Work;
        m_scale = p.Scale;
        auto start = SlideOrigin(p, m_rest);
        ClipTo(start);
        AppWindow().MoveAndResize({ start.X, start.Y, m_width, m_height });
    }

    void FlyoutWindow::StartOpening()
    {
        m_frameLogged = false;
        Animate(AppWindow().Position(), m_rest, OpenMs, &QuinticOut, nullptr);
    }

    void FlyoutWindow::Cloak(bool cloaked)
    {
        BOOL value = cloaked;
        DwmSetWindowAttribute(m_hwnd, DWMWA_CLOAK, &value, sizeof(value));
    }

    void FlyoutWindow::WhenRendered(std::function<void()> ready)
    {
        // Loaded says the tree is built and laid out; the frames after it say
        // it has been drawn. The second Rendering is the one that matters:
        // by then the first frame with the content in it has been committed.
        // A timer backs it up, so a frame that never comes - a GPU reset,
        // say - leaves the panel late rather than never shown.
        auto state = std::make_shared<std::pair<int, bool>>(0, false);
        auto finish = [ready, state]
        {
            if (state->second) return;
            state->second = true;
            ready();
        };

        auto fallback = Microsoft::UI::Dispatching::DispatcherQueue::GetForCurrentThread().CreateTimer();
        fallback.Interval(std::chrono::milliseconds(500));
        fallback.IsRepeating(false);
        fallback.Tick([finish](auto&&, auto&&)
        {
            ::EmberPane::Trace::Log(L"flyout: no frame in time - showing anyway");
            finish();
        });
        fallback.Start();

        auto countFrames = [this, finish, state, fallback]
        {
            auto token = std::make_shared<event_token>();
            *token = Media::CompositionTarget::Rendering([finish, state, fallback, token](auto&&, auto&&)
            {
                if (++state->first < 2) return;
                Media::CompositionTarget::Rendering(*token);
                fallback.Stop();
                finish();
            });
        };

        if (Panel().IsLoaded())
        {
            countFrames();
            return;
        }

        auto loaded = std::make_shared<event_token>();
        *loaded = Panel().Loaded([this, countFrames, loaded](auto&&, auto&&)
        {
            Panel().Loaded(*loaded);
            countFrames();
        });
    }

    void FlyoutWindow::HideFlyout()
    {
        if (!m_visible || m_closing) return;

        m_closing = true;
        m_closeStartedAt = NowMs();

        // The reverse of the open: back behind the taskbar it slid out of.
        TuckUnderTaskbar();
        auto p = CurrentPlacement();
        m_workArea = p.Work;
        m_scale = p.Scale;
        auto from = AppWindow().Position();
        auto to = SlideOrigin(p, from);

        Animate(from, to, CloseMs, &CubicIn, [this]
        {
            AppWindow().Hide();
            m_visible = false;
            m_closing = false;
            ::EmberPane::Trace::Log(L"flyout: hidden");
            if (Hidden) Hidden();
        });
    }

    // ------------------------------------------------------------ animation

    void FlyoutWindow::Animate(PointInt32 from, PointInt32 to, double durationMs, double (*ease)(double),
                               std::function<void()> done)
    {
        // The window itself moves, as Quick Settings does, so the acrylic
        // travels with the content rather than the content sliding inside a
        // stationary panel. Driven by the compositor's frame clock.
        m_animation = Animation{ from, to, NowMs(), durationMs, ease, std::move(done) };

        if (!m_rendering)
            m_rendering = Media::CompositionTarget::Rendering([this](auto&&, auto&&) { OnRendering(); });
    }

    void FlyoutWindow::OnRendering()
    {
        if (!m_frameLogged && m_visible)
        {
            // The first frame after showing is what the user sees, so that is
            // where the clock stops.
            m_frameLogged = true;
            ::EmberPane::Trace::Log(L"flyout: shown, first frame after " +
                                      std::to_wstring(std::lround(NowMs() - m_showStartedAt)) + L" ms");
        }

        if (!m_animation)
        {
            Media::CompositionTarget::Rendering(m_rendering);
            m_rendering = {};
            return;
        }

        auto& a = *m_animation;
        double t = std::clamp((NowMs() - a.Start) / a.Duration, 0.0, 1.0);
        double e = a.Ease(t);

        MoveClipped({ a.From.X + static_cast<int32_t>(std::lround((a.To.X - a.From.X) * e)),
                      a.From.Y + static_cast<int32_t>(std::lround((a.To.Y - a.From.Y) * e)) });

        if (t >= 1)
        {
            auto done = std::move(a.Done);
            m_animation.reset();
            if (done) done();
        }
    }
}
