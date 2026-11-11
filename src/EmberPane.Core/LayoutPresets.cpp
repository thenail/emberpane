#include "pch.h"
#include "LayoutPresets.h"

namespace EmberPane::Core
{
    namespace
    {
        constexpr double Third = 1.0 / 3.0;

        /// The large area's share, where a preset has one.
        constexpr double Large = 0.62;

        /// On the picture's square: an edge this close is the same edge. Snap's
        /// places are a twelfth apart or more (a quarter beside a third), and
        /// Large is this close to Snap's two thirds.
        constexpr int SamePlace = 60;

        LayoutTree Build(LayoutPreset preset, double large)
        {
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
                tree.InsertBeside(2, 1, Edge::Right, large);
                break;

            case LayoutPreset::LargeAndTwoSmall:
                tree.InsertBeside(2, 1, Edge::Right, large);
                tree.InsertBeside(3, 2, Edge::Bottom);
                break;

            case LayoutPreset::FourAreas:
                tree.InsertBeside(2, 1, Edge::Right);
                tree.InsertBeside(3, 1, Edge::Bottom);
                tree.InsertBeside(4, 2, Edge::Bottom);
                break;

            case LayoutPreset::LargeInTheMiddle:
                // The middle is area 1, as the large area is elsewhere, so the
                // window in use is dealt to it: a quarter to its left, then a
                // third of the rest to its right, a quarter of the whole.
                tree.InsertBeside(2, 1, Edge::Left, 0.25);
                tree.InsertBeside(3, 1, Edge::Right, 2.0 / 3.0);
                break;

            default:
                tree.InsertBeside(2, 1, Edge::Right);
                break;
            }

            return tree;
        }

        /// The preset as Windows' Snap layouts have it: the same areas, by the
        /// same numbers, at Windows' sizes. None for one at a time, which is
        /// maximizing, not snapping.
        std::optional<LayoutTree> AsSnapHasIt(LayoutPreset preset)
        {
            switch (preset)
            {
            case LayoutPreset::OneAtATime: return std::nullopt;
            case LayoutPreset::LargeAndSmall: return Build(preset, 2.0 / 3.0);
            case LayoutPreset::LargeAndTwoSmall: return Build(preset, 0.5);
            default: return Build(preset, Large);
            }
        }

        std::vector<Tile> OnSquare(LayoutTree const& areas)
        {
            return areas.Arrange({ 0, 0, 1000, 1000 }, 0, {});
        }

        /// The furthest any edge is from its counterpart.
        int Distance(Rect const& a, Rect const& b)
        {
            return std::max({ std::abs(a.Left - b.Left), std::abs(a.Top - b.Top), std::abs(a.Right - b.Right),
                              std::abs(a.Bottom - b.Bottom) });
        }

