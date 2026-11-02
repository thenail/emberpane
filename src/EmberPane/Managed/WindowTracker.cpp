#include "pch.h"
#include "Managed/WindowTracker.h"
#include "CommandLine.h"
#include "Managed/FrameService.h"
#include "Native/ProcessTree.h"
#include "Native/WindowFacts.h"
#include "Trace.h"
#include "WindowRules.h"

namespace EmberPane::Managed
{
    namespace
    {
        std::vector<HWND> TopLevelWindows()
        {
            std::vector<HWND> windows;
            EnumWindows([](HWND hwnd, LPARAM list)
            {
                reinterpret_cast<std::vector<HWND>*>(list)->push_back(hwnd);
                return TRUE;
            }, reinterpret_cast<LPARAM>(&windows));
            return windows;
        }
    }

    WindowTracker::WindowTracker(winrt::Windows::System::DispatcherQueue const& dispatcher,
                                 Core::AppSettings const& settings)
        : m_settings(settings)
    {
        // --only-title=text: manage only windows with this in their title. For
        // trying arranging on test windows, on a desktop whose real windows
        // must stay exactly where they are.
        m_onlyTitle = CommandLine::Value(L"--only-title");

        m_events = std::make_unique<Native::WinEvents>(dispatcher, [this](DWORD event, HWND hwnd) { OnEvent(event, hwnd); });
    }

    void WindowTracker::Start()
    {
        for (auto hwnd : TopLevelWindows())
            TryAdopt(hwnd);

        Trace::Log(L"windows: " + std::to_wstring(m_windows.size()) + L" managed");
    }

    WindowState WindowTracker::ReadState(HWND hwnd)
    {
        if (IsIconic(hwnd)) return WindowState::Minimized;
        if (IsZoomed(hwnd)) return WindowState::Maximized;
        if (Native::IsFullscreen(hwnd)) return WindowState::Fullscreen;
        return WindowState::Normal;
    }

    void WindowTracker::TryAdopt(HWND hwnd)
    {
        if (m_windows.contains(hwnd) || !Native::CouldBeAppWindow(hwnd)) return;

        auto reading = Native::ReadWindow(hwnd, m_desktops);
        if (m_onlyTitle && reading.Facts.Title.find(*m_onlyTitle) == std::wstring::npos) return;

        auto disposition = DispositionOf(hwnd, reading.Facts);
        if (disposition == Core::Disposition::Ignore) return;

        ManagedWindow window;
        window.Hwnd = hwnd;
        window.ProcessId = reading.ProcessId;
        window.ExeName = reading.Facts.ExeName;
        window.ClassName = reading.Facts.ClassName;
        window.Disposition = disposition;
        window.State = ReadState(hwnd);

        // An elevated window's path cannot be read, nor anything from it.
        window.AppName = reading.ExePath.empty() ? reading.Facts.Title : Native::ProcessTree::AppName(reading.ExePath);

        // Titles stay out of the trace; see Trace.h.
        Trace::Log(L"window: adopted " + window.ExeName + L" / " + window.ClassName);

        auto& adopted = m_windows.emplace(hwnd, std::move(window)).first->second;
        ApplyCorners(adopted);
        Added(hwnd);
        Changed();
    }

    Core::Disposition WindowTracker::DispositionOf(HWND hwnd, Core::WindowFacts const& facts) const
    {
        if (auto chosen = m_chosen.find(hwnd); chosen != m_chosen.end())
            return chosen->second;
        return Core::Classify(facts, m_settings.Rules);
    }

    void WindowTracker::SetDisposition(HWND hwnd, Core::Disposition disposition)
    {
        m_chosen[hwnd] = disposition;

        auto found = m_windows.find(hwnd);
        if (found == m_windows.end()) return;

        found->second.Disposition = disposition;
        found->second.Immovable = found->second.Immovable && disposition != Core::Disposition::Tile;
        Changed();
    }

    void WindowTracker::UpdateState(ManagedWindow& window)
    {
        auto previous = window.State;
        window.State = ReadState(window.Hwnd);

        if (window.State != previous)
            StateChanged(window.Hwnd, previous);
    }

