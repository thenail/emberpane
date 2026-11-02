#pragma once

/// Physical pixels, in integers, so adjacent gaps come out exactly equal.
namespace EmberPane::Core
{
    struct Pixel
    {
        int X{ 0 };
        int Y{ 0 };

        bool operator==(Pixel const&) const = default;
    };

    struct Size
    {
        int Width{ 0 };
        int Height{ 0 };

        bool operator==(Size const&) const = default;
    };

    struct Rect
    {
        int Left{ 0 };
        int Top{ 0 };
        int Right{ 0 };
        int Bottom{ 0 };

        int Width() const { return Right - Left; }
        int Height() const { return Bottom - Top; }
        long long Area() const { return static_cast<long long>(Width()) * Height(); }

        bool Contains(Pixel p) const { return p.X >= Left && p.X < Right && p.Y >= Top && p.Y < Bottom; }

        bool operator==(Rect const&) const = default;
    };

    enum class Axis
    {
        /// The split is a vertical line, the children left and right.
        SideBySide,

        /// The split is a horizontal line, the children above and below.
        Stacked,
    };

    enum class Edge
    {
        Left,
        Top,
        Right,
        Bottom,
    };
}
