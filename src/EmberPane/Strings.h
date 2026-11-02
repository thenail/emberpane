#pragma once

#include <initializer_list>
#include <string>
#include <string_view>

/// Translated text for what the code builds itself; what XAML shows as-is
/// comes through x:Uid instead. Both read Strings\<language>\Resources.resw,
/// and Windows picks the language from the user's display-language list,
/// falling back to English.
namespace EmberPane::Strings
{
    /// Follows the language Windows' own menus are in, or --lang. Both
    /// processes call it first thing: everything after resolves its strings
    /// as it loads.
    void UseDisplayLanguage();

    /// The string for a key; the key itself if there is none, so a missing
    /// translation shows up on screen instead of as a blank.
    std::wstring Get(wchar_t const* key);

    /// Get, with {0}, {1}, ... replaced by the arguments. Placeholders rather
    /// than concatenation, because word order differs between languages.
    std::wstring Format(wchar_t const* key, std::initializer_list<std::wstring_view> args);
}
