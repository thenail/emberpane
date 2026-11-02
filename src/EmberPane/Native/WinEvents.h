#pragma once

#include <deque>
#include <functional>

namespace EmberPane::Native
{
    /// Windows appearing, going and changing, from anywhere on the desktop.
    ///
    /// Out of context: nothing is injected into other processes. One hook per
    /// contiguous range of events, since a single hook across the whole range
    /// would receive every event on the system.
    ///
    /// EVENT_OBJECT_LOCATIONCHANGE is left out on purpose. It also fires for
    /// the mouse cursor and the caret, which would wake the tray on every
    /// mouse movement.
    ///
    /// The callback only filters and queues; the work happens from the
    /// dispatcher queue, so nothing runs inside a hook and a handler cannot
    /// be re-entered by the events it causes.
    class WinEvents
    {
    public:
        WinEvents(winrt::Windows::System::DispatcherQueue const& dispatcher,
                  std::function<void(DWORD event, HWND hwnd)> received);
        ~WinEvents();

        WinEvents(WinEvents const&) = delete;
        WinEvents& operator=(WinEvents const&) = delete;

    private:
        static void CALLBACK Callback(HWINEVENTHOOK hook, DWORD event, HWND hwnd, LONG object, LONG child,
                                      DWORD thread, DWORD time);
        void Drain();

        /// The API has no context parameter. There is one of these.
        static WinEvents* s_instance;

        winrt::Windows::System::DispatcherQueue m_dispatcher;
        std::function<void(DWORD, HWND)> m_received;
        std::vector<HWINEVENTHOOK> m_hooks;
        std::deque<std::pair<DWORD, HWND>> m_inbox;
        bool m_drainQueued{ false };
    };
}
