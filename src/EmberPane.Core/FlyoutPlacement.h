#pragma once

#include <optional>

namespace EmberPane::Core
{
    enum class ScreenEdge
    {
        Left,
        Top,
        Right,
        Bottom,
    };

    struct Point
    {
        double Left;
        double Top;

        bool operator==(Point const&) const = default;
    };
}

/// Where the flyout rests. Pure arithmetic in DIPs, kept out of the window so
/// it can be pinned by tests - positioning produced two field bugs in Glimmer
/// (a pixel-to-DIP conversion that put the panel off-screen, and the taskbar
/// edge one below), and neither was visible from reading the code.
namespace EmberPane::Core::FlyoutPlacement
{
    /// Which edge the taskbar occupies, derived from how the work area is
    /// inset inside the monitor.
    ///
    /// This does NOT ask the shell, and that is the point. SHAppBarMessage
    /// (ABM_GETTASKBARPOS) returns success on Windows 11 while writing
    /// nothing: poison uEdge before the call and the poison survives, and
    /// since ABE_LEFT is 0, a caller that zeroes the struct reads "docked
    /// left" on the common bottom-taskbar setup.
    ///
    /// The largest inset wins, which also picks the taskbar out from any
    /// other appbars. Nothing when no edge is inset - an auto-hiding taskbar
    /// reserves no work area, so the caller should fall back.
    std::optional<ScreenEdge> DockedEdge(double screenLeft, double screenTop, double screenRight,
                                         double screenBottom, double workLeft, double workTop,
                                         double workRight, double workBottom);

    /// Resting position, tucked into the corner of the work area nearest the
    /// notification area, with a gap. Clamped so the panel cannot land
    /// off-screen whatever it was handed.
    Point Rest(ScreenEdge edge, double workLeft, double workTop, double workRight, double workBottom,
               double width, double height, double gap);

    /// Where the open slide begins: wholly behind the taskbar, its leading
    /// edge on the taskbar's inner edge - where the work area ends - as
    /// Quick Settings starts. The panel then slides directly away from the
    /// taskbar into Rest, so a bottom taskbar slides it up and a right
    /// taskbar slides it left. The close runs the same way back.
    Point SlideOrigin(ScreenEdge edge, double restLeft, double restTop, double width, double height,
                      double workLeft, double workTop, double workRight, double workBottom);

    /// Whether the slide runs along the horizontal axis.
    bool SlidesHorizontally(ScreenEdge edge);
}
