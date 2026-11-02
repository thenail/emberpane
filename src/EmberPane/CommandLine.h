#pragma once

#include <string_view>

namespace EmberPane::CommandLine
{
    /// Whether the command line carries this exact switch, e.g. L"--show".
    inline bool Has(std::wstring_view flag)
    {
        int argc = 0;
        auto argv = CommandLineToArgvW(GetCommandLineW(), &argc);
        bool found = false;

        for (int i = 1; i < argc && !found; ++i)
            found = _wcsicmp(argv[i], std::wstring(flag).c_str()) == 0;

        LocalFree(argv);
        return found;
    }

    /// The value of a "--name=value" switch, if present.
    inline std::optional<std::wstring> Value(std::wstring_view name)
    {
        int argc = 0;
        auto argv = CommandLineToArgvW(GetCommandLineW(), &argc);
        std::optional<std::wstring> value;

        std::wstring prefix = std::wstring(name) + L"=";
        for (int i = 1; i < argc && !value; ++i)
            if (_wcsnicmp(argv[i], prefix.c_str(), prefix.size()) == 0)
                value = std::wstring(argv[i] + prefix.size());

        LocalFree(argv);
        return value;
    }
}
