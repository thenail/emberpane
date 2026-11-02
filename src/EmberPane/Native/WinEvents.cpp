#include "pch.h"
#include "Native/WinEvents.h"

namespace EmberPane::Native
{
    WinEvents* WinEvents::s_instance = nullptr;

    WinEvents::WinEvents(winrt::Windows::System::DispatcherQueue const& dispatcher,
                         std::function<void(DWORD, HWND)> received)
        : m_dispatcher(dispatcher), m_received(std::move(received))
    {
        s_instance = this;

        constexpr std::pair<DWORD, DWORD> Ranges[] = {
            { EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND },
            { EVENT_SYSTEM_MOVESIZESTART, EVENT_SYSTEM_MOVESIZEEND },
            { EVENT_SYSTEM_MINIMIZESTART, EVENT_SYSTEM_MINIMIZEEND },
            { EVENT_OBJECT_DESTROY, EVENT_OBJECT_HIDE },
            { EVENT_OBJECT_CLOAKED, EVENT_OBJECT_UNCLOAKED },
        };

        for (auto [first, last] : Ranges)
            if (auto hook = SetWinEventHook(first, last, nullptr, &Callback, 0, 0,
                                            WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS))
                m_hooks.push_back(hook);
    }

    WinEvents::~WinEvents()
    {
        for (auto hook : m_hooks)
            UnhookWinEvent(hook);
        s_instance = nullptr;
    }

    void CALLBACK WinEvents::Callback(HWINEVENTHOOK, DWORD event, HWND hwnd, LONG object, LONG child, DWORD, DWORD)
    {
        if (!s_instance || !hwnd || object != OBJID_WINDOW || child != CHILDID_SELF)
            return;

        auto self = s_instance;
        self->m_inbox.emplace_back(event, hwnd);

        if (!self->m_drainQueued)
            self->m_drainQueued = self->m_dispatcher.TryEnqueue([] { if (s_instance) s_instance->Drain(); });
    }

    void WinEvents::Drain()
    {
        m_drainQueued = false;

        // One at a time off the front: a handler may cause more events, which
        // join the back of the queue in order.
        while (!m_inbox.empty())
        {
            auto [event, hwnd] = m_inbox.front();
            m_inbox.pop_front();
            m_received(event, hwnd);
        }
    }
}
