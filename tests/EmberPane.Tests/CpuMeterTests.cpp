#include "Test.h"
#include "CpuMeter.h"

using namespace EmberPane::Core;

namespace
{
    constexpr std::uint64_t Second = 10'000'000;

    constexpr ProcessKey Root{ 100, 1000 };
    constexpr ProcessKey Child{ 200, 2000 };
    constexpr ProcessKey Other{ 300, 3000 };

    using Sample = std::map<ProcessKey, std::vector<ProcessSample>>;

    Sample One(std::uint64_t cpuSeconds) { return { { Root, { { Root, cpuSeconds * Second } } } }; }

    Sample Two(std::uint64_t rootSeconds, std::uint64_t childSeconds)
    {
        return { { Root, { { Root, rootSeconds * Second }, { Child, childSeconds * Second } } } };
    }

    auto CreatedAt(std::map<std::uint32_t, std::uint64_t> times)
    {
        return [times](std::uint32_t pid) -> std::optional<std::uint64_t>
        {
            auto found = times.find(pid);
            if (found == times.end()) return std::nullopt;
            return found->second;
        };
    }

    size_t SizeOf(Families const& families, ProcessKey root)
    {
        return families.at(root).size();
    }
}

TEST(A_first_sample_is_not_known)
{
    CpuMeter meter;
    meter.Tick(0, One(50));

    CHECK(!meter.For(Root).Known);
}

TEST(A_process_never_sampled_is_not_known)
{
    CpuMeter meter;
    CHECK(!meter.For(Root).Known);
}

TEST(One_full_core_reads_as_one)
{
    CpuMeter meter;
    meter.Tick(0, One(10));
    meter.Tick(2, One(12));

    auto usage = meter.For(Root);
    CHECK(usage.Known);
    CHECK_NEAR(1.0, usage.Rate, 1e-9);
}

TEST(Two_cores_read_as_two)
{
    CpuMeter meter;
    meter.Tick(0, One(10));
    meter.Tick(2, One(14));

    CHECK_NEAR(2.0, meter.For(Root).Rate, 1e-9);
}

TEST(The_rate_covers_the_whole_window)
{
    CpuMeter meter;
    meter.SetWindowTicks(5);
    meter.Tick(0, One(0));

    // Four seconds of work, then six of nothing.
    meter.Tick(2, One(2));
    meter.Tick(4, One(4));
    meter.Tick(6, One(4));
    meter.Tick(8, One(4));
    meter.Tick(10, One(4));

    CHECK_NEAR(0.4, meter.For(Root).Rate, 1e-9);
}

TEST(What_falls_out_of_the_window_stops_counting)
{
    CpuMeter meter;
    meter.SetWindowTicks(2);
    meter.Tick(0, One(0));
    meter.Tick(2, One(2));
    meter.Tick(4, One(2));
    meter.Tick(6, One(2));

    CHECK_NEAR(0.0, meter.For(Root).Rate, 1e-9);
}

TEST(A_child_exiting_does_not_make_usage_negative)
{
    CpuMeter meter;
    meter.Tick(0, Two(10, 500));
    meter.Tick(2, One(12));

    auto usage = meter.For(Root);
    CHECK(usage.Rate >= 0);
    CHECK_NEAR(1.0, usage.Rate, 1e-9);
}

TEST(A_child_joining_does_not_spike_usage)
{
    CpuMeter meter;
    meter.Tick(0, One(10));
    meter.Tick(2, Two(12, 500));

    CHECK_NEAR(1.0, meter.For(Root).Rate, 1e-9);
}

TEST(A_child_counts_from_its_second_sample)
{
    CpuMeter meter;
    meter.SetWindowTicks(1);
    meter.Tick(0, One(10));
    meter.Tick(2, Two(10, 500));
    meter.Tick(4, Two(10, 504));

    CHECK_NEAR(2.0, meter.For(Root).Rate, 1e-9);
}

TEST(A_family_counts_all_its_processes)
{
    CpuMeter meter;
    meter.Tick(0, Two(0, 0));
    meter.Tick(2, Two(1, 3));

    CHECK_NEAR(2.0, meter.For(Root).Rate, 1e-9);
}

