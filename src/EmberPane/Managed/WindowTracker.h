#pragma once

#include <map>
#include <memory>
#include <vector>
#include "Event.h"
#include "Managed/ManagedWindow.h"
#include "Native/VirtualDesktops.h"
#include "Native/WinEvents.h"
#include "WindowRules.h"

namespace EmberPane::Managed
{
    /// Which windows there are, and what state each is in. Finds what is
    /// already open, follows what comes and goes, and notices on the reconcile
    /// tick what no event reports: a window maximized, an event missed.
    class WindowTracker
    {
    public:
        WindowTracker(winrt::Windows::System::DispatcherQueue const& dispatcher, Core::AppSettings const& settings);

        WindowTracker(WindowTracker const&) = delete;
        WindowTracker& operator=(WindowTracker const&) = delete;

        /// Adopts every window already open.
        void Start();

        /// Reads only, is cheap, and cannot block.
        void Reconcile();

        /// The rules or the corners changed: every window is looked at again.
        void SettingsChanged();

        /// Gives every window back, for quitting.
        void ReleaseAll();

        /// This window, whatever the rules say, until it closes: chosen in
        /// the panel, or decided here for one that would not stay put.
        void SetDisposition(HWND hwnd, Core::Disposition disposition);

        /// The window's virtual desktop, as Native::VirtualDesktops has it, or
        /// for one whose desktop Windows will not name but which is on the
        /// desktop being looked at, that desktop. Empty when neither is known.
        std::wstring DesktopOf(HWND hwnd);
        bool IsOnCurrentDesktop(HWND hwnd) { return m_desktops.IsOnCurrent(hwnd); }

        std::map<HWND, ManagedWindow>& Windows() { return m_windows; }

        /// The processes behind the windows.
        std::vector<DWORD> ProcessIds() const;

        /// A window came or went, or a choice about one changed.
        Event<> Changed;

        Event<HWND> Added;
        Event<HWND> Removed;

        /// With the state it had before.
        Event<HWND, WindowState> StateChanged;

        /// It came to the foreground, or back from another desktop.
        Event<HWND> Activated;

        Event<HWND> DragStarted;
        Event<HWND> DragEnded;

    private:
        void OnEvent(DWORD event, HWND hwnd);
        void TryAdopt(HWND hwnd);
        void Forget(HWND hwnd);
        void ApplyCorners(ManagedWindow& window);
        void UpdateState(ManagedWindow& window);
        Core::Disposition DispositionOf(HWND hwnd, Core::WindowFacts const& facts) const;
        static WindowState ReadState(HWND hwnd);

        Core::AppSettings const& m_settings;
        Native::VirtualDesktops m_desktops;
        std::map<HWND, ManagedWindow> m_windows;
        std::map<HWND, Core::Disposition> m_chosen;

        /// --only-title: see WindowTracker.cpp.
        std::optional<std::wstring> m_onlyTitle;

        /// Last, so its hooks are gone before anything they call into.
        std::unique_ptr<Native::WinEvents> m_events;
    };
}
