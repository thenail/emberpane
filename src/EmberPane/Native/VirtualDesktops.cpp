#include "pch.h"
#include "Native/VirtualDesktops.h"

namespace EmberPane::Native
{
    namespace
    {
        /// CLSID_VirtualDesktopManager, which shobjidl_core.h declares the
        /// interface of but not the class.
        constexpr CLSID ManagerClass = { 0xaa509086, 0x5ca9, 0x4c25, { 0x8f, 0x95, 0x58, 0x9d, 0x3c, 0x07, 0xb4, 0x8a } };
    }

    IVirtualDesktopManager* VirtualDesktops::Manager()
    {
        if (!m_manager)
            CoCreateInstance(ManagerClass, nullptr, CLSCTX_ALL, IID_PPV_ARGS(m_manager.put()));
        return m_manager.get();
    }

    std::wstring VirtualDesktops::DesktopOf(HWND hwnd)
    {
        auto manager = Manager();
        if (!manager) return {};

        GUID id{};
        if (FAILED(manager->GetWindowDesktopId(hwnd, &id)) || id == GUID{})
            return {};

        wchar_t text[64]{};
        StringFromGUID2(id, text, 64);
        return text;
    }

    bool VirtualDesktops::IsOnCurrent(HWND hwnd)
    {
        auto manager = Manager();
        if (!manager) return true;

        BOOL on = TRUE;
        HRESULT result = manager->IsWindowOnCurrentVirtualDesktop(hwnd, &on);

        if (HRESULT_FACILITY(result) == FACILITY_RPC || result == RPC_E_DISCONNECTED ||
            result == HRESULT_FROM_WIN32(RPC_S_SERVER_UNAVAILABLE))
            m_manager = nullptr;

        return FAILED(result) || on;
    }
}
