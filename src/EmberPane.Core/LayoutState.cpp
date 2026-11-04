#include "pch.h"
#include "LayoutState.h"

#include <ShlObj.h>

namespace EmberPane::Core
{
    namespace
    {
        constexpr int Version = 1;

        std::wstring LocalAppData()
        {
            PWSTR path = nullptr;
            std::wstring result;
            if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &path)))
                result = path;
            CoTaskMemFree(path);
            return result;
        }

        std::wstring Text(Json::Value const& object, wchar_t const* name)
        {
            auto value = object.Find(name);
            return value ? value->AsString().value_or(L"") : L"";
        }

        double Number(Json::Value const& object, wchar_t const* name)
        {
            auto value = object.Find(name);
            return value ? value->AsNumber().value_or(0) : 0;
        }

        Json::Value WindowsToJson(std::vector<SavedWindow> const& windows)
        {
            auto list = Json::Value::MakeArray();
            for (auto const& window : windows)
            {
                auto item = Json::Value::MakeObject();
                item.Set(L"Window", static_cast<double>(window.Window));
                item.Set(L"Process", static_cast<double>(window.Process));
                item.Set(L"Area", static_cast<double>(window.Area));
                list.Append(std::move(item));
            }
            return list;
        }

        std::vector<SavedWindow> WindowsFromJson(Json::Value const* list)
        {
            std::vector<SavedWindow> windows;
            if (!list) return windows;

            for (auto const& item : list->Items())
            {
                SavedWindow window;
                window.Window = static_cast<std::uint64_t>(Number(item, L"Window"));
                window.Process = static_cast<std::uint32_t>(Number(item, L"Process"));
                window.Area = static_cast<WindowId>(Number(item, L"Area"));
                if (window.Window && window.Area) windows.push_back(window);
            }
            return windows;
        }
    }

    std::wstring LayoutState::DefaultPath()
    {
        return LocalAppData() + L"\\EmberPane\\layout.json";
    }

    Json::Value LayoutState::ToJson() const
    {
        auto root = Json::Value::MakeObject();
        root.Set(L"Version", Version);
        root.Set(L"Preset", static_cast<int>(Preset));
        root.Set(L"Boot", static_cast<double>(Boot));

        auto layouts = Json::Value::MakeArray();
        for (auto const& layout : Layouts)
        {
            auto item = Json::Value::MakeObject();
            item.Set(L"Monitor", layout.Monitor);
            item.Set(L"Desktop", layout.Desktop);

            auto ratios = Json::Value::MakeArray();
            for (double ratio : layout.Ratios)
                ratios.Append(ratio);
            item.Set(L"Ratios", std::move(ratios));

            item.Set(L"Members", WindowsToJson(layout.Members));
            layouts.Append(std::move(item));
        }
        root.Set(L"Layouts", std::move(layouts));
        root.Set(L"Returning", WindowsToJson(Returning));
        return root;
    }

    std::optional<LayoutState> LayoutState::FromJson(Json::Value const& root)
    {
        if (Number(root, L"Version") != Version) return std::nullopt;

        LayoutState state;
        state.Preset = static_cast<LayoutPreset>(static_cast<int>(Number(root, L"Preset")));
        state.Boot = static_cast<std::int64_t>(Number(root, L"Boot"));

        if (auto layouts = root.Find(L"Layouts"))
        {
            for (auto const& item : layouts->Items())
            {
                SavedLayout layout;
                layout.Monitor = Text(item, L"Monitor");
                layout.Desktop = Text(item, L"Desktop");
                if (layout.Monitor.empty()) continue;

                if (auto ratios = item.Find(L"Ratios"))
                    for (auto const& ratio : ratios->Items())
                        layout.Ratios.push_back(ratio.AsNumber().value_or(0));

                layout.Members = WindowsFromJson(item.Find(L"Members"));
                state.Layouts.push_back(std::move(layout));
            }
        }

        state.Returning = WindowsFromJson(root.Find(L"Returning"));
        return state;
    }
}
