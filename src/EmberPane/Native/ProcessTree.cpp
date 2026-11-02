#include "pch.h"
#include "Native/ProcessTree.h"

#include <TlHelp32.h>

#pragma comment(lib, "version.lib")

namespace EmberPane::Native::ProcessTree
{
    namespace
    {
        std::uint64_t ToTicks(FILETIME const& time)
        {
            return (static_cast<std::uint64_t>(time.dwHighDateTime) << 32) | time.dwLowDateTime;
        }

        struct OwnerSearch
        {
            DWORD Frame;
            DWORD Found;
        };

        std::wstring FileStem(std::wstring const& path)
        {
            auto name = path.substr(path.find_last_of(L'\\') + 1);
            auto dot = name.find_last_of(L'.');
            return dot == std::wstring::npos ? name : name.substr(0, dot);
        }
    }

    std::vector<Core::ProcessEntry> Snapshot()
    {
        std::vector<Core::ProcessEntry> entries;

        HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snapshot == INVALID_HANDLE_VALUE) return entries;

        PROCESSENTRY32W entry{ sizeof(entry) };
        if (Process32FirstW(snapshot, &entry))
        {
            do
            {
                entries.push_back({ entry.th32ProcessID, entry.th32ParentProcessID, entry.szExeFile });
            } while (Process32NextW(snapshot, &entry));
        }

        CloseHandle(snapshot);
        return entries;
    }

    DWORD OwnerOf(HWND hwnd)
    {
        DWORD pid = 0;
        GetWindowThreadProcessId(hwnd, &pid);

        wchar_t name[64]{};
        GetClassNameW(hwnd, name, 64);
        if (wcscmp(name, L"ApplicationFrameWindow") != 0)
            return pid;

        OwnerSearch search{ pid, pid };
        EnumChildWindows(hwnd, [](HWND child, LPARAM context)
        {
            auto search = reinterpret_cast<OwnerSearch*>(context);

            DWORD owner = 0;
            GetWindowThreadProcessId(child, &owner);
            if (owner == search->Frame) return TRUE;

            search->Found = owner;
            return FALSE;
        }, reinterpret_cast<LPARAM>(&search));

        return search.Found;
    }

    std::wstring ExePath(DWORD pid)
    {
        HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        if (!process) return {};

        std::wstring path(1024, L'\0');
        DWORD length = static_cast<DWORD>(path.size());
        bool ok = QueryFullProcessImageNameW(process, 0, path.data(), &length) != 0;
        CloseHandle(process);

        path.resize(ok ? length : 0);
        return path;
    }

    std::wstring AppName(std::wstring const& exePath)
    {
        DWORD size = GetFileVersionInfoSizeW(exePath.c_str(), nullptr);
        if (size)
        {
            std::vector<BYTE> data(size);

            struct Translation { WORD Language; WORD CodePage; };
            Translation* translations{};
            UINT length{};

            if (GetFileVersionInfoW(exePath.c_str(), 0, size, data.data()) &&
                VerQueryValueW(data.data(), L"\\VarFileInfo\\Translation", reinterpret_cast<void**>(&translations),
                               &length) &&
                length >= sizeof(Translation))
            {
                wchar_t key[64];
                swprintf_s(key, L"\\StringFileInfo\\%04x%04x\\FileDescription", translations[0].Language,
                           translations[0].CodePage);

                wchar_t* description{};
                if (VerQueryValueW(data.data(), key, reinterpret_cast<void**>(&description), &length) && length > 1)
                    return description;
            }
        }

        return FileStem(exePath);
    }

    std::optional<Times> TimesOf(HANDLE process)
    {
        FILETIME created{}, exited{}, kernel{}, user{};
        if (!GetProcessTimes(process, &created, &exited, &kernel, &user))
            return std::nullopt;

        return Times{ ToTicks(created), ToTicks(kernel) + ToTicks(user) };
    }
}
