#pragma once

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

        /// Just below the window being dragged, and above everything else.
        void Show(Core::Rect const& rect, HWND dragged);
        void Hide();

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

        void Show(Scene const& scene);
        void Hide();

    private:
        void Paint(Scene const& scene);

        HWND m_hwnd{};
        std::optional<Scene> m_shown;
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
