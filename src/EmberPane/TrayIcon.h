#pragma once

namespace EmberPane
{
    /// The notification-area icon, on a message-only window of its own.
    ///
    /// WinUI has no tray API, so this is plain Shell_NotifyIcon. Left-click
    /// (and Enter/Space when the icon has keyboard focus) opens the panel.
    /// Right-click goes straight to Settings rather than to a menu whose
    /// useful entries would be one click further in - Settings carries the
    /// rest, including Quit.
    class TrayIcon
    {
    public:
        TrayIcon(wchar_t glyph, std::function<void()> clicked, std::function<void()> rightClicked);
        ~TrayIcon();

        TrayIcon(TrayIcon const&) = delete;
        TrayIcon& operator=(TrayIcon const&) = delete;

        /// Arranging or paused: the glyph says which.
        void SetGlyph(wchar_t glyph);

        /// Repaints for the current taskbar theme. The icon lives for the
        /// whole session, so a theme switch would otherwise strand a white
        /// glyph on a now-light taskbar - the same invisibility, deferred.
        void Repaint() { SetGlyph(m_glyph); }

    private:
        static LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
        void Add();

        HWND m_hwnd{};
        HICON m_icon{};
        wchar_t m_glyph{};
        UINT m_taskbarCreated{};
        std::function<void()> m_clicked;
        std::function<void()> m_rightClicked;
    };

    /// Draws a Segoe Fluent Icons glyph into an HICON of the given pixel size,
    /// in white or black to suit the taskbar. The caller destroys it.
    HICON CreateGlyphIcon(wchar_t glyph, int size);
}
