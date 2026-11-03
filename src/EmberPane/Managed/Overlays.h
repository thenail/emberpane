#pragma once

#include <d2d1.h>
#include <optional>
#include <vector>
#include "Geometry.h"

namespace EmberPane::Managed
{
    /// Windows' accent colour. Neither overlay uses the heat scale, so neither
    /// can be read as a sign of how busy an app is.
    COLORREF AccentColor();

    /// Where a dragged window will land: frosted glass with rounded corners, as
    /// Windows' own Snap shows, behind the window being dragged. WS_EX_TRANSPARENT,
    /// so it never catches the mouse. One window, made when first needed and
    /// gone when the drag ends.
    class DropPreview
    {
    public:
        ~DropPreview();

        /// Just below `above`: the guide, itself just below the window being
        /// dragged.
        void Show(Core::Rect const& rect, HWND above);
        void Hide();

        HWND Hwnd() const { return m_hwnd; }

    private:
        HWND m_hwnd{};
        std::optional<Core::Rect> m_shown;
    };

    /// Where a window can go, shown during a drag: each area of the layout,
    /// outlined. The area under the pointer is the frosted preview. Above the
    /// window in the user's hand, and click-through.
    ///
    /// Covers the monitor's work area with per-pixel alpha, so it is drawn only
    /// when what it shows changes, and exists only during a drag.
    class DropGuide
    {
    public:
        struct Scene
        {
            Core::Rect Area;
            UINT Dpi{ 96 };
            std::vector<Core::Rect> Places;

            bool operator==(Scene const&) const = default;
        };

        ~DropGuide();

        /// Just below the window being dragged.
        void Show(Scene const& scene, HWND dragged);
        void Hide();

        HWND Hwnd() const { return m_hwnd; }

    private:
        void Paint(Scene const& scene);

        HWND m_hwnd{};
        std::optional<Scene> m_shown;
    };

    /// What Windows' border cannot do: frames wider than it, or standing off a
    /// window with a dark line between. One click-through layer per monitor,
    /// just in front of the windows of the layout, because a window's shadow
    /// falls on everything behind it and takes the colour down by up to 40%.
    /// Clicks land on what is beneath, which in a gap is usually a window's
    /// resize border.
    ///
    /// Per-pixel alpha over a monitor's work area, so it is only drawn when
    /// what it shows changes, and exists only when a frame needs it. Never over
    /// the whole monitor; see Arranger::OnShow.
    class FrameLayer
    {
    public:
        /// A rounded band round a window, Inner to Outer pixels out from its
        /// visible edge.
        struct Band
        {
            Core::Rect Frame;
            int Inner{ 0 };
            int Outer{ 0 };
            COLORREF Color{};
            int Radius{ 0 };

            bool operator==(Band const&) const = default;
        };

        struct Scene
        {
            Core::Rect Area;
            std::vector<Band> Bands;

            bool operator==(Scene const&) const = default;
        };

        ~FrameLayer();

        FrameLayer() = default;
        FrameLayer(FrameLayer const&) = delete;
        FrameLayer& operator=(FrameLayer const&) = delete;

        enum class Stack
        {
            InFront,
            Beneath,
        };

        /// `front` is the frontmost window of the layout at rest, which it
        /// goes just in front of; windows further forward stay in front of it.
        /// Or beneath, with no window at rest.
        void Show(Scene const& scene, HWND front, Stack stack);

        /// Out of sight, keeping what it shows for when it is shown again.
        void Hide();

        /// Lets go of the drawing surface, a monitor's worth of pixels, until
        /// something changes.
        void Trim();

        /// Takes the drawing surface back, if it was let go, ahead of changes.
        void Warm();

        HWND Hwnd() const { return m_hwnd; }

    private:
        void Paint(Scene const& scene);

        /// Draws one rectangle of the layer into the surface.
        void DrawPart(Scene const& scene, RECT const& part);
        void InFrontOf(HWND front);
        void Beneath(HWND window);

        /// The parts of the layer to redraw, or none for all of it.
        static std::optional<std::vector<RECT>> Changed(Scene const& was, Scene const& now);

        HWND m_hwnd{};
        std::optional<Scene> m_shown;

        /// The surface holds what is shown, so that only changes need drawing.
        bool m_painted{ false };

        HDC m_dc{};
        HBITMAP m_bitmap{};
        HGDIOBJ m_old{};
        BYTE* m_bits{};
        int m_width{ 0 };
        int m_height{ 0 };
    };

    /// The line in the gap between windows, which is also the handle: it
    /// covers the whole gap, nearly invisibly, so that a press anywhere across
    /// it is caught, including where no window's resize border reaches.
    ///
    /// Made on show and destroyed on hide. A window kept alive belongs to the
    /// virtual desktop it was created on, and would have a stale size and DPI.
    ///
    /// The Arranger's watch says when the cursor has left. TrackMouseEvent
    /// cannot: a window appearing under a cursor at rest is told at once that
    /// the cursor has gone, since Windows only updates that on movement.
    class GapHandle
    {
    public:
        struct Handlers
        {
            /// The press landed; the drag has begun.
            std::function<void()> Started;

            /// The cursor moved during a drag, at most 30 times a second.
            std::function<void(Core::Pixel)> Dragged;

            std::function<void()> Finished;
        };

        explicit GapHandle(Handlers handlers);
        ~GapHandle();

        GapHandle(GapHandle const&) = delete;
        GapHandle& operator=(GapHandle const&) = delete;

        void Show(Core::Rect const& gap, Core::Axis direction, UINT dpi);
        void Hide();

        bool Visible() const { return m_hwnd != nullptr; }
        bool Dragging() const { return m_dragging; }
        HWND Hwnd() const { return m_hwnd; }

    private:
        static LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
        LRESULT Handle(UINT message, WPARAM wParam, LPARAM lParam);
        void Paint(Core::Rect const& gap);

        Handlers m_handlers;
        HWND m_hwnd{};
        Core::Axis m_direction{ Core::Axis::SideBySide };
        UINT m_dpi{ 96 };
        bool m_dragging{ false };
        ULONGLONG m_lastDrag{ 0 };
    };
}
