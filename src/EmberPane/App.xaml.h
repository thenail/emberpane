#pragma once

#include "App.xaml.g.h"
#include "FlyoutWindow.xaml.h"
#include "Link.h"
#include "Services.h"

namespace winrt::EmberPane::implementation
{
    /// The UI process: the panel and Settings, and nothing that has to keep
    /// running once they are closed. Started by the host (see Link.h); exits
    /// when neither window is open.
    struct App : AppT<App>
    {
        App();

        void OnLaunched(Microsoft::UI::Xaml::LaunchActivatedEventArgs const&);

    private:
        enum class Action { Show, Settings };

        void OnMessage(::EmberPane::Link::Message const& message);
        void OnState(::EmberPane::Link::Message const& message);
        void ApplySettings(::EmberPane::Link::Message const& message);
        void Send(::EmberPane::Link::Message const& message);
        void OpenSettings();
        void ExitIfIdle();
        void Quit();

        /// Closes the windows and ends this process; the host carries on.
        void Close();

        std::unique_ptr<::EmberPane::Services> m_services;
        std::unique_ptr<::EmberPane::Link::Endpoint> m_link;
        HWND m_host{};

        /// What the host started this process to do, done once the host's
        /// state has arrived: a window built before that would open empty.
        Action m_action{ Action::Show };

        com_ptr<FlyoutWindow> m_flyout;
        EmberPane::SettingsWindow m_settings{ nullptr };

        /// Gives up if the host never answers, rather than lingering unseen.
        Microsoft::UI::Dispatching::DispatcherQueueTimer m_helloTimeout{ nullptr };

        bool m_exiting{ false };
    };
}
