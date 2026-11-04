#include "pch.h"
#include "Host.h"
#include "CommandLine.h"
#include "HeatRamp.h"
#include "Managed/FrameService.h"
#include "Managed/Timing.h"
#include "Native/Shell.h"
#include "Trace.h"

namespace EmberPane
{
    namespace
    {
        /// See m_uiGoneAt: comfortably longer than a click, far shorter than
        /// a deliberate second one.
        constexpr ULONGLONG ReopenGuardMs = 500;

        /// ViewAll while arranging, Pause while not. Both are in Segoe Fluent
        /// Icons and Segoe MDL2 Assets.
        constexpr wchar_t ArrangingGlyph = L'';
        constexpr wchar_t PausedGlyph = L'';

        wchar_t TrayGlyph(Core::AppSettings const& settings)
        {
            return settings.Arrange.Enabled ? ArrangingGlyph : PausedGlyph;
        }

        std::wstring Quote(std::wstring const& s)
        {
            return L"\"" + s + L"\"";
        }

        /// Ticks are written to the trace after half a minute, once things
        /// have settled, and then every ten minutes.
        constexpr int FirstTickReport = 15;
        constexpr int TickReportEvery = 300;

    }

    Host::Host(winrt::Windows::System::DispatcherQueue const& dispatcher)
        : m_dispatcher(dispatcher)
    {
        bool firstRun = !Core::AppSettings::ExistsAtDefaultPath();

        m_settings = Core::AppSettings::LoadDefault();

        // --arrange: on for this run, saved nowhere. Only beside --only-title,
        // so that it can never rearrange a real desktop by accident.
        if (CommandLine::Has(L"--arrange") && CommandLine::Value(L"--only-title"))
            m_settings.Arrange.Enabled = true;

        m_events = std::make_unique<Native::SystemEvents>();
        m_link = std::make_unique<Link::Endpoint>(Link::HostClass, [this](Link::Message const& m) { OnMessage(m); });

        m_tray = std::make_unique<TrayIcon>(TrayGlyph(m_settings), [this] { OnTrayClick(); },
                                            [this] { OnTrayRightClick(); });

        m_events->ThemeChanged = [this]
        {
            Trace::Log(L"theme changed - repainting tray glyph");
            m_tray->Repaint();
        };

        m_sampler = std::make_unique<Managed::CpuSampler>();
        m_sampler->SetWindowSeconds(m_settings.Frames.WindowSeconds);

        m_tracker = std::make_unique<Managed::WindowTracker>(dispatcher, m_settings);
        m_tracker->Changed.Add([this] { Send(MakeWindows()); });
        m_tracker->Start();

        // After Start, which it then arranges in the order of the screen.
        m_arranger = std::make_unique<Managed::Arranger>(dispatcher, *m_tracker, m_settings);
        m_rings = std::make_unique<Managed::FrameRings>(dispatcher, *m_tracker, *m_arranger, m_settings);

        m_events->DisplaysChanged = [this]
        {
            Trace::Log(L"displays changed");
            m_arranger->DisplaysChanged();
        };
        m_events->WorkAreaChanged = [this] { m_arranger->DisplaysChanged(); };

        // Nothing to sample while no one can see it, and the time it was off
        // must not read as an idle stretch.
        m_events->DisplayPowerChanged = [this](Native::DisplayPower state)
        {
            Trace::Log(L"display power: " + std::to_wstring(static_cast<int>(state)));
            m_arranger->SetDisplayOn(state != Native::DisplayPower::Off);

            if (state == Native::DisplayPower::Off)
            {
                m_tick.Stop();
            }
            else if (!m_tick.IsRunning())
            {
                m_sampler->ClearWindow();
                m_tick.Start();
            }
        };

        StartTicking();

        // --show / --settings: open a window at startup, for measurement and
        // screenshots; reaching Settings otherwise means a real click on the
        // tray, which a screenshot script cannot do. A first run opens the
        // panel too, since that is where arranging is offered.
        using CommandLine::Has;
        if (Has(L"--settings"))
            LaunchUi(L"--settings");
        else if (Has(L"--show") || firstRun)
            LaunchUi(L"--show");

        if (firstRun)
            m_settings.Save();
    }

    Host::~Host()
    {
        if (m_tick) m_tick.Stop();
        if (m_uiWait) UnregisterWaitEx(m_uiWait, INVALID_HANDLE_VALUE);
        if (m_uiProcess) CloseHandle(m_uiProcess);
    }

    // ------------------------------------------------------------ the tick

