#include "pch.h"
#include "App.xaml.h"
#include "CommandLine.h"
#include "SettingsWindow.xaml.h"
#include "Strings.h"
#include "Trace.h"

using namespace winrt;
using namespace Microsoft::UI::Xaml;
namespace Link = ::EmberPane::Link;

namespace winrt::EmberPane::implementation
{
    App::App()
    {
        // --theme=light|dark: look at a theme without switching the desktop.
        // Must be set before any resources load.
        if (auto theme = ::EmberPane::CommandLine::Value(L"--theme"))
        {
            if (_wcsicmp(theme->c_str(), L"dark") == 0) RequestedTheme(ApplicationTheme::Dark);
            if (_wcsicmp(theme->c_str(), L"light") == 0) RequestedTheme(ApplicationTheme::Light);
        }

        // Goes in the log, as nothing else will say why the UI process died.
        UnhandledException([](IInspectable const&, UnhandledExceptionEventArgs const& e)
        {
            wchar_t code[16];
            swprintf_s(code, L"0x%08X", static_cast<unsigned>(e.Exception().value));
            ::EmberPane::Trace::Log(std::wstring(L"ui: unhandled ") + code + L" " + std::wstring(e.Message()));

#if defined _DEBUG
            if (IsDebuggerPresent())
                __debugbreak();
#endif
        });
    }

    void App::OnLaunched([[maybe_unused]] LaunchActivatedEventArgs const& e)
    {
        using ::EmberPane::CommandLine::Has;

        // The windows close and open over this process's short life; only
        // ExitIfIdle and Quit end it.
        DispatcherShutdownMode(DispatcherShutdownMode::OnExplicitShutdown);

        ::EmberPane::Trace::Log(std::wstring(L"ui: startup: ") + GetCommandLineW());

        // Before any window is built: each resolves its strings as it loads.
        ::EmberPane::Strings::UseDisplayLanguage();

        m_services = std::make_unique<::EmberPane::Services>();
        auto& services = *m_services;
        ::EmberPane::Services::Set(&services);

        services.OpenSettings = [this] { OpenSettings(); };
        services.Quit = [this] { Quit(); };
        services.SaveSettings = [this]
        {
            auto m = Link::Make(Link::Type::SaveSettings);
            m.Set(L"settings", m_services->Settings.ToJson());
            Send(m);
        };

        m_action = Has(L"--settings") ? Action::Settings : Action::Show;

        m_link = std::make_unique<Link::Endpoint>(Link::UiClass, [this](Link::Message const& m) { OnMessage(m); });

        // The host passes its window; one started by hand looks for it.
        if (auto host = ::EmberPane::CommandLine::Value(L"--host"))
            m_host = reinterpret_cast<HWND>(static_cast<UINT_PTR>(_wcstoui64(host->c_str(), nullptr, 10)));
        if (!m_host)
            m_host = FindWindowExW(HWND_MESSAGE, nullptr, Link::HostClass, nullptr);

        auto hello = Link::Make(Link::Type::Hello);
        hello.Set(L"hwnd", static_cast<double>(reinterpret_cast<UINT_PTR>(m_link->Hwnd())));
        if (!m_link->Send(m_host, hello))
        {
            ::EmberPane::Trace::Log(L"ui: no host to talk to - exiting");
            Exit();
            return;
        }

        m_helloTimeout = Microsoft::UI::Dispatching::DispatcherQueue::GetForCurrentThread().CreateTimer();
        m_helloTimeout.Interval(std::chrono::seconds(5));
        m_helloTimeout.IsRepeating(false);
        m_helloTimeout.Tick([this](auto&&, auto&&)
        {
            if (m_flyout) return;
            ::EmberPane::Trace::Log(L"ui: the host never answered - exiting");
            Exit();
        });
        m_helloTimeout.Start();
    }

    void App::Send(Link::Message const& message)
    {
        m_link->Send(m_host, message);
    }

    void App::OnMessage(Link::Message const& message)
    {
        auto type = Link::TypeOf(message);
        auto& services = *m_services;

        if (type == Link::Type::State)
        {
            OnState(message);
            return;
        }

        // Nothing is shown before the state arrives, so nothing needs to
        // follow a change before then.
        if (!m_flyout || m_exiting) return;

        if (type == Link::Type::Windows)
        {
            services.Windows.Replace(message);
        }
        else if (type == Link::Type::Status)
        {
            auto text = message.Find(L"text");
            services.StatusChanged(text ? text->AsString().value_or(L"") : L"");
        }
        else if (type == Link::Type::Settings)
        {
            ApplySettings(message);
            services.SettingsChanged();
        }
        else if (type == Link::Type::Toggle)
        {
            m_flyout->Toggle();
        }
        else if (type == Link::Type::OpenSettings)
        {
            OpenSettings();
        }
        else if (type == Link::Type::Quit)
        {
            ::EmberPane::Trace::Log(L"ui: the host is quitting");
            Close();
        }
    }

    void App::ApplySettings(Link::Message const& message)
    {
        if (auto settings = message.Find(L"settings"))
            m_services->Settings = ::EmberPane::Core::AppSettings::FromJson(*settings);
    }

    void App::OnState(Link::Message const& message)
    {
        if (m_flyout) return;
        m_helloTimeout.Stop();

        auto& services = *m_services;
        ApplySettings(message);

        m_flyout = make_self<FlyoutWindow>();

        // --pin keeps the panel up when it loses focus, for inspection.
        m_flyout->Pin(::EmberPane::CommandLine::Has(L"--pin"));
        m_flyout->Hidden = [this] { ExitIfIdle(); };

        services.Windows.Replace(message);

        if (auto status = message.Find(L"status"))
            services.StatusChanged(status->AsString().value_or(L""));

        if (m_action == Action::Settings)
            OpenSettings();
        else
            m_flyout->ShowFlyout();
    }

    void App::OpenSettings()
    {
        m_flyout->HideFlyout();

        // Reuse the open window rather than stacking copies - and bring it
        // back if it was minimised, the state that made asking again look
        // like nothing happened.
        if (m_settings)
        {
            m_settings.RestoreAndActivate();
            return;
        }

        m_settings = EmberPane::SettingsWindow();
        m_settings.Closed([this](auto&&, auto&&)
        {
            m_settings = nullptr;
            ExitIfIdle();
        });
        m_settings.Activate();
    }

    void App::ExitIfIdle()
    {
        if (m_exiting || m_flyout->IsOpen() || m_settings) return;

        // Nothing left to show: the process goes, and WinUI with it. The
        // next click on the tray starts a fresh one.
        ::EmberPane::Trace::Log(L"ui: nothing open - exiting");
        m_exiting = true;

        // Not from inside the window's own event.
        Microsoft::UI::Dispatching::DispatcherQueue::GetForCurrentThread().TryEnqueue([this]
        {
            m_flyout->Close();
            Exit();
        });
    }

    void App::Quit()
    {
        // The host goes too; it is the one in the tray.
        Send(Link::Make(Link::Type::Quit));
        Close();
    }

    void App::Close()
    {
        if (m_exiting) return;

        m_exiting = true;
        if (m_settings) m_settings.Close();
        if (m_flyout) m_flyout->Close();
        Exit();
    }
}
