#include "pch.h"
#include "Native/Monitors.h"

namespace EmberPane::Native
{
    namespace
    {
        /// "\\.\DISPLAY2" to the device path of the monitor it drives.
        std::map<std::wstring, std::wstring> DevicePaths()
        {
            std::map<std::wstring, std::wstring> found;

            UINT32 pathCount = 0, modeCount = 0;
            if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &pathCount, &modeCount) != ERROR_SUCCESS)
                return found;

            std::vector<DISPLAYCONFIG_PATH_INFO> paths(pathCount);
            std::vector<DISPLAYCONFIG_MODE_INFO> modes(modeCount);
            if (QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &pathCount, paths.data(), &modeCount, modes.data(),
                                   nullptr) != ERROR_SUCCESS)
                return found;

            paths.resize(pathCount);
            for (auto const& path : paths)
            {
                DISPLAYCONFIG_SOURCE_DEVICE_NAME source{};
                source.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
                source.header.size = sizeof(source);
                source.header.adapterId = path.sourceInfo.adapterId;
                source.header.id = path.sourceInfo.id;

                DISPLAYCONFIG_TARGET_DEVICE_NAME target{};
                target.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME;
                target.header.size = sizeof(target);
                target.header.adapterId = path.targetInfo.adapterId;
                target.header.id = path.targetInfo.id;

                if (DisplayConfigGetDeviceInfo(&source.header) == ERROR_SUCCESS &&
                    DisplayConfigGetDeviceInfo(&target.header) == ERROR_SUCCESS)
                    found.try_emplace(source.viewGdiDeviceName, target.monitorDevicePath);
            }

            return found;
        }

        /// A device path minus its interface-class suffix.
        std::wstring KeyOf(std::wstring const& devicePath)
        {
            auto suffix = devicePath.find(L"#{");
            return suffix == std::wstring::npos ? devicePath : devicePath.substr(0, suffix);
        }

        BOOL CALLBACK Collect(HMONITOR handle, HDC, LPRECT, LPARAM list)
        {
            reinterpret_cast<std::vector<HMONITOR>*>(list)->push_back(handle);
            return TRUE;
        }
    }

    std::vector<Monitor> ListMonitors()
    {
        std::vector<HMONITOR> handles;
        EnumDisplayMonitors(nullptr, nullptr, &Collect, reinterpret_cast<LPARAM>(&handles));

        auto paths = DevicePaths();

        std::vector<Monitor> monitors;
        for (auto handle : handles)
        {
            MONITORINFOEXW info{};
            info.cbSize = sizeof(info);
            if (!GetMonitorInfoW(handle, &info)) continue;

            UINT dpiX = 96, dpiY = 96;
            GetDpiForMonitor(handle, MDT_EFFECTIVE_DPI, &dpiX, &dpiY);

            Monitor monitor;
            auto path = paths.find(info.szDevice);
            monitor.Key = path != paths.end() ? KeyOf(path->second) : std::wstring(info.szDevice);
            monitor.Handle = handle;
            monitor.Bounds = info.rcMonitor;
            monitor.Work = info.rcWork;
            monitor.Dpi = dpiX;
            monitor.Primary = (info.dwFlags & MONITORINFOF_PRIMARY) != 0;
            monitors.push_back(std::move(monitor));
        }
        return monitors;
    }

    std::wstring SignatureOf(std::vector<Monitor> const& monitors)
    {
        std::vector<std::wstring> lines;
        for (auto const& m : monitors)
        {
            wchar_t line[128];
            swprintf_s(line, L"|%d,%d,%d,%d|%u\n", m.Work.left, m.Work.top, m.Work.right, m.Work.bottom, m.Dpi);
            lines.push_back(m.Key + line);
        }
        std::sort(lines.begin(), lines.end());

        std::wstring signature;
        for (auto const& line : lines)
            signature += line;
        return signature;
    }
}