    void Host::StartTicking()
    {
        m_tick = m_dispatcher.CreateTimer();
        m_tick.Interval(std::chrono::seconds(Managed::SampleSeconds));
        m_tick.Tick([this](auto&&, auto&&) { OnTick(); });
        m_tick.Start();

        OnTick();
    }

    void Host::OnTick()
    {
        double started = Trace::NowMs();

        m_tracker->Reconcile();
        m_arranger->Reconcile();
        m_sampler->SetRoots(m_tracker->ProcessIds());

        // With frames off and nothing open to show it, nobody is looking.
        bool rebuilt = false;
        if (m_settings.Frames.Enabled || m_uiState == UiState::Connected)
            rebuilt = m_sampler->Tick();

        PaintFrames();

        // Reported apart: the first opens every process cold, and a rebuild
        // walks the tree, so neither says what an ordinary tick costs.
        double took = Trace::NowMs() - started;
        if (m_tickCount == 0)
            m_firstTickMs = took;
        else if (rebuilt)
            m_slowestRebuildMs = std::max(m_slowestRebuildMs, took);
        else
            m_slowestTickMs = std::max(m_slowestTickMs, took);

        ++m_tickCount;
        if (m_tickCount == FirstTickReport || m_tickCount % TickReportEvery == 0)
        {
            Trace::Log(L"ticks: " + std::to_wstring(m_tickCount) + L", first " + std::to_wstring(m_firstTickMs) +
                       L" ms, slowest ordinary " + std::to_wstring(m_slowestTickMs) + L" ms, slowest rebuild " +
                       std::to_wstring(m_slowestRebuildMs) + L" ms, " +
                       std::to_wstring(m_tracker->Windows().size()) + L" windows");
            m_slowestTickMs = 0;
            m_slowestRebuildMs = 0;
        }
    }

    void Host::PaintFrames()
    {
        for (auto& [hwnd, window] : m_tracker->Windows())
        {
            auto usage = m_sampler->For(window.ProcessId);

            // Not known reads as resting: a colour must mean something.
            bool show = m_settings.Frames.Enabled && usage.Known;
            bool lit = !window.Heat.UseSystemDefault;
            window.Heat = show ? Core::ColorFor(usage.Rate, m_settings.Frames.Theme, lit) : Core::FrameColor{};
        }

        m_rings->Update();
    }

    // ------------------------------------------------------------ tray

    void Host::OnTrayClick()
    {
        switch (m_uiState)
        {
        case UiState::Connected:
        {
            // The click on our own tray icon is what grants the right to take
            // the foreground; it is ours, and the panel is in another process.
            AllowSetForegroundWindow(m_uiProcessId);
            Send(Link::Make(Link::Type::Toggle));
            break;
        }

        case UiState::Starting:
            // Already on its way; a second click would only stack a toggle
            // onto a panel the user has not seen yet.
            break;

        case UiState::None:
            if (GetTickCount64() - m_uiGoneAt < ReopenGuardMs)
            {
                Trace::Log(L"tray: click just closed the panel - not reopening");
                break;
            }
            LaunchUi(L"--show");
            break;
        }
    }

    void Host::OnTrayRightClick()
    {
        switch (m_uiState)
        {
        case UiState::Connected:
            AllowSetForegroundWindow(m_uiProcessId);
            Send(Link::Make(Link::Type::OpenSettings));
            break;
        case UiState::Starting:
            break;
        case UiState::None:
            LaunchUi(L"--settings");
            break;
        }
    }

    // ------------------------------------------------------------ the UI process

    void Host::LaunchUi(wchar_t const* action)
    {
        auto exe = Native::Shell::ExecutablePath();
        std::wstring command = Quote(exe) + L" --ui " + action;

        wchar_t hwnd[32];
        swprintf_s(hwnd, L" --host=%llu", static_cast<unsigned long long>(reinterpret_cast<UINT_PTR>(m_link->Hwnd())));
        command += hwnd;

        // What the host was started with that is really the UI's business.
        if (CommandLine::Has(L"--pin")) command += L" --pin";
        for (auto name : { L"--theme", L"--lang" })
            if (auto value = CommandLine::Value(name))
                command += L" " + std::wstring(name) + L"=" + Quote(*value);

        Trace::Log(L"ui: starting (" + std::wstring(action) + L")");

        STARTUPINFOW startup{ sizeof(startup) };
        PROCESS_INFORMATION process{};
        if (!CreateProcessW(exe.c_str(), command.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &startup,
                            &process))
        {
            Trace::Log(L"ui: could not start (" + std::to_wstring(GetLastError()) + L")");
            return;
        }

        CloseHandle(process.hThread);

        // Ours to pass on: the click that asked for the panel.
        AllowSetForegroundWindow(process.dwProcessId);

        if (m_uiProcess) CloseHandle(m_uiProcess);
        m_uiProcess = process.hProcess;
        m_uiProcessId = process.dwProcessId;
        m_uiState = UiState::Starting;

        // Its exit, however it comes - a quit, a close, a crash - arrives on
        // the thread pool and is handed back to this thread.
        RegisterWaitForSingleObject(&m_uiWait, m_uiProcess, [](void* context, BOOLEAN)
        {
            auto self = static_cast<Host*>(context);
            self->m_dispatcher.TryEnqueue([self] { self->OnUiGone(); });
        }, this, INFINITE, WT_EXECUTEONLYONCE);
    }