        long long Overlap(Rect const& a, Rect const& b)
        {
            long long width = std::min(a.Right, b.Right) - std::max(a.Left, b.Left);
            long long height = std::min(a.Bottom, b.Bottom) - std::max(a.Top, b.Top);
            return width > 0 && height > 0 ? width * height : 0;
        }
    }

    std::vector<LayoutPreset> AllPresets()
    {
        return { LayoutPreset::OneAtATime, LayoutPreset::TwoColumns, LayoutPreset::ThreeColumns,
                 LayoutPreset::LargeAndSmall, LayoutPreset::LargeAndTwoSmall, LayoutPreset::FourAreas,
                 LayoutPreset::LargeInTheMiddle };
    }

    LayoutTree MakeLayout(LayoutPreset preset)
    {
        return Build(preset, Large);
    }

    std::vector<Rect> PictureOf(LayoutPreset preset)
    {
        return PictureOf(MakeLayout(preset));
    }

    std::vector<Rect> PictureOf(LayoutTree const& areas)
    {
        std::vector<Rect> picture;
        for (auto const& tile : OnSquare(areas))
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

    std::optional<SnapTarget> MatchSnap(Rect const& snapped, Rect const& work, LayoutPreset current,
                                        LayoutTree const& areas)
    {
        if (work.Width() <= 0 || work.Height() <= 0 || snapped.Width() <= 0 || snapped.Height() <= 0)
            return std::nullopt;

        // Onto the picture's square, where every layout is compared.
        auto across = [&](int x) { return static_cast<int>(std::lround((x - work.Left) * 1000.0 / work.Width())); };
        auto down = [&](int y) { return static_cast<int>(std::lround((y - work.Top) * 1000.0 / work.Height())); };
        Rect place{ across(snapped.Left), down(snapped.Top), across(snapped.Right), down(snapped.Bottom) };

        // 1. The layout in use, as it stands.
        std::optional<SnapTarget> found;
        int nearest = SamePlace + 1;
        for (auto const& tile : OnSquare(areas))
        {
            if (int distance = Distance(place, tile.Bounds); distance < nearest)
            {
                nearest = distance;
                found = SnapTarget{ current, tile.Id };
            }
        }
        if (found) return found;

        // 2. Snap's layouts. The same place in several of them is the same
        // distance away in each.
        struct Candidate
        {
            SnapTarget Target;
            int Distance;
        };
        std::vector<Candidate> candidates;
        for (auto preset : AllPresets())
        {
            auto layout = AsSnapHasIt(preset);
            if (!layout) continue;

            for (auto const& tile : OnSquare(*layout))
                if (int distance = Distance(place, tile.Bounds); distance <= SamePlace)
                    candidates.push_back({ { preset, tile.Id }, distance });
        }

        if (!candidates.empty())
        {
            constexpr int Rounding = 3;
            int closest = std::ranges::min(candidates, {}, &Candidate::Distance).Distance;
            std::erase_if(candidates, [&](auto const& c) { return c.Distance > closest + Rounding; });

            for (auto const& candidate : candidates)
                if (candidate.Target.Preset == current) return candidate.Target;

            // In the panel's order on a tie, which puts simpler layouts first.
            return std::ranges::min(candidates, {}, [](auto const& c) { return MakeLayout(c.Target.Preset).Count(); })
                .Target;
        }

        // 3. Mostly in one area of the layout in use. Half each is in neither.
        long long most = 0;
        for (auto const& tile : OnSquare(areas))
        {
            if (auto overlap = Overlap(place, tile.Bounds); overlap > most)
            {
                most = overlap;
                found = SnapTarget{ current, tile.Id };
            }
        }
        return most * 2 > place.Area() ? found : std::nullopt;
    }

    namespace
    {
        Rect OnSquare(Rect const& snapped, Rect const& work)
        {
            auto across = [&](int x) { return static_cast<int>(std::lround((x - work.Left) * 1000.0 / work.Width())); };
            auto down = [&](int y) { return static_cast<int>(std::lround((y - work.Top) * 1000.0 / work.Height())); };
            return { across(snapped.Left), down(snapped.Top), across(snapped.Right), down(snapped.Bottom) };
        }

        /// The area of `preset` at a place on the square: in the layout as it
        /// stands when that is the one in use, or in Snap's version of it.
        std::optional<WindowId> AreaAt(Rect const& place, LayoutPreset preset, LayoutPreset current,
                                       LayoutTree const& areas)
        {
            std::optional<WindowId> found;
            int nearest = SamePlace + 1;
            auto look = [&](LayoutTree const& layout)
            {
                for (auto const& tile : OnSquare(layout))
                    if (int distance = Distance(place, tile.Bounds); distance < nearest)
                    {
                        nearest = distance;
                        found = tile.Id;
                    }
            };

            if (preset == current) look(areas);
            if (auto snaps = AsSnapHasIt(preset)) look(*snaps);
            return found;
        }
    }

    std::optional<SnapGroup> MatchSnaps(std::vector<Rect> const& snapped, Rect const& work, LayoutPreset current,
                                        LayoutTree const& areas)
    {
        if (snapped.empty() || work.Width() <= 0 || work.Height() <= 0) return std::nullopt;

        std::vector<Rect> places;
        for (auto const& rect : snapped)
            places.push_back(OnSquare(rect, work));

        auto all = [&](LayoutPreset preset) -> std::optional<SnapGroup>
        {
            SnapGroup group{ preset, {} };
            for (auto const& place : places)
            {
                auto area = AreaAt(place, preset, current, areas);
                if (!area) return std::nullopt;
                group.Areas.push_back(area);
            }
            return group;
        };

        // A place for every window: the layout in use, or the simplest.
        if (auto group = all(current)) return group;

        std::optional<SnapGroup> simplest;
        size_t fewest = SIZE_MAX;
        for (auto preset : AllPresets())
        {
            auto group = all(preset);
            if (!group) continue;

            if (auto count = MakeLayout(preset).Count(); count < fewest)
            {
                fewest = count;
                simplest = group;
            }
        }
        if (simplest) return simplest;

        // No one preset has them all: the window the user chose decides.
        auto first = MatchSnap(snapped.front(), work, current, areas);
        if (!first) return std::nullopt;

        SnapGroup group{ first->Preset, { first->Area } };
        for (size_t i = 1; i < places.size(); ++i)
            group.Areas.push_back(AreaAt(places[i], first->Preset, current, areas));
        return group;
    }
}