    void WindowTracker::Forget(HWND hwnd)
    {
        auto found = m_windows.find(hwnd);
        if (found == m_windows.end()) return;

        // A window that has only hidden lives on, and may return.
        if (IsWindow(hwnd))
            FrameService::Release(found->second);
        else
            m_chosen.erase(hwnd);

        m_windows.erase(found);
        Removed(hwnd);
        Changed();
    }

    void WindowTracker::ApplyCorners(ManagedWindow& window)
    {
        // Corners are part of arranging. Showing how busy apps are moves and
        // reshapes nothing.
        auto const& arrange = m_settings.Arrange;
        bool asked = arrange.Enabled && arrange.Corners != Core::CornerStyle::Leave;
        FrameService::Round(window, asked ? std::optional(arrange.Corners) : std::nullopt);
    }

    void WindowTracker::OnEvent(DWORD event, HWND hwnd)
    {
        switch (event)
        {
        case EVENT_OBJECT_SHOW:
            TryAdopt(hwnd);
            break;

        case EVENT_OBJECT_UNCLOAKED:
        case EVENT_SYSTEM_FOREGROUND:
            // A window's styles may have been incomplete when it was shown.
            TryAdopt(hwnd);
            if (m_windows.contains(hwnd))
                Activated(hwnd);
            break;

        case EVENT_OBJECT_HIDE:
        case EVENT_OBJECT_DESTROY:
            // Close-to-tray apps hide, so both mean it has gone.
            Forget(hwnd);
            break;

        case EVENT_OBJECT_CLOAKED:
            // Off to another desktop it stays ours; a UWP app hiding itself
            // on this one has gone.
            if (m_windows.contains(hwnd) && m_desktops.IsOnCurrent(hwnd))
                Forget(hwnd);
            break;

        case EVENT_SYSTEM_MINIMIZESTART:
        case EVENT_SYSTEM_MINIMIZEEND:
            if (auto found = m_windows.find(hwnd); found != m_windows.end())
                UpdateState(found->second);
            break;

        case EVENT_SYSTEM_MOVESIZESTART:
            if (m_windows.contains(hwnd))
                DragStarted(hwnd);
            break;

        case EVENT_SYSTEM_MOVESIZEEND:
            if (auto found = m_windows.find(hwnd); found != m_windows.end())
            {
                UpdateState(found->second);
                DragEnded(hwnd);
            }
            break;
        }
    }

    void WindowTracker::Reconcile()
    {
        std::vector<HWND> gone;

        for (auto& [hwnd, window] : m_windows)
        {
            if (!IsWindow(hwnd))
            {
                gone.push_back(hwnd);
                continue;
            }

            UpdateState(window);

            // A UWP app's own process can appear after its frame did.
            if (window.ExeName == L"ApplicationFrameHost.exe")
                if (DWORD owner = Native::ProcessTree::OwnerOf(hwnd); owner != window.ProcessId)
                    window.ProcessId = owner;
        }

        for (auto hwnd : gone)
            Forget(hwnd);
    }

    void WindowTracker::SettingsChanged()
    {
        std::vector<HWND> ignored;

        for (auto& [hwnd, window] : m_windows)
        {
            auto reading = Native::ReadWindow(hwnd, m_desktops);
            window.Disposition = DispositionOf(hwnd, reading.Facts);

            if (window.Disposition == Core::Disposition::Ignore)
                ignored.push_back(hwnd);
            else
                ApplyCorners(window);
        }

        for (auto hwnd : ignored)
            Forget(hwnd);

        // A rule may have just let one in.
        for (auto hwnd : TopLevelWindows())
            TryAdopt(hwnd);
    }

    void WindowTracker::ReleaseAll()
    {
        for (auto& [hwnd, window] : m_windows)
            if (IsWindow(hwnd))
                FrameService::Release(window);

        m_windows.clear();
    }

    std::vector<DWORD> WindowTracker::ProcessIds() const
    {
        std::set<DWORD> ids;
        for (auto const& [hwnd, window] : m_windows)
            ids.insert(window.ProcessId);
        return { ids.begin(), ids.end() };
    }
}