    void Host::OnUiConnected(HWND hwnd)
    {
        DWORD processId = 0;
        GetWindowThreadProcessId(hwnd, &processId);

        // Only the UI this host started. One started by hand finds the host
        // too, but a second copy of the panel would only confuse.
        if (processId != m_uiProcessId)
        {
            Trace::Log(L"ui: ignoring a UI this host did not start");
            return;
        }

        m_uiHwnd = hwnd;
        m_uiState = UiState::Connected;
        Trace::Log(L"ui: connected");

        Send(MakeState());
    }

    void Host::OnUiGone()
    {
        if (m_uiWait)
        {
            UnregisterWaitEx(m_uiWait, nullptr);
            m_uiWait = nullptr;
        }
        if (m_uiProcess)
        {
            CloseHandle(m_uiProcess);
            m_uiProcess = nullptr;
        }

        m_uiProcessId = 0;
        m_uiHwnd = nullptr;
        m_uiState = UiState::None;
        m_uiGoneAt = GetTickCount64();
        Trace::Log(L"ui: gone");
    }

    // ------------------------------------------------------------ messages

    void Host::OnMessage(Link::Message const& message)
    {
        auto type = Link::TypeOf(message);

        if (type == Link::Type::Hello)
        {
            auto hwnd = message.Find(L"hwnd");
            if (auto number = hwnd ? hwnd->AsNumber() : std::nullopt)
                OnUiConnected(reinterpret_cast<HWND>(static_cast<UINT_PTR>(*number)));
        }
        else if (type == Link::Type::SaveSettings)
        {
            auto settings = message.Find(L"settings");
            if (!settings) return;

            m_settings = Core::AppSettings::FromJson(*settings);
            m_settings.Save();

            // Arranging moves every window on the desktop, so a log should
            // always be able to say what turned it on.
            Trace::Log(L"settings saved: arrange=" + std::to_wstring(m_settings.Arrange.Enabled) +
                       L" gap=" + std::to_wstring(m_settings.Arrange.GapDip) +
                       L" frames=" + std::to_wstring(m_settings.Frames.Enabled));
            m_tray->SetGlyph(TrayGlyph(m_settings));

            m_sampler->SetWindowSeconds(m_settings.Frames.WindowSeconds);
            m_tracker->SettingsChanged();
            m_arranger->SettingsChanged();
            PaintFrames();
        }
        else if (type == Link::Type::Quit)
        {
            Quit();
        }
    }

    void Host::Send(Link::Message const& message)
    {
        if (m_uiState != UiState::Connected) return;
        m_link->Send(m_uiHwnd, message);
    }

    Link::Message Host::MakeWindows() const
    {
        auto list = Link::Message::MakeArray();
        for (auto const& [hwnd, window] : m_tracker->Windows())
        {
            auto item = Link::Message::MakeObject();
            item.Set(L"app", window.AppName);
            item.Set(L"exe", window.ExeName);
            list.Append(std::move(item));
        }

        auto m = Link::Make(Link::Type::Windows);
        m.Set(L"windows", std::move(list));
        return m;
    }

    Link::Message Host::MakeState() const
    {
        auto m = MakeWindows();
        m.Set(L"type", Link::Type::State);
        m.Set(L"settings", m_settings.ToJson());
        return m;
    }

    void Host::Quit()
    {
        Trace::Log(L"quit");

        m_tick.Stop();

        // Colours and corners outlive this process, so they go back first.
        m_tracker->ReleaseAll();

        // The UI is told, not waited for: it closes its windows and goes.
        Send(Link::Make(Link::Type::Quit));

        m_tray.reset();
        PostQuitMessage(0);
    }
}
