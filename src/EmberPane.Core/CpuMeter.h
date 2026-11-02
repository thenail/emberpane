#pragma once

#include <compare>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace EmberPane::Core
{
    /// A process, told apart from a later one given the same pid.
    struct ProcessKey
    {
        std::uint32_t Pid{ 0 };
        std::uint64_t Created{ 0 };

        auto operator<=>(ProcessKey const&) const = default;
    };

    /// One line of a process snapshot.
    struct ProcessEntry
    {
        std::uint32_t Pid{ 0 };
        std::uint32_t Parent{ 0 };
        std::wstring Exe;
    };

    /// Each root process, with itself and its descendants.
    using Families = std::map<ProcessKey, std::vector<ProcessKey>>;

    /// Processes that host other people's work, whose children are not theirs
    /// to answer for. Everything launched from the Start menu is a child of
    /// Explorer, and it must not all count as File Explorer's.
    bool IsNoAggregate(std::wstring const& exe);

    /// An app is rarely one process: Chrome and VS Code do their work in
    /// children of the one that owns the window. So a window's figure covers
    /// the owner and its descendants, stopping at a process that owns a
    /// managed window of its own, and at IsNoAggregate. A child created before
    /// its parent is a parent link to a reused pid, and is not followed.
    ///
    /// creationOf reads a process's creation time, and is called only for
    /// processes the walk reaches: reading every process would be needless
    /// work with hundreds running. Nothing when it cannot be read.
    Families BuildFamilies(std::vector<ProcessEntry> const& entries, std::vector<ProcessKey> const& roots,
                           std::function<std::optional<std::uint64_t>(std::uint32_t)> const& creationOf);

    struct ProcessSample
    {
        ProcessKey Key;

        /// Kernel plus user time, in 100 ns units.
        std::uint64_t CpuTime{ 0 };
    };

    struct Usage
    {
        /// CPU-seconds per second over the window; 1.0 is one full core.
        double Rate{ 0 };
        double TotalSeconds{ 0 };

        /// False until two samples exist, or when the process cannot be read.
        bool Known{ false };
    };

    /// Turns samples of CPU time into a rate per family.
    ///
    /// It accumulates deltas per process, never a sum of totals. A sum would
    /// drop when a child exits, and read as negative usage, and would spike
    /// when a long-running child joins a family.
    class CpuMeter
    {
    public:
        /// The window is this many ticks long.
        void SetWindowTicks(int ticks);

        /// One sample of every family, each process in it. now is in seconds.
        void Tick(double now, std::map<ProcessKey, std::vector<ProcessSample>> const& families);

        /// Forgets the window, as after sleep: time passed that was not
        /// measured, and a rate across it would mean nothing. The totals stay.
        void ClearWindow();

        Usage For(ProcessKey const& root) const;

    private:
        struct Interval
        {
            double Seconds;
            std::uint64_t Delta;
        };

        struct Family
        {
            std::deque<Interval> Window;
            std::uint64_t Total{ 0 };

            /// It has been sampled, so the next tick has an interval to report.
            bool Seen{ false };
        };

        int m_windowTicks{ 5 };
        std::optional<double> m_lastTick;
        std::map<ProcessKey, std::uint64_t> m_lastCpu;
        std::map<ProcessKey, Family> m_families;
    };
}
