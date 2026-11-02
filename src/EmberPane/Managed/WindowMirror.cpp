#include "pch.h"
#include "Managed/WindowMirror.h"

namespace EmberPane::Managed
{
    namespace
    {
        std::wstring Text(Link::Message const& item, wchar_t const* name)
        {
            auto value = item.Find(name);
            return value ? value->AsString().value_or(L"") : L"";
        }
    }

    void WindowMirror::Replace(Link::Message const& message)
    {
        auto list = message.Find(L"windows");
        if (!list) return;

        m_windows.clear();
        for (auto const& item : list->Items())
            m_windows.push_back({ Text(item, L"app"), Text(item, L"exe") });

        WindowsChanged();
    }
}
