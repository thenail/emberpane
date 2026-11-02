#include "pch.h"
#include "Strings.h"
#include "CommandLine.h"
#include "Trace.h"

#include <winrt/Microsoft.Windows.ApplicationModel.Resources.h>
#include <winrt/Microsoft.Windows.Globalization.h>

namespace EmberPane::Strings
{
    namespace
    {
        using winrt::Microsoft::Windows::ApplicationModel::Resources::ResourceLoader;

        /// One loader for the process. It reads EmberPane.pri beside the
        /// exe, which the build compiles from the .resw files.
        ResourceLoader const& Loader()
        {
            static ResourceLoader loader = []
            {
                try
                {
                    return ResourceLoader();
                }
                catch (winrt::hresult_error const& e)
                {
                    Trace::Log(L"strings: no resource file (" + std::wstring(e.message()) + L")");
                    return ResourceLoader{ nullptr };
                }
            }();
            return loader;
        }
    }

    void UseDisplayLanguage()
    {
        // The language Windows' own menus are in. Left alone, the resources
        // would follow the preferred-language list instead, which can differ
        // (English Windows, Swedish list) and leave the app looking foreign
        // next to everything else. Where there is no translation, the list
        // and then English are still the fallbacks.
        //
        // --lang=de: another language, for checking a translation without
        // switching the whole machine.
        auto language = CommandLine::Value(L"--lang");
        if (!language)
        {
            wchar_t name[LOCALE_NAME_MAX_LENGTH]{};
            if (LCIDToLocaleName(GetUserDefaultUILanguage(), name, LOCALE_NAME_MAX_LENGTH, 0) > 0)
                language = name;
        }
        if (language)
        {
            Trace::Log(L"language: " + *language);
            winrt::Microsoft::Windows::Globalization::ApplicationLanguages::PrimaryLanguageOverride(*language);
        }
    }

    std::wstring Get(wchar_t const* key)
    {
        if (auto const& loader = Loader())
        {
            try
            {
                auto value = loader.GetString(key);
                if (!value.empty())
                    return std::wstring(value);
            }
            catch (winrt::hresult_error const&)
            {
            }
        }

        Trace::Log(std::wstring(L"strings: missing ") + key);
        return key;
    }

    std::wstring Format(wchar_t const* key, std::initializer_list<std::wstring_view> args)
    {
        auto text = Get(key);

        size_t index = 0;
        for (auto arg : args)
        {
            auto token = L"{" + std::to_wstring(index++) + L"}";
            for (size_t at = text.find(token); at != std::wstring::npos; at = text.find(token, at + arg.size()))
                text.replace(at, token.size(), arg);
        }
        return text;
    }
}
