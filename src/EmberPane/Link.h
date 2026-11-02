#pragma once

#include <deque>
#include <functional>
#include "Json.h"

/// The line between the two processes EmberPane runs as.
///
/// The host - what sits in the tray all day - is plain Win32: the windows it
/// manages, the tray icon. The panel and Settings are WinUI, and WinUI
/// cannot be unloaded once it is in a process, so they run in a process of
/// their own (EmberPane.exe --ui) that the host starts on a click and that
/// exits as soon as both windows are closed. That keeps the resident process
/// at a few megabytes instead of the tens WinUI costs.
///
/// Messages are JSON objects with a "type", passed as WM_COPYDATA between two
/// message-only windows. Receiving one only queues it and posts a reminder to
/// itself; it is handled once the message loop is back at the top. Nothing
/// runs inside the other side's SendMessage, so neither side can be re-entered
/// halfway through something - a slider handler, say, or a list being walked.
namespace EmberPane::Link
{
    /// The host's window class, for a UI started by hand to find it.
    inline constexpr wchar_t HostClass[] = L"EmberPane.Host";
    inline constexpr wchar_t UiClass[] = L"EmberPane.Ui";

    /// Message types.
    namespace Type
    {
        // UI -> host
        inline constexpr wchar_t Hello[] = L"hello";           // { hwnd }
        inline constexpr wchar_t SaveSettings[] = L"saveSettings"; // { settings }
        inline constexpr wchar_t Quit[] = L"quit";

        // host -> UI
        inline constexpr wchar_t State[] = L"state";           // { settings, status, windows }
        inline constexpr wchar_t Windows[] = L"windows";       // { windows: [{ app, exe }] }
        inline constexpr wchar_t Status[] = L"status";         // { text }
        inline constexpr wchar_t Settings[] = L"settings";     // { settings }
        inline constexpr wchar_t Toggle[] = L"toggle";
        inline constexpr wchar_t OpenSettings[] = L"openSettings";
        // and Quit, the other way
    }

    using Message = Core::Json::Value;

    /// A message of the given type, for the caller to add fields to.
    Message Make(wchar_t const* type);

    /// The message's type; empty when it has none.
    std::wstring TypeOf(Message const& message);

    /// One side's window: sends to the other side's, and hands what arrives
    /// to the callback on this thread, from the message loop.
    class Endpoint
    {
    public:
        Endpoint(wchar_t const* className, std::function<void(Message const&)> received);
        ~Endpoint();

        Endpoint(Endpoint const&) = delete;
        Endpoint& operator=(Endpoint const&) = delete;

        HWND Hwnd() const { return m_hwnd; }

        /// False when the other side is gone or did not take it within a
        /// second - a hung UI must not freeze the tray.
        bool Send(HWND to, Message const& message) const;

    private:
        static LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
        void Drain();

        HWND m_hwnd{};
        std::function<void(Message const&)> m_received;
        std::deque<Message> m_inbox;
        bool m_drainPosted{ false };
    };
}
