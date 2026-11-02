#pragma once

#include <functional>
#include <utility>
#include <vector>

namespace EmberPane
{
    /// A multicast callback for plain C++ objects: the model classes here are
    /// not WinRT types, so they cannot use winrt::event.
    ///
    /// Raising works on a copy of the handler list, so a handler may add or
    /// remove handlers - including itself - without upsetting the loop.
    /// Everything that raises these does so on its process's main thread.
    template <typename... Args>
    class Event
    {
    public:
        using Handler = std::function<void(Args...)>;

        int Add(Handler handler)
        {
            int token = m_next++;
            m_handlers.emplace_back(token, std::move(handler));
            return token;
        }

        void Remove(int token)
        {
            std::erase_if(m_handlers, [token](auto const& entry) { return entry.first == token; });
        }

        void operator()(Args... args) const
        {
            auto handlers = m_handlers;
            for (auto const& [token, handler] : handlers)
                handler(args...);
        }

    private:
        std::vector<std::pair<int, Handler>> m_handlers;
        int m_next{ 1 };
    };
}
