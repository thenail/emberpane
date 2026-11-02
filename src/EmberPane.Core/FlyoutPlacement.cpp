#include "pch.h"
#include "FlyoutPlacement.h"

namespace EmberPane::Core::FlyoutPlacement
{
    std::optional<ScreenEdge> DockedEdge(double screenLeft, double screenTop, double screenRight,
                                         double screenBottom, double workLeft, double workTop,
                                         double workRight, double workBottom)
    {
        double left = workLeft - screenLeft;
        double top = workTop - screenTop;
        double right = screenRight - workRight;
        double bottom = screenBottom - workBottom;

        double widest = std::max(std::max(left, top), std::max(right, bottom));

        // A hair of rounding is not a taskbar.
        if (widest < 1.0) return std::nullopt;

        // Bottom first, then right: the common cases win ties.
        if (bottom >= widest) return ScreenEdge::Bottom;
        if (right >= widest) return ScreenEdge::Right;
        if (top >= widest) return ScreenEdge::Top;
        return ScreenEdge::Left;
    }

    Point Rest(ScreenEdge edge, double workLeft, double workTop, double workRight, double workBottom,
               double width, double height, double gap)
    {
        double left, top;
        switch (edge)
        {
        case ScreenEdge::Top:
            left = workRight - width - gap;
            top = workTop + gap;
            break;
        case ScreenEdge::Left:
            left = workLeft + gap;
            top = workBottom - height - gap;
            break;
        default:
            left = workRight - width - gap;
            top = workBottom - height - gap;
            break;
        }

        left = std::clamp(left, workLeft, std::max(workLeft, workRight - width));
        top = std::clamp(top, workTop, std::max(workTop, workBottom - height));

        return { left, top };
    }

    Point SlideOrigin(ScreenEdge edge, double restLeft, double restTop, double width, double height,
                      double workLeft, double workTop, double workRight, double workBottom)
    {
        switch (edge)
        {
        case ScreenEdge::Top: return { restLeft, workTop - height };
        case ScreenEdge::Left: return { workLeft - width, restTop };
        case ScreenEdge::Right: return { workRight, restTop };
        default: return { restLeft, workBottom };
        }
    }

    bool SlidesHorizontally(ScreenEdge edge)
    {
        return edge == ScreenEdge::Left || edge == ScreenEdge::Right;
    }
}
