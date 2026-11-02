#pragma once

#include "FlyoutWindow.g.h"

namespace winrt::EmberPane::implementation
{
    struct FlyoutWindow : FlyoutWindowT<FlyoutWindow>
    {
        FlyoutWindow();

        /// Opens the panel, or closes it if it is open. A click that has just
        /// dismissed the panel (by taking focus away) must not reopen it, so a
        /// toggle during or just after a close is swallowed.
        void Toggle();

        void ShowFlyout();
        void HideFlyout();

        /// Keeps the panel open when it loses focus, for screenshots.
        void Pin(bool pinned) { m_pinned = pinned; }

        bool IsOpen() const { return m_visible; }

        /// Raised once the panel has finished sliding away.
        std::function<void()> Hidden;

    private:
        struct Placement
        {
            RECT Work;
            double Scale;
            int Edge; // Core::ScreenEdge
        };

        void BuildLayouts();
        void UpdateLayouts();
        void UpdateStatus();

        Placement CurrentPlacement() const;
        Windows::Graphics::PointInt32 RestingPosition(Placement const& placement, int width, int height) const;
        Windows::Graphics::PointInt32 SlideOrigin(Placement const& placement, Windows::Graphics::PointInt32 rest) const;
        int MeasuredHeight();
        void RepositionIfVisible();

        /// Sizes the panel to its content and puts it where the slide in
        /// starts; the resting position is kept for the slide to end at.
        void PlaceAtSlideOrigin();
        void TuckUnderTaskbar();

        /// The part of the panel inside the work area, were it at this point.
        RECT VisiblePart(Windows::Graphics::PointInt32 at) const;

        /// Clips the window to the work area, as it would be at this point.
        void ClipTo(Windows::Graphics::PointInt32 at);

        /// Moves the window and its clip together.
        void MoveClipped(Windows::Graphics::PointInt32 to);
        void StartOpening();

        /// Hides the window from the screen while it still renders.
        void Cloak(bool cloaked);

        /// Calls ready once the panel's content has been drawn.
        void WhenRendered(std::function<void()> ready);

        void Animate(Windows::Graphics::PointInt32 from, Windows::Graphics::PointInt32 to, double durationMs,
                     double (*ease)(double), std::function<void()> done);
        void OnRendering();

        HWND m_hwnd{};
        bool m_visible{ false };
        bool m_closing{ false };
        bool m_pinned{ false };
        double m_closeStartedAt{ -1e9 };
        double m_showStartedAt{ 0 };
        bool m_frameLogged{ true };
        int m_width{ 0 };
        int m_height{ 0 };
        Windows::Graphics::PointInt32 m_rest{};

        /// Where the panel may be seen during the slide, and at what scale.
        RECT m_workArea{};
        double m_scale{ 1 };

        /// A window region is set.
        bool m_clipped{ false };

        /// XAML has drawn the panel at least once in this process.
        bool m_rendered{ false };

        std::wstring m_status;
        std::vector<Microsoft::UI::Xaml::Controls::Canvas> m_pictures;

        struct Animation
        {
            Windows::Graphics::PointInt32 From;
            Windows::Graphics::PointInt32 To;
            double Start;
            double Duration;
            double (*Ease)(double);
            std::function<void()> Done;
        };
        std::optional<Animation> m_animation;
        event_token m_rendering{};
    };
}

namespace winrt::EmberPane::factory_implementation
{
    struct FlyoutWindow : FlyoutWindowT<FlyoutWindow, implementation::FlyoutWindow>
    {
    };
}