TEST(A_gap_clears_the_window)
{
    CpuMeter meter;
    meter.Tick(0, One(10));
    meter.Tick(2, One(12));

    meter.ClearWindow();
    CHECK(!meter.For(Root).Known);

    // An hour of sleep is not an interval to report.
    meter.Tick(3600, One(13));
    CHECK(!meter.For(Root).Known);

    meter.Tick(3602, One(15));
    CHECK_NEAR(1.0, meter.For(Root).Rate, 1e-9);
}

TEST(The_total_includes_what_came_before_and_survives_a_gap)
{
    CpuMeter meter;
    meter.Tick(0, One(100));
    meter.Tick(2, One(102));
    meter.ClearWindow();
    meter.Tick(3600, One(110));

    CHECK_NEAR(110.0, meter.For(Root).TotalSeconds, 1e-9);
}

TEST(A_family_that_goes_is_forgotten)
{
    CpuMeter meter;
    meter.Tick(0, One(10));
    meter.Tick(2, One(12));
    meter.Tick(4, {});

    CHECK(!meter.For(Root).Known);
}

TEST(A_family_covers_a_process_and_its_descendants)
{
    std::vector<ProcessEntry> entries{ { 100, 1, L"chrome.exe" }, { 200, 100, L"chrome.exe" }, { 300, 200, L"chrome.exe" } };

    auto families = BuildFamilies(entries, { Root }, CreatedAt({ { 200, 2000 }, { 300, 3000 } }));

    CHECK_EQ(size_t{ 3 }, SizeOf(families, Root));
}

TEST(The_walk_stops_at_a_process_with_a_window_of_its_own)
{
    std::vector<ProcessEntry> entries{ { 100, 1, L"terminal.exe" }, { 300, 100, L"notepad.exe" }, { 400, 300, L"helper.exe" } };

    auto families = BuildFamilies(entries, { Root, Other }, CreatedAt({ { 300, 3000 }, { 400, 4000 } }));

    CHECK_EQ(size_t{ 1 }, SizeOf(families, Root));
    CHECK_EQ(size_t{ 2 }, SizeOf(families, Other));
}

TEST(Everything_started_from_explorer_is_not_explorers)
{
    std::vector<ProcessEntry> entries{ { 100, 1, L"EXPLORER.EXE" }, { 200, 100, L"background.exe" } };

    auto families = BuildFamilies(entries, { Root }, CreatedAt({ { 200, 2000 } }));

    CHECK_EQ(size_t{ 1 }, SizeOf(families, Root));
}

TEST(The_walk_does_not_pass_through_a_shared_host)
{
    std::vector<ProcessEntry> entries{ { 100, 1, L"app.exe" }, { 200, 100, L"svchost.exe" }, { 300, 200, L"other.exe" } };

    auto families = BuildFamilies(entries, { Root }, CreatedAt({ { 200, 2000 }, { 300, 3000 } }));

    CHECK_EQ(size_t{ 1 }, SizeOf(families, Root));
}

TEST(A_child_older_than_its_parent_is_a_reused_pid)
{
    std::vector<ProcessEntry> entries{ { 100, 1, L"app.exe" }, { 200, 100, L"old.exe" } };

    auto families = BuildFamilies(entries, { Root }, CreatedAt({ { 200, 500 } }));

    CHECK_EQ(size_t{ 1 }, SizeOf(families, Root));
}

TEST(A_child_that_cannot_be_read_is_left_out)
{
    std::vector<ProcessEntry> entries{ { 100, 1, L"app.exe" }, { 200, 100, L"locked.exe" } };

    auto families = BuildFamilies(entries, { Root }, CreatedAt({}));

    CHECK_EQ(size_t{ 1 }, SizeOf(families, Root));
}

TEST(A_loop_of_parent_links_ends)
{
    std::vector<ProcessEntry> entries{ { 100, 200, L"a.exe" }, { 200, 100, L"b.exe" } };

    auto families = BuildFamilies(entries, { Root }, CreatedAt({ { 200, 2000 }, { 100, 1000 } }));

    CHECK_EQ(size_t{ 2 }, SizeOf(families, Root));
}
