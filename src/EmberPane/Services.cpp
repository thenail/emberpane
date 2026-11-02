#include "pch.h"
#include "Services.h"

namespace EmberPane
{
    namespace
    {
        Services* g_services = nullptr;
    }

    Services& Services::Get()
    {
        return *g_services;
    }

    void Services::Set(Services* services)
    {
        g_services = services;
    }
}
