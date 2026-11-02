#pragma once

#include <functional>

namespace EmberPane::Native
{
    /// The console display's power state, from GUID_CONSOLE_DISPLAY_STATE.
    enum class DisplayPower
    {
        Off = 0,
        On = 1,

        /// Dimmed ahead of turning off, on hardware that does it. The panel is
        /// still lit, so this counts as on.
        Dimmed = 2,
    };

    /// The system notifications EmberPane reacts to, on one hidden window.
    ///
    ///   - Displays changed. Unplugging, docking or waking from sleep moves
    ///     the monitors under the layout. Windows sends several
    ///     WM_DISPLAYCHANGE messages for one physical event, so bursts are
    ///     collapsed into one callback.
    ///   - Work area changed. The taskbar moved or resized, so every layout's
    ///     area did too.
    ///   - Display power. Nothing needs sampling while no one can see it.
    ///   - Theme changed. The tray glyph is drawn for one taskbar colour and
    ///     outlives any switch between light and dark.
    ///
    /// A real top-level window, never shown, rather than a message-only one:
    /// message-only windows do not receive broadcasts, and WM_DISPLAYCHANGE
    /// and WM_SETTINGCHANGE are broadcasts.
    class SystemEvents
    {
    public:
        SystemEvents();
        ~SystemEvents();

        SystemEvents(SystemEvents const&) = delete;
        SystemEvents& operator=(SystemEvents const&) = delete;

        /// Raised once a burst of display changes has settled.
        std::function<void()> DisplaysChanged;

        /// Raised when the display power state changes, including the wake.
        std::function<void(DisplayPower)> DisplayPowerChanged;

        /// Raised when the taskbar's reserved space changes.
        std::function<void()> WorkAreaChanged;

        /// Raised when Windows switches between light and dark.
        std::function<void()> ThemeChanged;

        /// Starts optimistic: Windows delivers the real value right after
        /// registration.
        DisplayPower CurrentPower() const { return m_power; }

        bool PowerNotificationsRegistered() const { return m_powerRegistration != nullptr; }

    private:
        static LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
        LRESULT Handle(UINT message, WPARAM wParam, LPARAM lParam);
        void ScheduleDisplaysChanged();

        HWND m_hwnd{};
        HPOWERNOTIFY m_powerRegistration{};
        DisplayPower m_power{ DisplayPower::On };
    };
}
