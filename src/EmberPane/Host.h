#pragma once

#include <memory>
#include "Link.h"
#include "Managed/Arranger.h"
#include "Managed/CpuSampler.h"
#include "Managed/FrameRings.h"
#include "Managed/WindowTracker.h"
#include "Native/SystemEvents.h"
#include "Settings.h"
#include "TrayIcon.h"

namespace EmberPane
{
    /// The process that lives in the tray: the settings, the windows being
    /// managed, the system notifications and the icon. No WinUI - that is the
    /// point of it; see Link.h.
    ///
    /// Owns the settings file: the UI sends its edits here rather than
    /// writing them itself, so there is one writer and nothing to race.
    class Host
    {
    public:
        explicit Host(winrt::Windows::System::DispatcherQueue const& dispatcher);
        ~Host();

        Host(Host const&) = delete;
        Host& operator=(Host const&) = delete;

    private:
        enum class UiState { None, Starting, Connected };

        void OnTrayClick();
        void OnTrayRightClick();

        /// Starts EmberPane.exe --ui, which opens with this action.
        void LaunchUi(wchar_t const* action);
        void OnUiConnected(HWND hwnd);
        void OnUiGone();

        void OnMessage(Link::Message const& message);
        void Send(Link::Message const& message);
        Link::Message MakeState() const;
        Link::Message MakeWindows() const;

        /// Every SampleSeconds, with nothing changing as with everything.
        void OnTick();
        void PaintFrames();
        void StartTicking();

        void Quit();

        winrt::Windows::System::DispatcherQueue m_dispatcher;

        Core::AppSettings m_settings;
        std::unique_ptr<Native::SystemEvents> m_events;
        std::unique_ptr<TrayIcon> m_tray;
        std::unique_ptr<Link::Endpoint> m_link;
        std::unique_ptr<Managed::WindowTracker> m_tracker;
        std::unique_ptr<Managed::CpuSampler> m_sampler;
        std::unique_ptr<Managed::Arranger> m_arranger;
        std::unique_ptr<Managed::FrameRings> m_rings;

        winrt::Windows::System::DispatcherQueueTimer m_tick{ nullptr };
        int m_tickCount{ 0 };
        double m_firstTickMs{ 0 };
        double m_slowestTickMs{ 0 };
        double m_slowestRebuildMs{ 0 };

        UiState m_uiState{ UiState::None };
        HWND m_uiHwnd{};
        HANDLE m_uiProcess{};
        DWORD m_uiProcessId{};
        HANDLE m_uiWait{};

        /// When the UI process last went away. A click on the tray icon while
        /// the panel is open first takes focus from the panel, which closes
        /// it and ends the process - and the click itself arrives just after.
        /// It meant "close", so it must not start a new UI.
        ULONGLONG m_uiGoneAt{ 0 };
    };
}
