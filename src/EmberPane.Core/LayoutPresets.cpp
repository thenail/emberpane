#include "pch.h"
#include "LayoutPresets.h"

namespace EmberPane::Core
{
    std::vector<LayoutPreset> AllPresets()
    {
        return { LayoutPreset::OneAtATime, LayoutPreset::TwoColumns, LayoutPreset::ThreeColumns,
                 LayoutPreset::LargeAndSmall, LayoutPreset::LargeAndTwoSmall, LayoutPreset::FourAreas };
    }

    LayoutTree MakeLayout(LayoutPreset preset)
    {
        constexpr double Third = 1.0 / 3.0;
        constexpr double Large = 0.62;

        LayoutTree tree;
        tree.AddFirst(1);

        switch (preset)
        {
        case LayoutPreset::OneAtATime:
            break;

        case LayoutPreset::ThreeColumns:
            tree.InsertBeside(2, 1, Edge::Right, Third);
            tree.InsertBeside(3, 2, Edge::Right);
            break;

        case LayoutPreset::LargeAndSmall:
            tree.InsertBeside(2, 1, Edge::Right, Large);
            break;

        case LayoutPreset::LargeAndTwoSmall:
            tree.InsertBeside(2, 1, Edge::Right, Large);
            tree.InsertBeside(3, 2, Edge::Bottom);
            break;

        case LayoutPreset::FourAreas:
            tree.InsertBeside(2, 1, Edge::Right);
            tree.InsertBeside(3, 1, Edge::Bottom);
            tree.InsertBeside(4, 2, Edge::Bottom);
            break;

        default:
            tree.InsertBeside(2, 1, Edge::Right);
            break;
        }

        return tree;
    }

    std::vector<Rect> PictureOf(LayoutPreset preset)
    {
        return PictureOf(MakeLayout(preset));
    }

    std::vector<Rect> PictureOf(LayoutTree const& areas)
    {
        std::vector<Rect> picture;
        for (auto const& tile : areas.Arrange({ 0, 0, 1000, 1000 }, 0, {}))
            picture.push_back(tile.Bounds);
        return picture;
    }

    bool IsResized(LayoutTree const& areas, LayoutPreset preset)
    {
        // A tenth of a percent is rounding, or a drag let go where it began.
        constexpr int Sliver = 2;

        auto now = PictureOf(areas);
        auto was = PictureOf(preset);
        if (now.size() != was.size()) return true;

        auto within = [](int a, int b) { return std::abs(a - b) <= Sliver; };
        for (size_t i = 0; i < now.size(); ++i)
        {
            if (!within(now[i].Left, was[i].Left) || !within(now[i].Top, was[i].Top) ||
                !within(now[i].Right, was[i].Right) || !within(now[i].Bottom, was[i].Bottom))
                return true;
        }
        return false;
    }
}
