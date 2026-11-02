#pragma once

#include <map>
#include <vector>
#include "CpuMeter.h"

namespace EmberPane::Managed
{
    /// Reads how much CPU time the apps behind the windows have used, into a
    /// CpuMeter. Documented APIs only: NtQuerySystemInformation would read
    /// everything in one call, but its CPU fields are not in the public header.
    ///
    /// Each tick costs a GetProcessTimes per tracked process, on cached handles
    /// (0.06 ms for 100). The process tree is walked far less often: a
    /// snapshot is a thousand times dearer, about 15 ms with 660 processes.
    class CpuSampler
    {
    public:
        CpuSampler();

        /// How long the rate is measured over.
        void SetWindowSeconds(int seconds);

        /// The processes whose windows are being managed.
        void SetRoots(std::vector<DWORD> const& pids);

        /// True when this tick walked the process tree, the costly kind.
        bool Tick();

        /// Time passed unmeasured: the display was off, or the PC asleep.
        void ClearWindow();

        /// Known is false for a process that cannot be read.
        Core::Usage For(DWORD pid) const;

    private:
        void Rebuild();

        Core::CpuMeter m_meter;
        std::vector<DWORD> m_roots;
        std::map<DWORD, Core::ProcessKey> m_rootKeys;
        Core::Families m_families;
        std::map<Core::ProcessKey, winrt::handle> m_handles;
        int m_ticksUntilRebuild{ 0 };
    };
}
