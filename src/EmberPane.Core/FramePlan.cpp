#include "pch.h"
#include "FramePlan.h"

namespace EmberPane::Core
{
    FramePlan PlanFrame(int color, int inner, int border, int gap, bool clear)
    {
        int start = inner - border;
        int width = std::max(color, -start);
        int room = gap / 2;

        if (start + width > room)
            width = room - start;

        // No room for it, or nothing Windows' own border does not do.
        if (width < 1 || (inner == 0 && start + width <= 0))
            return {};

        FramePlan plan;
        plan.Layered = true;
        plan.DarkBorder = inner > 0 && !clear;
        plan.NoBorder = inner > 0 && clear;
        plan.ColorFrom = start;
        plan.ColorTo = start + width;

        if (start > 0 && !clear)
        {
            plan.DarkFrom = -border;
            plan.DarkTo = start;
        }

        return plan;
    }
}
