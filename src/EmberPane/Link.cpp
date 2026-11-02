#include "pch.h"
#include "Link.h"
#include "Trace.h"

namespace EmberPane::Link
{
    namespace
    {
        /// Marks a WM_COPYDATA as ours, so a stray one from anything else that
        /// found the window is ignored rather than parsed.
        constexpr ULONG_PTR Magic = 0x45504C4B; // 'EPLK'

        constexpr UINT DrainMessage = WM_APP + 1;

        constexpr UINT SendTimeoutMs = 1000;
    }

    Message Make(wchar_t const* type)
    {
        auto message = Message::MakeObject();
        message.Set(L"type", type);
        return message;
    }

    std::wstring TypeOf(Message const& message)
    {
        auto type = message.Find(L"type");
        return type ? type->AsString().value_or(L"") : L"";
    }

    Endpoint::Endpoint(wchar_t const* className, std::function<void(Message const&)> received)
        : m_received(std::move(received))
    {
        WNDCLASSEXW wc{ sizeof(wc) };
        wc.lpfnWndProc = WndProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = className;
        RegisterClassExW(&wc);

        m_hwnd = CreateWindowExW(0, className, L"EmberPane", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, wc.hInstance,
                                 this);
    }

    Endpoint::~Endpoint()
    {
        if (m_hwnd)
        {
            SetWindowLongPtrW(m_hwnd, GWLP_USERDATA, 0);
            DestroyWindow(m_hwnd);
        }
    }

    bool Endpoint::Send(HWND to, Message const& message) const
    {
        if (!to) return false;

        auto bytes = Core::Json::ToUtf8(Core::Json::Write(message));
        COPYDATASTRUCT data{ Magic, static_cast<DWORD>(bytes.size()), bytes.data() };

        DWORD_PTR result = 0;
        bool sent = SendMessageTimeoutW(to, WM_COPYDATA, reinterpret_cast<WPARAM>(m_hwnd),
                                        reinterpret_cast<LPARAM>(&data), SMTO_ABORTIFHUNG, SendTimeoutMs,
                                        &result) != 0;
        if (!sent || !result)
            Trace::Log(L"link: could not send " + TypeOf(message));
        return sent && result;
    }

    void Endpoint::Drain()
    {
        m_drainPosted = false;

        // One at a time off the front: a handler may send, and a reply that
        // lands meanwhile joins the back of the queue in order.
        while (!m_inbox.empty())
        {
            auto message = std::move(m_inbox.front());
            m_inbox.pop_front();
            if (m_received) m_received(message);
        }
    }

    LRESULT CALLBACK Endpoint::WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
    {
        if (message == WM_NCCREATE)
        {
            auto create = reinterpret_cast<CREATESTRUCTW*>(lParam);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
        }

        auto self = reinterpret_cast<Endpoint*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (!self)
            return DefWindowProcW(hwnd, message, wParam, lParam);

        if (message == WM_COPYDATA)
        {
            auto data = reinterpret_cast<COPYDATASTRUCT const*>(lParam);
            if (!data || data->dwData != Magic || !data->lpData) return FALSE;

            auto text = Core::Json::FromUtf8({ static_cast<char const*>(data->lpData), data->cbData });
            auto parsed = Core::Json::Parse(text);
            if (!parsed || parsed->GetKind() != Core::Json::Value::Kind::Object) return FALSE;

            self->m_inbox.push_back(std::move(*parsed));
            if (!self->m_drainPosted)
                self->m_drainPosted = PostMessageW(hwnd, DrainMessage, 0, 0) != 0;
            return TRUE;
        }

        if (message == DrainMessage)
        {
            self->Drain();
            return 0;
        }

        return DefWindowProcW(hwnd, message, wParam, lParam);
    }
}
