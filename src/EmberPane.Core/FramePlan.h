#pragma once

namespace EmberPane::Core
{
    /// How a window's frame is made, all in pixels, counted outward from the
    /// window's visible edge. Windows' own border lies just inside that edge,
    /// from -border to 0. It cannot be widened or moved, but it can be coloured
    /// or turned off, and turned off, that strip shows what is behind the
    /// window.
    struct FramePlan
    {
        /// Ember Pane draws part of it. Otherwise Windows' border is the frame.
        bool Layered{ false };

        /// Windows' border goes dark rather than taking the colour.
        bool DarkBorder{ false };

        /// Windows' border is turned off, to be seen through.
        bool NoBorder{ false };

        /// Any dark line past Windows' border. Drawn from inside it, so that
        /// the two meet without a seam.
        int DarkFrom{ 0 };
        int DarkTo{ 0 };

        int ColorFrom{ 0 };
        int ColorTo{ 0 };

        bool operator==(FramePlan const&) const = default;
    };

    /// A line of `inner` next to the window's content, then `color` of colour.
    /// The line is dark or, with `clear`, see-through, and that is all `clear`
    /// changes: the colour stays where it is.
    ///
    /// The colour is never narrower than it takes to leave no part of Windows'
    /// border outside it. Each window gets half the gap: past that the colour
    /// narrows, and with no room for it at all, Windows' border is the frame.
    FramePlan PlanFrame(int color, int inner, int border, int gap, bool clear = false);
}
