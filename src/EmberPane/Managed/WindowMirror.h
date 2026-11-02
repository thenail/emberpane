#pragma once

#include <functional>
#include <map>
#include <string>
#include <vector>
#include "Event.h"
#include "Link.h"

namespace EmberPane::Managed
{
    /// An app with a window open, for choosing one in Settings.
    struct WindowItem
    {
        std::wstring App;
        std::wstring Exe;
    };

    /// The host's window list, as the UI process sees it.
    class WindowMirror
    {
    public:
        std::vector<WindowItem> const& Windows() const { return m_windows; }

        /// From the host: a new list, as a "windows" or "state" message has it.
        void Replace(Link::Message const& message);

        /// The list was replaced.
        Event<> WindowsChanged;

    private:
        std::vector<WindowItem> m_windows;
    };
}