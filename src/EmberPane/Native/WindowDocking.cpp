#include "pch.h"
#include "Native/WindowDocking.h"

namespace EmberPane::Native::WindowDocking
{
    bool IsOn()
    {
        BOOL on = TRUE;
        SystemParametersInfoW(SPI_GETDOCKMOVING, 0, &on, 0);
        return on != FALSE;
    }

    bool Preference()
    {
        wchar_t value[8]{};
        DWORD size = sizeof(value);
        LSTATUS status = RegGetValueW(HKEY_CURRENT_USER, LR"(Control Panel\Desktop)", L"DockMoving", RRF_RT_REG_SZ,
                                      nullptr, value, &size);

        // Windows' default is on.
        return status != ERROR_SUCCESS || wcscmp(value, L"0") != 0;
    }

    bool Set(bool on)
    {
        // The value goes in uiParam, whatever the documentation says of
        // pvParam: given there, it is ignored and the zero here turns docking
        // off. No SPIF_UPDATEINIFILE, so nothing reaches the profile.
        SystemParametersInfoW(SPI_SETDOCKMOVING, on ? TRUE : FALSE, nullptr, 0);
        return IsOn() == on;
    }
}
