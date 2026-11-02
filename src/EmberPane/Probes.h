#pragma once

/// Measurement aids, each writing a text file next to the exe (see
/// Shell::LogDirectory), so a run can be diagnosed without a debugger attached.
namespace EmberPane::Probes
{
    /// diagnostics.txt: the facts that explain a run - arguments, OS build,
    /// transparency, and the tray process's memory once things have settled.
    void WriteDiagnostics();

    /// --probe-windows: writes windows-probe.txt, with every top-level
    /// window's facts and what Ember Pane would do with it. The first tool to
    /// reach for when an app behaves oddly. It includes titles, which nothing
    /// else writes down, since a user runs it deliberately.
    void WriteWindowProbe();
}
