# Ember Pane: implementation specification

Draft 1, 2 November 2026. The name still has to be reserved; see [section 13.3](#133-the-name).

Ember Pane is a Windows 11 tray app that arranges the windows of the apps you have open. You choose one of a few predefined layouts in its panel, and it places every ordinary window, including those already running when it starts, in the layout's areas with a small margin around each. Windows keep their rounded corners and stay resizable: drag an edge and its neighbours follow. Each window's thin border changes colour with how hard its app is working, and Settings gives each colour its plain word.

It is built the way [AstroDimmer](../../astrodimmer) is built: C++/WinRT, a small Win32 tray process, a WinUI process that exists only while its windows are open, and a pure Core library covered by tests. Where this document does not say otherwise, do what AstroDimmer does.

## 1. Scope

**Version 1**

- Places existing and new windows of other apps, on every monitor and virtual desktop.
- Keeps a configurable margin between windows and around the screen edge.
- Asks Windows for rounded corners on every managed window.
- Lets the user resize and rearrange tiled windows with the mouse, in the normal Windows way.
- Shows a line in the gap on hover, which can be dragged to resize the windows on both sides.
- Lets the user drag a window to where it should go, with a preview of where it will land.
- Colours each window's border by how hard its app is working, and says so in plain words.
- Offers a tray icon, a Quick Settings style panel and a Settings window.
- Installs per user, with no admin rights.

**After version 1**

- Keyboard shortcuts for focus, swap and resize.
- Fixed positions per app ("Outlook always goes top right").
- Layouts that survive a reboot.
- Other layout styles, such as master and stack.

**Not planned**

- Named workspaces of its own. Windows' virtual desktops already do this.
- Injecting a DLL into other processes. Nothing here needs it.
- Running elevated. See section 3.

## 2. Decisions

| Decision | Choice | Why |
| --- | --- | --- |
| Reaching other apps | Plain Win32 and DWM calls from outside the app | No injection, and no cooperation from the app. Verified in section 3. |
| Layout | Predefined layouts of one to four areas, each held as a binary split tree, per monitor and virtual desktop | Giving every window its own place made a dozen areas with a dozen windows open. A few areas you chose yourself stay readable. Windows beyond the number of areas share one, in front of each other, and a dragged edge still maps onto one split's ratio. |
| Frame | The window's own DWM border, `DWMWA_BORDER_COLOR` | Windows draws it, so it follows the window and its corners exactly, with no extra windows, no z-order chasing and no lag. It is thin, and its width cannot be changed, so section 9.6 draws what it cannot, only when asked to. |
| Corners | `DWMWA_WINDOW_CORNER_PREFERENCE` | Same mechanism, already the Windows 11 look. |
| CPU measure | CPU-seconds per second over the last 10 s, summed across the app's process family | "How much is it taking" is a current rate. A lifetime total only ever rises, and a long-open idle app would stay red. |
| Colour scale | Absolute, in cores' worth of CPU time | As a share of the machine, one pegged core reads as 3% on a 32-thread CPU. |
| What people see | Plain words, from Resting to Flat out | The measure above is for the code. Nobody should need to know what a CPU-second is. Section 9.8 sets the rule. |
| Minimum OS | Windows 11, build 22000 | The border and corner attributes start there. AstroDimmer's floor is 17763. |
| Privilege | Normal user, never elevated | Keeps the per-user installer. `uiAccess` needs a signed binary in a protected folder, which a per-user install cannot provide. |
| Windows' Snap | Off while arranging is on, for the session only | It answers the same drag as the preview. It cannot be stopped for one drag, and the user's saved preference is never changed. Section 8.6. |
| First run | Arranging starts off; the panel opens to offer it | Moving every window on the desktop without being asked is a poor first impression. Frames start on, since they move nothing. |

## 3. What has been verified

Everything in the first table was measured on 2 November 2026, on the development machine: Windows 11 build 26300, display at 150%, on a 3840 by 2160 primary display, from a non-elevated process. Targets were throwaway windows and private copies of Chrome with temporary profiles, never windows in use. Results were confirmed by reading screen pixels where pixels could show them.

| Question | Result |
| --- | --- |
| Can another process's window be moved? | Yes, `SetWindowPos` with `SWP_ASYNCWINDOWPOS`. |
| Can its border colour be set? | Yes. `S_OK`, and the pixels changed. |
| Can its corners be changed? | Yes. Switching between `DWMWCP_ROUND` and `DWMWCP_DONOTROUND` changed the corner on screen. |
| Does the border follow the rounded corner? | Yes. |
| How wide is the border? | 2 physical pixels at 150%, for both a DPI-aware and a DPI-virtualized window. DWM reported 2 and 1 respectively. The width can only be read, not set. |
| Does a colour outlive the process that set it? | **Yes.** It stays after that process exits, until something changes it or the window closes. `DWMWA_COLOR_DEFAULT` from another process restores it. |
| Is the invisible resize border a constant? | **No.** Left, top, right, bottom was 9, 0, 9, 9 for an aware window and for Chrome, VS Code and Snipping Tool, 7, 0, 7, 7 for Notepad, and 11, 0, 11, 11 for a virtualized window. It must be measured per window. |
| Does a window take exactly the size it is asked for? | A DPI-aware window did, three times out of three. A virtualized window asked for height 460 settled at 461. Read-back needs a tolerance. |
| Can CPU times be read? | 536 of 692 processes. The other 156 were services, system processes and vendor helpers, so a "don't know" state is required. |
| Can the gap be dragged to resize? | Largely, by Windows' answer to `WM_NCHITTEST` for each pixel of the gap between two standard windows, and the same again between two Chrome windows at 12 and 18 px. No drag was performed. Figures in section 8.6. |
| Does the border show on Chrome? | **Yes**, on all four edges, 2 px wide. |
| Are tiled windows rounded? | **Yes.** Two Chrome windows placed by `SetWindowPos` kept their rounded corners, with the border following the curve. Placing a window is not the shell's snapped state. |
| Do common apps need a built-in rule? | Not these. Chrome, VS Code, Spotify, Notepad, Snipping Tool, File Explorer and the Settings app's frame all have `WS_CAPTION` and `WS_THICKFRAME`. |
| How do hidden and system windows report their desktop? | A hidden UWP window was cloaked by the shell and reported `GUID_NULL` while on the current desktop. `TextInputHost` failed with `0x8002802B`. The `CloakedHere` row in section 7.2 and the `GUID_NULL` fallback in section 8.7 fit both. |
| Do pixels of alpha 1 in a layered window catch the mouse? | **Yes**, by `WindowFromPoint`: alpha 1 was caught and alpha 0 passed through. No real press was sent. |
| What does polling the cursor cost? | 4.4 µs a poll, about 0.00004 CPU-seconds per second at 10 polls a second, before the cost of waking. |
| What does sampling CPU cost? | 0.06 ms for `GetProcessTimes` on 100 handles, 14.6 ms for a Toolhelp snapshot of 660 processes. Real ticks cost more; section 14 has them. |
| Can Snap be stopped for a drag? | Yes, with `SPI_SETDOCKMOVING`, but only **before** the press. Turned off during a real drag, it has no effect and the window still maximized. |
| How is that setting passed? | **In `uiParam`, not the `pvParam` the docs name.** Given in `pvParam` it is ignored, and the zero in `uiParam` turns docking off, so "on" silently fails. Always read it back. |
| Does a snapped window stay square? | **Yes.** Windows does not round a window it marks as snapped, and `SetWindowPos` does not clear the mark (`IsWindowArranged`). `SW_RESTORE` followed straight away by the move clears it, and the window ends where it was asked to. |
| Does the frame follow an app's real load? | **Yes.** A window whose work ran in a child process reached the exact colour for one full core, within the 10 s window, and returned to the default after `--quit`. |

These are still **not** verified. Milestone M0 stays open until they are:

| Question | State |
| --- | --- |
| Can an elevated window be moved, coloured or measured? | Not tested: the UAC prompt was not approved. Moving should fail (UIPI). Colour and times are unknown. Needs one approved prompt. |
| Is 2 px enough? | Needs a person's eyes. Pixels show it is drawn, not that it can be read at a glance. |
| Does VS Code behave as Chrome does? | Not tested live: a pending VS Code update held private copies from opening. It shares Chrome's window class, styles and insets, so it should. Recheck after the update. |
| Do Office and other custom-framed apps show the border? | Not tested. The docs say some customized frames do not round automatically, and that the corner API is only a hint. |
| How does a window pinned to all desktops report its desktop? | Not tested: none was open. It decides which tree that window belongs to. |

## 4. Reuse from AstroDimmer

Paths are relative to `C:\Dev\astrodimmer`. Rename `AstroDimmer` to `EmberPane` throughout: namespaces, window classes, the mutex `Local\EmberPane.Host`, the Run value and the settings folder. "Astro Dimmer" in text becomes "Ember Pane". Generate new project GUIDs and a new installer `AppGuid`. Change the link's `Magic` to `0x45504C4B` ('EPLK').

**Copy, renaming only**

| Files | What they give us |
| --- | --- |
| `Directory.Build.props` | The hybrid CRT, so no Visual C++ redistributable |
| `src/AstroDimmer.Core/Json.h`, `Json.cpp` | JSON for settings and the link |
| `src/AstroDimmer.Core/FlyoutPlacement.h`, `FlyoutPlacement.cpp` | Panel position, with its tests |
| `src/AstroDimmer/Event.h`, `CommandLine.h`, `Trace.h`, `ThemeBrush.h` | Multicast events, switches, the log, theme brushes |
| `src/AstroDimmer/TrayIcon.h`, `TrayIcon.cpp` | Notification icon, glyph drawing, recovery when Explorer restarts |
| `src/AstroDimmer/Native/Shell.h`, `Shell.cpp` | Theme, paths, version, run at login |
| `src/AstroDimmer/Controls/SettingsCard.*`, `Controls/OffsetStepper.*`, `Themes/Generic.xaml` | The Settings look |
| `src/AstroDimmer/app.manifest` | PerMonitorV2, which every coordinate in this document relies on |
| `tests/AstroDimmer.Tests/Test.h`, `Main.cpp` | The test harness |

**Copy, then change**

| Files | Change |
| --- | --- |
| `src/AstroDimmer/Main.cpp` | Drop the DDC probe. Add `--probe-windows` and `--quit`, both before the mutex. |
| `src/AstroDimmer/Host.h`, `Host.cpp` | Keep the UI process handling, tray handling, `Send` and polling pattern. The displays and the schedule give way to the components in section 8. |
| `src/AstroDimmer/Link.h`, `Link.cpp` | Same transport, new message types (section 10). |
| `src/AstroDimmer/App.xaml.h`, `App.xaml.cpp`, `App.xaml` | Same lifecycle. Handle the new messages. |
| `src/AstroDimmer/Services.h`, `Services.cpp` | `WindowMirror` replaces `DisplayMirror`; the stage goes. |
| `src/AstroDimmer/Strings.h`, `Strings.cpp` | Keep `UseDisplayLanguage`, `Get` and `Format`. `Time` and `Kelvin` go. |
| `src/AstroDimmer/FlyoutWindow.xaml*` | Keep placement, the slide, cloaking, the focus sink and the footer. New rows. |
| `src/AstroDimmer/SettingsWindow.xaml*` | Keep the page shell and cards. New content. |
| `src/AstroDimmer/Displays/DisplayMirror.*` | The model for `WindowMirror`. |
| `src/AstroDimmer/Native/SystemEvents.h`, `SystemEvents.cpp` | Add `WorkAreaChanged`, on `WM_SETTINGCHANGE` with `SPI_SETWORKAREA`. |
| `src/AstroDimmer/Native/DisplayInfo.h`, `DisplayInfo.cpp` | Keep `Monitors`, `Targets` and `FriendlyName`. Drop the EDID and DDC helpers. |
| `src/AstroDimmer.Core/MonitorMatcher.h`, `MonitorMatcher.cpp` | Keep `DisplayTarget`, `DeviceKey` and `KeyOf`, for a monitor identity that survives a replug. The dxva2 matching goes. |
| `src/AstroDimmer.Core/Settings.h`, `Settings.cpp` | Keep tolerant loading and the atomic save. New fields; the Glimmer import goes. |
| `src/AstroDimmer/Probes.h`, `Probes.cpp` | Keep `WriteDiagnostics`. A window probe replaces the DDC probe. |
| `build.ps1`, `.github/workflows/build.yml`, `installer/AstroDimmer.iss` | See section 12. |

**Leave behind**: `src/AstroDimmer/Displays/` apart from the mirror's shape, `Native/Ddc*`, `Native/Backlight.*`, `AstroStage.h`, `AstroTimeline.*`, `WorldMap.*`, `LocationDetector.*`, and from Core `AstroEngine`, `AstroScheduler`, `AstroSettings`, `SolarTimes`, `DayTimeline`, `DateTime`, `ColorTemperature`, `Backlight`, `DisplayDefaults` and `DisplayNames`.

## 5. Architecture

Two processes from one exe, for the same reason as AstroDimmer: WinUI cannot be unloaded, so it lives in a process that exits when its windows close.

```
 EmberPane.exe (host: plain Win32, runs all session)
 ┌──────────────────────────────────────────────────────────┐
 │  WinEvents ──► WindowTracker ──► Arranger ──► SetWindowPos ───► other apps'
 │                     │                                    │      windows
 │                     ▼                                    │
 │                 CpuSampler ──► FrameService ──► DWM ─────────►
 │                                                          │
 │  SystemEvents, TrayIcon, Settings (the only writer)      │
 └───────────────────────┬──────────────────────────────────┘
                         │ Link: JSON over WM_COPYDATA
 ┌───────────────────────┴──────────────────────────────────┐
 │  EmberPane.exe --ui (WinUI: panel and Settings)           │
 │  started on a click, exits when both are closed           │
 └──────────────────────────────────────────────────────────┘

 EmberPane.Core: pure logic, used by both, tested on its own
```

Rules that hold everywhere:

- **One thread.** The host does everything on its main thread, which must keep pumping messages because `SetWinEventHook` delivers events there.
- **Never wait on another app.** A hung app must not freeze the tray or stop other windows being arranged. Use only calls that do not block on the target: `SWP_ASYNCWINDOWPOS` for moves, `GetWindowRect`, `IsZoomed` and DWM attributes for reads. Do not `SendMessage` to foreign windows.
- **Never run work inside a hook callback.** The callback filters and queues; the work happens from the dispatcher queue, as `Link::Endpoint` does with its inbox.
- **Core knows no HWNDs.** A window is an opaque `WindowId`, so every decision can be tested without a window.
- **Coordinates are physical pixels.** Margins are stored in DIPs and converted per monitor.
- **The host owns the settings file.** The UI sends edits, as in AstroDimmer.

## 6. Repository layout

```
EmberPane.slnx, Directory.Build.props, build.ps1, README.md, LICENSE
docs/
installer/EmberPane.iss
src/EmberPane.Core/
    Geometry.h                 Rect, Size, Edge, Axis
    LayoutTree.h/.cpp          The split tree and its arithmetic
    WindowRules.h/.cpp         Classification and the user's exceptions
    CpuMeter.h/.cpp            Process samples to a rate per app
    HeatRamp.h/.cpp            Rate to colour
    FlyoutPlacement, Json, MonitorMatcher, Settings   (from AstroDimmer)
src/EmberPane/
    Main.cpp, Host, Link, TrayIcon, Probes, Services, Strings, ...
    App.xaml, FlyoutWindow.xaml, SettingsWindow.xaml, Controls/, Themes/
    Managed/
        ManagedWindow.h        What the host knows about one window
        WindowTracker.h/.cpp   Discovery, adoption, reconcile
        Arranger.h/.cpp        Trees, placing, settling, drags
        GapLine.h/.cpp         The hover line and its drag
        CpuSampler.h/.cpp      Process handles and the sampling tick
        FrameService.h/.cpp    Border colours and corners
        WindowMirror.h/.cpp    The UI process's copy of the window list
    Native/
        WinEvents.h/.cpp       SetWinEventHook wrapper
        WindowFacts.h/.cpp     Reads one window into a WindowFacts
        ProcessTree.h/.cpp     Toolhelp snapshot, exe names, real owner of UWP windows
        VirtualDesktops.h/.cpp IVirtualDesktopManager wrapper
        DisplayInfo, Shell, SystemEvents   (from AstroDimmer)
tests/EmberPane.Tests/
```

## 7. Core

### 7.1 LayoutTree

One tree per monitor and virtual desktop. A leaf holds a `WindowId`. An inner node holds an `Axis` and a `Ratio`, the first child's share of the space left after the gap.

```cpp
using WindowId = std::uint64_t;

struct Tile { WindowId Id; Rect Bounds; };

class LayoutTree
{
public:
    /// Splits the leaf holding `beside`, or without one the largest leaf (the
    /// most recently inserted, on a tie), along its longer side. False when
    /// the halves would not reach `minimum`: the caller floats the window
    /// rather than squeezing it in.
    bool Insert(WindowId id, std::optional<WindowId> beside, Rect area, int gap, Size minimum);

    /// The sibling takes the parent's place.
    void Remove(WindowId id);
    void Swap(WindowId a, WindowId b);

    /// Takes a window from where it is in this tree and splits `beside` to
    /// hold it, on the given side. Between trees, Remove from one and call
    /// InsertBeside on the other.
    void MoveBeside(WindowId id, WindowId beside, Edge side);
    void InsertBeside(WindowId id, WindowId beside, Edge side);

    std::vector<Tile> Arrange(Rect area, int gap, MinimumSizes const& minimums) const;

    /// Moves the split that bounds this edge of the window. False when the
    /// edge lies against the area's boundary, where there is no split.
    bool MoveEdge(WindowId id, Edge edge, int position, Rect area, int gap, MinimumSizes const& minimums);

    /// The gap under a point, for the gap line. Where gaps cross, the
    /// outermost split wins, since its gap is the unbroken one.
    std::optional<GapHit> GapAt(Point point, Rect area, int gap) const;

    /// Moves one split directly: the same arithmetic as MoveEdge, for a drag
    /// that starts on the gap rather than on a window.
    void MoveSplit(SplitId split, int position, Rect area, int gap, MinimumSizes const& minimums);
};
```

`area` is the monitor's work area, inset by the outer margin. `Bounds` is the visible rectangle of the window, not the rectangle passed to Windows (section 8.4).

**Arrange** splits a rectangle `R` along an axis of length `L`, in integers, so adjacent gaps are exactly `gap` wide at any scale:

```
space  = L - gap
first  = clamp(round(space * Ratio), minimum of first child, space - minimum of second child)
second = space - first
```

A subtree's minimum along the split axis is the sum of its children's minimums plus `gap`; across the axis it is the larger of the two. If the area cannot satisfy them, give each child what fits and accept an overlap: a window asking for more than the screen has must not break the layout.

**MoveEdge** walks up from the leaf to the nearest split on the matching axis with the leaf on the matching side. Moving `Right` to `x` uses a split with the leaf in its first child. Moving `Left` to `x` uses one with the leaf in its second child. `Top` and `Bottom` work the same way on the other axis.

```
Right edge:  Ratio = (x - R.Left) / (R.Width - gap)
Left edge:   Ratio = (x - gap - R.Left) / (R.Width - gap)
```

Clamp the ratio to what the minimums allow.

### 7.2 WindowRules

A pure function over facts read once by `Native/WindowFacts`:

```cpp
enum class Disposition { Ignore, Float, Tile };

struct WindowFacts
{
    bool Visible, HasOwner, ToolWindow, NoActivate, HasCaption, Resizable;
    bool OwnProcess;
    bool CloakedHere;   // cloaked, yet on the current desktop
    std::wstring ClassName, ExeName, Title;
};

Disposition Classify(WindowFacts const& facts, std::vector<Rule> const& rules);
```

In order, first match wins:

| Condition | Result |
| --- | --- |
| Not visible, or our own exe (host or UI) | Ignore |
| Cloaked on the current desktop: usually a UWP app hiding itself. `UNCLOAKED` classifies it again. | Ignore |
| Shell windows: `Progman`, `WorkerW`, `Shell_TrayWnd`, `Shell_SecondaryTrayWnd` | Ignore |
| `WS_EX_TOOLWINDOW` or `WS_EX_NOACTIVATE` | Ignore |
| Has an owner (dialogs and their kin) | Ignore |
| A user rule matches | The rule's mode |
| No `WS_CAPTION` | Ignore |
| No `WS_THICKFRAME` (cannot be resized) | Float |
| Otherwise | Tile |

Windows on other virtual desktops are cloaked too, but are classified normally and join their own desktop's tree.

Rules run before the style checks so that a user can tile an app that draws its own frame. A rule matches on exe name, case-insensitively, optionally narrowed by class and by a title substring.

The common apps surveyed in section 3 pass these checks unaided. Check others with `--probe-windows` in M2. An app with no `WS_CAPTION` needs a built-in rule or a looser check.

Dispositions in practice: **Tile** windows are placed and framed. **Float** windows are framed and left where they are. **Ignore** windows are left alone entirely.

### 7.3 CpuMeter

An app is rarely one process. Chrome and VS Code do their work in children of the process that owns the window, so a window's figure covers a **family**: the owning process and its descendants.

Family rules, applied when the process tree is refreshed:

- The walk stops at a process that owns a managed window of its own. It is its own family.
- It also stops at the roots in `NoAggregate`: `explorer.exe`, `svchost.exe`, `sihost.exe`, `RuntimeBroker.exe`, `ApplicationFrameHost.exe`. Everything launched from the Start menu is a child of Explorer, and it must not all count as File Explorer's.
- A child must have been created after its parent, or the parent link is a reused PID.

`CpuMeter` is fed samples of `{ pid, creationTime, cpuTime }`, where `cpuTime` is kernel plus user time in 100 ns units. **It accumulates deltas per process, never a sum of totals.** A sum would drop when a child exits and read as negative usage, and would spike when a long-running child joins a family. A process seen for the first time contributes nothing until its second sample.

```cpp
struct Usage
{
    double Rate;        // CPU-seconds per second over the window; 1.0 is one full core
    double TotalSeconds;
    bool Known;         // false until two samples exist, or when the process cannot be read
};
```

The rate is the sum of deltas across a ring of the last `WindowSeconds / SampleSeconds` ticks, divided by the elapsed time actually measured. Clear the ring after a gap, such as sleep or a display-off pause.

### 7.4 HeatRamp

```cpp
struct FrameColor { bool UseSystemDefault; std::uint8_t R, G, B; };
FrameColor ColorFor(double rate);

enum class Activity { Resting, Light, Working, Busy, VeryBusy, FlatOut };
Activity ActivityFor(double rate);
```

| From, in CPU-seconds per second | The word people see | Colour |
| --- | --- | --- |
| 0 | Resting | System default border |
| 0.02 | Light | Blue |
| 0.25 | Working | Teal |
| 1.0 | Busy | Amber |
| 2.0 | Very busy | Orange |
| 4.0 | Flat out | Red |

A frame already coloured keeps blue down to 0.01, half the first stop. An idle app can sit right at 0.02, and its frame would otherwise blink on and off with each tick.

Those are the colours of the default theme. Settings offers four, with the same stops:

| Theme | Light | Working | Busy | Very busy | Flat out |
| --- | --- | --- | --- | --- | --- |
| Bright (default) | `#4C9BE8` | `#2EC4B6` | `#F2C12E` | `#F2711C` | `#E03131` |
| Balanced | `#2F80DE` | `#0B9287` | `#B28000` | `#E26600` | `#DC2F2F` |
| Ember | `#FFD166` | `#FFB13B` | `#FF8C2E` | `#F2542D` | `#D7263D` |
| Aurora | `#5EEAD4` | `#38BDF8` | `#818CF8` | `#C084FC` | `#F472B6` |

Interpolate between stops on `log2(rate)`, then quantize to 24 levels. Quantizing means a window's colour changes only when its load really has, so the common tick makes no DWM call. Blue to amber to red avoids relying on telling red from green, and Settings gives each colour its word, so colour is never the only signal. A word changes exactly at its stop, while the colour blends between stops.

A border lies against the window's own content, which may be light or dark and cannot be asked for. Bright reaches 12:1 on black but only 1.5:1 for amber on a light window, where it can hardly be seen on its own. There are two answers. Balanced is all mid-tones: every level is at least 3:1 (WCAG's bar for graphics) against white, `#F3F3F3`, `#202020`, `#2B2B2B` and black. Or the dark line in section 9.6 separates any theme from the window: every level of every theme is at least 3:1 against it. Tests pin both.

The stops and themes are tables in `HeatRamp.cpp`.

## 8. Host

### 8.1 Placing windows that are already running

This is the first thing the host does once arranging is on, whether at startup or when the switch is flipped. It needs nothing from the apps and works on anything already open.

1. `EnumWindows` lists every top-level window, frontmost first.
2. Each is classified (section 7.2). Tiled windows go into the tree for the monitor they are mostly on (`MonitorFromWindow`, nearest) and their virtual desktop.
3. In that order, each window goes to the area with the fewest windows, so the frontmost windows are the ones on show, and the rest share areas behind them.
4. Windows are then placed as in section 8.4. A window is never relaunched, and its app sees only an ordinary move and resize.

Three kinds of window are treated differently: minimized windows are tracked but not inserted until restored; maximized windows are inserted but not moved until the user restores them; windows that cannot be moved (section 8.8) float.

Turning arranging off leaves every window where it is. Windows goes on treating them as normal windows.

### 8.2 WinEvents

One `SetWinEventHook` per contiguous range, `WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS`, process and thread 0. One hook across the whole range would receive every event on the system.

| Event | Use |
| --- | --- |
| `EVENT_OBJECT_SHOW`, `EVENT_OBJECT_UNCLOAKED` | A window appeared, or returned from another desktop |
| `EVENT_OBJECT_HIDE`, `EVENT_OBJECT_DESTROY` | It went away. Close-to-tray apps hide, so treat both alike. |
| `EVENT_OBJECT_CLOAKED` | It left the current desktop, or a UWP app hid it |
| `EVENT_SYSTEM_MINIMIZESTART`, `MINIMIZEEND` | Give up its tile, or take one back |
| `EVENT_SYSTEM_MOVESIZESTART`, `MOVESIZEEND` | A user drag |
| `EVENT_SYSTEM_FOREGROUND` | Remember the focused window per tree |

The callback drops everything without `idObject == OBJID_WINDOW && idChild == CHILDID_SELF`, appends to a queue and enqueues one drain. A plain static callback needs a static pointer to the instance, since the API has no context parameter.

**Do not hook `EVENT_OBJECT_LOCATIONCHANGE` globally.** It also fires for the mouse cursor and caret, which would wake the tray on every mouse movement. Maximizing and apps moving themselves are caught by the reconcile tick instead (section 8.5).

### 8.3 WindowTracker and ManagedWindow

The tracker owns a `ManagedWindow` per adopted window:

```cpp
struct ManagedWindow
{
    HWND Hwnd;
    DWORD ProcessId;                   // the real one; see below
    std::wstring ExeName, ClassName;
    Core::Disposition Disposition;
    WindowState State;                 // Normal, Minimized, Maximized, Fullscreen, Immovable
    RECT Target;                       // where it should be, as passed to SetWindowPos
    Core::Rect Inset;                  // invisible border, measured
    Core::Size Minimum;                // learned, starts at the app default
    int Corrections;                   // see Stubborn windows, section 8.5
    std::optional<COLORREF> Painted;   // last colour written; empty forces a write
};
```

UWP windows belong to `ApplicationFrameHost.exe`. For `ApplicationFrameWindow`, enumerate the child windows and take the process of the one that differs, usually class `Windows.UI.Core.CoreWindow`. Do this again later if it is not there yet.

Titles change, so read them with `GetWindowTextW` only when the panel needs them. For a window in another process, that call does not send a message to it, so it cannot hang.

### 8.4 Placing a window

For each `Tile` of a tree whose window is in state `Normal`:

1. Measure the inset: `DWMWA_EXTENDED_FRAME_BOUNDS` against `GetWindowRect`. It differs per window and per DPI, as section 3 shows.
2. Grow the tile by the inset, so that the visible edge lands on the tile.
3. Skip the call if `Target` already equals this.
4. Call `SetWindowPos` with `SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_ASYNCWINDOWPOS`.

Do not use `BeginDeferWindowPos`. It applies a batch as a unit, so one elevated or hung window can hold up the rest.

**Settling.** 200 ms after a pass, read each moved window back. Tolerance is 2 px on every side, since DPI-virtualized windows round (section 3).

| Read-back | Meaning | Action |
| --- | --- | --- |
| Within tolerance | Done | Re-measure the inset, in case the DPI changed |
| Larger than asked | The app has a minimum size | Record it in `Minimum`, re-arrange the tree |
| Smaller than asked | A maximum size or a character grid, as in terminals | Accept it; do not retry |
| Unmoved | It cannot be moved | See section 8.8 |
| On a monitor of another DPI, or otherwise wrong | The app rescaled during the move | Place it again, at most twice |

A newly shown window gets a second settle after 1 s, because many apps restore their saved geometry just after showing.

`WM_GETMINMAXINFO` is no use for finding minimum sizes: its pointer does not cross processes. Read-back is how they are learned.

### 8.5 Reconcile tick

Every `SampleSeconds` (2 s), with the CPU sample. Reads only, cheap, and cannot block:

- `IsWindow` false: remove it, in case an event was missed.
- `IsZoomed` changed: enter or leave `Maximized`. On leaving, arrange again.
- Covers its whole monitor with no caption: enter `Fullscreen`. Its tile is kept, nothing is moved, and no border is written.
- Monitor signature changed: handle as a display change (section 8.7). The signature is each monitor's key, work area and DPI. AstroDimmer polls for the same reason: some docks and KVMs send no message.
- A `Normal` tiled window is away from `Target` with no drag in progress: something moved it, perhaps Snap or the app itself. Put it back.

**Stubborn windows.** Each such correction increments `Corrections`. After 3 within 10 s, stop fighting and set the window to `Float` until it closes. The count decays on quiet ticks.

### 8.6 User drags

`MOVESIZESTART` marks the window as interacting and holds off arranging its tree. On `MOVESIZEEND`, compare the rectangle with `Target`, taking the inset into account:

| What changed | Action |
| --- | --- |
| Nothing, within tolerance | Nothing |
| Size | For each edge that moved, `LayoutTree::MoveEdge`. Arrange the tree. A corner drag moves two edges. |
| Position only | As the drop preview showed (below), or back where it was if it showed nothing |
| It ended maximized or snapped by Windows | Maximized: leave it alone. Snapped: the reconcile tick puts it back. |

The gap is the drag handle, with nothing extra to build: it works through the windows' own invisible resize borders, 9 px wide at 150%. A pixel the borders do not reach falls through to the desktop, where a press does nothing. Measured at 150%:

| Gap | Side by side: pixels that resize | Stacked: pixels that resize |
| --- | --- | --- |
| 8 DIPs, 12 px (default) | All 12 | 9 of 12 |
| 12 DIPs, 18 px | All 18 | 9 of 18 |
| 16 DIPs, 24 px | 18 of 24 | 9 of 24 |
| 32 DIPs, 48 px | 18 of 48 | 9 of 48 |

Side by side, both windows contribute, and whichever receives the drag, the same split moves. Stacked, only the upper window's bottom border reaches into the gap, since the top inset is 0. The lower window's top edge is grabbed just inside the window, directly below the dead pixels.

The gap setting therefore stops at 12 DIPs, the widest gap with no dead pixels side by side. The gap line (section 9.7) covers the dead pixels once it has appeared. Raise the limit only if M4 shows the line is reliable, because the first moments of a hover and the case with the line turned off still depend on these figures.

**Dragging a window into place.** While a tiled window is dragged by its title bar, every area of the layout is outlined, and the area under the pointer is frosted, as Snap's preview is. That is all: a drag has one choice to make, which area, or none. In a margin the nearest area counts.

**Dropping a window out of the layout.** Near a gap between two areas, nothing is frosted. The zone is the gap and a third of each neighbouring area's length across it (`LayoutTree::NearGap`), so it is there even with no gap, and the margin at the screen edge is not part of it. A window let go there leaves the layout and floats where it was dropped, until it closes. Dragged again, it shows the areas like a tiled window: near a gap it goes on floating, and dropped on an area it rejoins the layout there. Windows floating for any other reason, by an app's rule or because they kept moving themselves, are not offered this.

Dropping puts the window in that area, in front of whatever is there. If that leaves the area it came from empty, a window from the area it joined moves over, so with one window in each the two trade places. An area with nothing in it stays empty and can be dropped on.

- It works across monitors, since each tile belongs to whichever tree is under the cursor.
- A drag is a move once opposite edges have both moved, so a resize, which always leaves one edge of each pair still, never shows the areas. Size cannot be the test. Windows gives a window it snapped its old size as soon as a drag begins, and does the same for one pulled out of maximized, and a change of DPI between monitors resizes a window on the way. Judging by size missed a real drag of every such window once.
- The cursor is read every 30 ms, only for the length of the drag.
- The guide sits above the dragged window and is click-through. It covers the work area with per-pixel alpha, so it is redrawn only when what it shows changes, and exists only during a drag.
- The preview is one window, like the gap line, with `WS_EX_TRANSPARENT` added so it never catches the mouse. It is frosted glass with rounded corners, as Windows' own Snap shows, directly below the dragged window rather than above it. Create it when first needed and destroy it when the drag ends.
- The frost comes from `SetWindowCompositionAttribute`, which is undocumented, so it is looked up at run time and the preview falls back to a plain translucent one. The documented acrylic backdrop turns solid on an inactive window, and this window must never be active.
- Esc cancels, as in any Windows drag: the window returns, `MOVESIZEEND` finds it at `Target`, and the first row of the table above applies.
- Floating windows show no preview, but for those dropped out of the layout as above.

**Keeping Windows' Snap out.** Snap answers the same drag: it shows its own preview and takes the window when released. So while arranging is on, `Native/WindowDocking` turns docking off, and turns it back on when arranging goes off and on quit.

- It has to be off before the press, so it cannot be turned off for one drag. Floating windows lose drag-to-Snap too while arranging is on.
- The change is for the session only: no `SPIF_UPDATEINIFILE`, so the saved preference in `HKCU\Control Panel\Desktop` is never touched. Windows' Settings page goes on showing the user's choice.
- It is restored to that saved preference, not to what was found, because after a crash what was found is what the last run left. The next start of Ember Pane puts it right, and so does signing out.
- If the user has Snap off already, nothing changes.

**On a window's own edge, neighbours follow on release.** On the gap line they follow live (section 9.7). Live following for edge drags can come after v1, using an `EVENT_OBJECT_LOCATIONCHANGE` hook on the dragged window's process only, installed for the length of the drag.

**Minimum size for a tile**: 120 DIPs in each direction, or the window's learned minimum if larger.

### 8.7 Monitors and virtual desktops

Trees are keyed by `{ monitor key, desktop GUID }`.

- **Monitor key**: the stable `DeviceKey` from AstroDimmer's `DisplayInfo::Targets()` and `MonitorMatcher::KeyOf`, not the HMONITOR, which does not survive a replug.
- **Desktop**: `IVirtualDesktopManager::GetWindowDesktopId`, from `CoCreateInstance(CLSID_VirtualDesktopManager)`. It is the documented interface. The undocumented ones that can list and switch desktops change between Windows builds, and nothing here needs them. On failure use `GUID_NULL`. Recreate the object after an RPC failure, since it dies with Explorer.
- **Moves between desktops**: there is no event for this. Check a window's desktop on `UNCLOAKED` and `FOREGROUND`, and move it between trees if it changed.
- **Monitor removed**: keep its trees, marked parked. Move its windows to the primary monitor's tree. When the monitor returns, move them back and restore the ratios, so that docking is painless.
- **Display changes** use `SystemEvents::DisplaysChanged`, already debounced by 2 s for bursts, plus `WorkAreaChanged` and the signature check.

### 8.8 Windows it cannot move

A non-elevated process cannot move an elevated window. Detect this by read-back, not by predicting it: `SWP_ASYNCWINDOWPOS` may report success for a request that is dropped. After two unmoved read-backs, set `State = Immovable` and `Disposition = Float`, and give up its tile. The panel marks the row "Running as administrator: can't be arranged". Reading the exe name may fail for the same reason, in which case the row shows the title.

### 8.9 CpuSampler

Uses documented APIs only. `NtQuerySystemInformation` would read everything in one call, but its CPU fields are not in the public header.

- **Each tick**: `GetProcessTimes` on cached handles, opened with `PROCESS_QUERY_LIMITED_INFORMATION`, for the processes of tracked families. Pass the samples to `CpuMeter`.
- **Every 10 s, and when the tracked set changes**: a `CreateToolhelp32Snapshot` to rebuild families and to open or close handles. Doing it every tick would be needless work with about 700 processes running.
- **A process that cannot be read**: the family continues without it. If its root cannot be read, `Known` is false, and the frame and panel say so.
- **While the display is off**: stop the timer, from `SystemEvents::DisplayPowerChanged`. Clear the ring when it comes back.

Two windows of one app share a family, so they show the same figure and colour: the figure is what the app is taking, not the window. Telling them apart is not possible from outside the app. Measured on 2 November 2026 with VS Code: 8 windows, all owned by one process, each with the same two child windows, one in that process and one in the shared GPU process. Its work runs in 91 processes, with renderers from 0.001 to 0.39 of a core, but nothing links a renderer or an extension host to a window. Guessing, by start time or otherwise, is ruled out: there were 17 renderers for 8 windows, and a colour that names the wrong window is worse than one that names the app. Nor is there a signal to pair them by. A renderer's priority and power state do change, but with 7 of 8 VS Code windows open and most of them covered, 12 of 13 renderers were at normal priority, so being covered does not show. Only a measure of what each window repaints could split the figure, as an estimate.

### 8.10 FrameService

```cpp
void FrameService::Paint(ManagedWindow& window, Core::FrameColor color);
void FrameService::Round(ManagedWindow& window);
void FrameService::Release(ManagedWindow& window);
```

`Paint` writes `DWMWA_BORDER_COLOR` only if the colour differs from `Painted`. `Core::FrameColor` becomes a `COLORREF`, which is `0x00BBGGRR`, not RGB.

| Window | Border |
| --- | --- |
| Tile or Float, rate at or above idle | Ramp colour |
| Already coloured, rate from 0.01 up to idle | First stop's colour |
| Idle, or usage unknown | `DWMWA_COLOR_DEFAULT` |
| Minimized, Maximized, Fullscreen | Nothing written |
| Frames turned off | `DWMWA_COLOR_DEFAULT`, once |

A window Windows marks as snapped stays square whatever is asked, so `Place` clears the mark first, and the reconcile tick catches one snapped later, three tries per window. `Round` sets the Corners setting's preference while arranging is on, and Windows' own while it is off: showing how busy apps are reshapes nothing. The docs call this a hint: windows using per-pixel alpha layering or window regions cannot be rounded, and maximized or snapped windows are not.

`Release` writes `DWMWA_COLOR_DEFAULT` and `DWMWCP_DEFAULT`. Call it when a window leaves management and for all windows on quit.

**A colour outlives us** (section 3), so a crash or a kill leaves frames as they were. Therefore:

- `Painted` starts empty, so adoption always writes, and restarting cleans up after a crash.
- The installer asks the app to quit with `--quit` before it falls back to killing it (section 12).
- Neither attribute can be read back, so an app's own custom border colour is lost until it sets it again. The apps affected are few, and it is why a rule of `Ignore` leaves a window fully alone.

Overriding the border also means a coloured window no longer shows Windows' accent colour when active. Its title bar still shows focus.

### 8.11 Lifecycle

- **Start**: Mutex, then trace, settings, tracker, tray. If no settings file exists, open the panel.
- **Quit**: From Settings or `--quit`: `Release` every window, unhook, stop the UI, remove the icon.
- **Explorer restarts**: `TrayIcon` re-adds itself, and the virtual desktop object is recreated on its next failure.
- **Privacy**: No network access. `trace.txt` records exe and class names, never window titles. `--probe-windows` includes titles, since a user runs it deliberately, and says so.

## 9. Interface

The look and code structure are AstroDimmer's: a stock WinUI Quick Settings panel, a Settings window of cards on Mica, one page.

### 9.1 Tray

Left-click toggles the panel; right-click opens Settings, as `TrayIcon` already does. Pick a Segoe Fluent Icons glyph present in Segoe MDL2 Assets too, and a second state for paused. `TrayIcon::SetGlyph` handles the change.

### 9.2 Panel

```
┌────────────────────────────────────────┐
│ ┌──────────┐ ┌──────────┐ ┌──────────┐ │
│ │ ████████ │ │ ████ ███ │ │ ██ ██ ██ │ │
│ │ One at   │ │   Two    │ │  Three   │ │
│ │ a time   │ │ columns  │ │ columns  │ │
│ └──────────┘ └──────────┘ └──────────┘ │
│ ┌──────────┐ ┌──────────┐ ┌──────────┐ │
│ │ █████ ██ │ │ █████ ▄▄ │ │ ███  ███ │ │
│ │ Large,   │ │ Large,   │ │  Four    │ │
│ │ small    │ │ two small│ │  areas   │ │
│ └──────────┘ └──────────┘ └──────────┘ │
├────────────────────────────────────────┤
│                                     ⚙  │
└────────────────────────────────────────┘
```

- The panel offers layouts and nothing else, not windows. Each is a small picture drawn from the preset's own areas, so it cannot disagree with what it does, and the one in use is in the accent colour.
- Pressing a layout applies it to every monitor at once and turns arranging on, since choosing one says that you want it. Besides that, arranging is turned on and off in Settings, along with the busy borders and the gap.
- Changing layout deals the windows into the new areas frontmost first, so the windows in use are the ones on show. Sizes dragged in the old layout are not kept.
- The busy words have left the panel with the window list. The border colours stay, and the key in Settings still gives each colour its word.
- Per-window choices have left it too. An app can be kept out of the layout under "Apps with their own rule" in Settings.

### 9.3 Settings

| Group | Cards |
| --- | --- |
| General | Arrange windows; Start with Windows; Quit |
| Layout | Gap between windows, 0 to 12 DIPs (section 8.6 explains the limit); Space at the screen edge, 0 to 32 DIPs; Corners: Rounded, Small, Leave as they are; Show a line between windows on hover |
| Show how busy apps are | On or off; Colours: Bright, Balanced, Ember, Aurora; width of the coloured frame, 1 to 6 px; dark line inside the frame, 0 to 4 px; See-through line, off by default; Colours change: Quickly, Normally, Slowly (5, 10 or 30 seconds); a key showing each colour of the chosen theme with its word |
| Exceptions | One row per rule: the app, and a choice of Arrange it, Leave it where I put it, or Leave it alone (`Tile`, `Float`, `Ignore`); Add, offering the apps now running |

Version text comes last, as in AstroDimmer.

### 9.4 Switches

`--show`, `--pin`, `--settings`, `--theme=dark|light` and `--lang=de` carry over. New ones:

| Switch | Effect |
| --- | --- |
| `--probe-windows` | Writes `windows-probe.txt`, with every top-level window's facts and disposition, and exits. The first tool to reach for when an app behaves oddly. |
| `--quit` | Finds a running host with `FindWindowExW(HWND_MESSAGE, ...)` and sends it `quit` |
| `--no-arrange` | Runs with arranging off, whatever the settings say |
| `--only-title=text` | Manages only windows with this in their title, so that arranging can be tried on test windows while real ones stay exactly where they are |
| `--arrange` | Arranging on for this run, saved nowhere. Only works beside `--only-title`, so it can never rearrange a real desktop by accident. |

### 9.5 Languages

Start with `en-US`, and keep every string in `Strings/en-US/Resources.resw` from the first commit. Add AstroDimmer's other 18 languages at M7, once the wording has stopped moving.

### 9.6 Frame widths and the dark line

DWM's border cannot be widened, narrowed or moved, and it is 2 px at 150%. Settings gives two widths in real pixels, default 2 and 2: the colour, and a dark line between it and the window. `Core::PlanFrame` works out the rest, outward from the window's visible edge, with Windows' border lying from `-border` to 0:

- With no line and a colour no wider than Windows' border, the border is the frame. Nothing is drawn and nothing watches anything.
- With a line, the border goes `#1F1F1F` and the colour starts `line` pixels in from the border's inner edge. The defaults at 150% give the whole border, dark, then two pixels of colour just outside it. A line of 1 and a colour of 2 give one dark pixel, then two of colour, covering the border's outer pixel and one pixel beyond. A line wider than the border is drawn past it.
- The See-through line setting, off by default, turns the border off with `DWMWA_COLOR_NONE` and changes nothing else, so the colour stays where it is. Turned off, the border's strip does not go away: it shows what is behind the window. On real apps it reads the same as the gap; on WinForms test windows it is partly grey. Windows stacked behind have theirs turned off too, or the strip would show them, as a light line round a dark window. A line wider than the border is left empty past it.
- Each window has half the gap. Past that the colour narrows, and with no room for it at all the border is the frame.
- Only windows in a layout get more than the border. A floating window has no gap to draw in, and one behind others would have its frame drawn across them.
- A resting window, and one whose load is unknown, keeps Windows' border.
- So does a window stacked behind another in a shared area. Windows' default border is partly see-through, so the colour of a window behind showed in the border of the one in front, as a second coloured line with See-through line on. It takes its colour on coming to the front.

`Managed::FrameRings` decides this for each window, and `FrameLayer` draws it: one click-through, per-pixel-alpha window per monitor, with software Direct2D, as for the drop guide. A trial on 2 November 2026 settled how:

- **One layer per monitor, not one window per app.** There is one position to keep instead of a window for each, and hiding on minimize, fullscreen and desktop switches comes down to what is drawn. `Arranger::OnShow` gives the front window of each area on the desktop being looked at, leaving out one being carried.
- **In front of the windows, not behind.** A window's shadow falls on what is behind it, and took colours down by up to 40%: `#E03131` read as `#7F1C1C`. The layer sits directly in front of the frontmost window of the layout, so windows further forward stay in front of it, and it follows on activation. It must never be placed after a topmost window, or it becomes topmost.
- **Clicks pass through.** In a gap they land on a window's resize border, or the gap line's handle, as before.
- **Never the whole monitor.** A window exactly covering a monitor reads as a fullscreen app, even with nothing to see, and within seconds Windows takes the taskbar's topmost status away. It drops behind the windows, and their shadows fall on it. The layer covers the work area, less one pixel where that is the whole monitor, as with an auto-hiding taskbar.
- **Bursts, not polling.** Frames follow at 30 times a second for half a second after anything moves, and for as long as a window or gap is in the user's hand. Otherwise only the 2 s tick looks. The drawing surface, a monitor's worth of pixels, is let go when things go quiet.
- A window stays on the virtual desktop it was made on, so a layer left on another desktop is made again on this one.

Not yet known: what a layer in front of the windows does to video and games, and to apps that slow down when they think they are covered. Setting the dark line to 0 takes the layer away.

### 9.7 The gap line

With the cursor resting in a gap, a line appears along it. The line is the handle: pressing anywhere across the gap and dragging resizes the windows on both sides, and they follow live.

A line that only showed would be misleading, since section 8.6 shows pixels in the gap where a press does nothing. Making it the handle also removes those pixels, works beside apps with no invisible border, and gives live following without watching other apps' windows.

**Finding the gap.** A 100 ms `DispatcherQueueTimer` reads `GetCursorPos` and calls `LayoutTree::GapAt` on the tree under the cursor. The line appears after two hits in a row, so that crossing a gap on the way somewhere does not flash it. The cost is about ten wake-ups a second, for which section 14 sets the limit.

- The timer runs only with arranging and the line both on, the display on, and at least one tree holding two tiles.
- `WindowFromPoint` must be one of the two neighbours or the desktop. Otherwise a floating or maximized window is covering the gap, and nothing appears.
- `WH_MOUSE_LL` is not an option: every mouse movement on the system would pass through this thread, and any stall would lag the pointer. A permanent window per split is not either, since section 9.6's costs would apply to each.

**The window.** One window at most, owned by `GapLine`:

- `WS_POPUP` with `WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TOPMOST`, so a press does not take focus. It is topmost only while shown, and the check above has just established that nothing is in front.
- It covers the whole gap between the tiles on each side, drawn once with `UpdateLayeredWindow`: alpha 1 across the gap, so the desktop shows through but the mouse is caught, and a solid line 3 DIPs wide down the centre.
- The colour is the Windows accent colour from `DwmGetColorizationColor`, not one from the heat scale, so it cannot be read as a CPU signal.
- It sets the ↔ or ↕ cursor itself.
- **Create it on show and destroy it on hide.** A window kept alive belongs to the virtual desktop it was created on, and it would have a stale size and DPI. Creation is cheap, and the bitmap is a few tens of kilobytes.

**Hiding.** The timer does this too: when the check fails, because the cursor has left or something has moved in front, the line goes. `TrackMouseEvent` cannot be used: a window appearing under a cursor at rest is told at once that the cursor has gone, since Windows only updates that on movement.

**Dragging.** On `WM_LBUTTONDOWN`, call `SetCapture`. The line stays up for the whole drag. On `WM_MOUSEMOVE`, at most 30 times a second, call `LayoutTree::MoveSplit` and place the windows of both subtrees (section 8.4), moving the line with them. On release or `WM_CAPTURECHANGED`, stop and let the normal settle run. A window that refuses to shrink stops the split, as minimums do everywhere.

Windows' own edge drags (section 8.6) still work beside this. They are what you get before the line has appeared, or with it turned off.

### 9.8 Plain language

Everything a person reads must make sense to someone who has never opened Task Manager. These words stay in the code and never reach the screen: CPU, CPU-second, core, process, thread, tile, float, split, DIP.

| In the code | On screen |
| --- | --- |
| CPU frames | Show how busy apps are |
| Rate | The colour, and the key in Settings that gives each colour its word |
| `Tile`, `Float`, `Ignore` | Arrange it, Leave it where I put it, Leave it alone |
| Margin | Space at the screen edge |

The running total is not shown anywhere. `CpuMeter` keeps it for `windows-probe.txt` and the log, for diagnosing the app.

## 10. Link

Same transport as AstroDimmer: JSON objects with a `type`, sent by `WM_COPYDATA` between message-only windows, queued on arrival and handled from the message loop. An `id` is the HWND as a number.

| Direction | Type | Fields |
| --- | --- | --- |
| UI to host | `hello` | `hwnd` |
| | `saveSettings` | `settings`. Also carries the layout chosen in the panel. |
| | `quit` | |
| Host to UI | `state` | `settings`, `windows`, `status` |
| | `windows` | `windows`: `app`, `exe`, for choosing an app in Settings |
| | `settings` | `settings` |
| | `status` | `text` |
| | `toggle`, `openSettings`, `quit` | |

## 11. Settings

`%APPDATA%\EmberPane\settings.json`. Loading is forgiving and saving is atomic, as in AstroDimmer: a damaged file gives defaults, not a failure to start.

```json
{
  "Arrange": { "Enabled": false, "Layout": "TwoColumns", "GapDip": 8, "MarginDip": 8, "Corners": "Round", "GapLine": true },
  "Frames": { "Enabled": true, "WindowSeconds": 10 },
  "Rules": [
    { "Exe": "calculatorapp.exe", "Mode": "Float" }
  ]
}
```

`Layout` is `OneAtATime`, `TwoColumns`, `ThreeColumns`, `LargeAndSmall`, `LargeAndTwoSmall` or `FourAreas`. `Corners` is `Round`, `RoundSmall` or `Leave`. `Mode` is `Tile`, `Float` or `Ignore`. A rule may add `"Class"` and `"Title"`, the latter matching as a substring. A change arrives as `saveSettings` and is applied at once: the host re-classifies and arranges.

## 12. Build, packaging and release

The same as AstroDimmer unless listed. Same `packages.config`, Visual Studio 2026, x64 and ARM64, unpackaged, self-contained Windows App SDK, hybrid CRT, tests run by `build.ps1`.

| File | Change |
| --- | --- |
| `.vcxproj` | `WindowsTargetPlatformMinVersion` 10.0.22000.0. Link `dwmapi.lib`, already present. Drop `dxva2.lib` and `wbemuuid.lib`. |
| `build.ps1` | Keep `-Installer` and `-Zip`. Drop `-Store`. The log list becomes `trace.txt`, `diagnostics.txt`, `windows-probe.txt`. |
| `installer/EmberPane.iss` | `MinVersion=10.0.22000`. New `AppGuid`. `StopApp` first runs `{app}\EmberPane.exe --quit`, waits briefly, and only then uses its existing forced stop, so that frames are reset. |
| `build.yml` | Keep the build, version check and draft release. Drop the Store artifact. Signing stays off until SignPath is set up. |

winget follows the first stable release, as AstroDimmer's did. The Microsoft Store gets an MSIX of the same build, as AstroDimmer's does: see [packaging/store/README.md](../packaging/store/README.md).

The folder is not yet a git repository. `git init` is the first step of M1.

## 13. Risks

### 13.1 Open technical risks

| Risk | Consequence | Handling |
| --- | --- | --- |
| Some custom-framed apps show no border or corners | The frame is missing on those apps | Chrome passes (section 3). VS Code and Office are still to check. If one fails, it floats without a frame. |
| 2 px is too faint | The main visual feature goes unnoticed | The gate in M2 |
| Apps resist being placed | Flicker | Settling caps retries; stubborn windows float |
| Windows' own Snap fights the tiling | Two previews for one drag | Settled: docking is off while arranging is on (section 8.6). `Win`+arrow keys are still to check. |
| Undocumented desktop behaviour | A window ends up in the wrong tree | Only the documented interface is used; the worst case is one misplaced window |

### 13.2 What the app will not do well

Administrator windows float. Dialogs are not tiled. A window that insists on being large overlaps its neighbour until the tree is resized.

### 13.3 The name

The app is **Ember Pane**, and `EmberPane` in code, paths and the exe. An ember glows hotter as it burns and a pane is a window, which covers both halves of the app.

On 2 November 2026, no published app of that name appeared in the Microsoft Store or winget catalogs. Those searches cover only part of the Store and cannot see reserved names, and no trademark search has been done. So:

- **Reserve the name in Partner Center before M1**, because the rename runs through everything. If it is taken, choose again before any code is written.
- Do a trademark search before the first public release.
- `C:\Dev\Window Manager` keeps its old folder name until someone renames it, which is simplest before `git init` in M1.

The settings path and `AppGuid` must not change after the first release.

## 14. Budgets

The tool measures CPU, so it should be seen to use little.

| Measure | Target |
| --- | --- |
| Host CPU, 20 windows, nothing changing, gap line on | Under 0.01 CPU-seconds per second, so it reads idle on its own scale |
| Hover to line visible | 100 to 200 ms |
| Gap line drag | Neighbours follow at up to 30 updates a second |
| Host private memory | Under 5 MB |
| One sampling tick | Under 5 ms |
| A tick with a process snapshot, every 10 s | Under 40 ms |
| Window shown to window placed | Under 100 ms |
| Frame leaves idle after a load starts | Within 2 ticks, 4 s |
| Frame reaches its full colour | After `WindowSeconds`, 10 s |
| UI process, panel closed | None running |

`WriteDiagnostics` records the host's memory and the trace records tick times, so these are checked from a real run.

Measured on a Release build at M2, with 13 windows and about 700 processes:

| Measure | Result |
| --- | --- |
| Host CPU | 0.002 CPU-seconds per second |
| Private memory | 3.6 MB |
| Working set | 27 MB, most of it system libraries shared with other processes |
| First tick | 21 ms |
| Slowest ordinary tick | 3.3 ms |
| Slowest tick with a snapshot | 32 ms |
| Host CPU with arranging and the gap line on | 0.0027 CPU-seconds per second |
| Time to exit after `--quit` | About 0.2 s |

The first budgets, 1 ms and 20 ms for ticks and a 15 MB working set, were estimates from a small benchmark, and the real figures missed them. The tick budgets above were raised to fit, and memory now counts what is private to the app, because overall CPU is a fifth of its budget and none of these causes a visible delay.

## 15. Testing

**Core**, in `tests/EmberPane.Tests`, with AstroDimmer's harness and sentence-style names. Add tests with each piece. These must exist:

| Area | Cases |
| --- | --- |
| `LayoutTree` | `A_single_window_fills_the_area`; `Two_windows_are_separated_by_exactly_the_gap`; `Four_windows_insert_into_a_grid`; `Removing_a_window_gives_its_space_to_its_sibling`; `The_gaps_are_equal_at_every_scale`; `Tiles_never_leave_the_area` |
| Resizing | `Dragging_a_right_edge_moves_the_split_beside_it`; `A_corner_drag_moves_two_splits`; `An_edge_against_the_screen_moves_nothing`; `A_minimum_size_stops_the_split` |
| Gap line | `A_point_in_a_gap_finds_its_split`; `A_point_on_a_tile_finds_nothing`; `The_outermost_split_wins_where_gaps_cross`; `Moving_a_split_resizes_both_sides` |
| Minimums | `A_window_too_large_for_its_tile_widens_it`; `A_full_tree_refuses_a_new_window`; `An_impossible_layout_still_arranges` |
| `WindowRules` | Each row of the table in section 7.2, and `A_rule_can_tile_an_app_with_no_caption` |
| `CpuMeter` | `A_child_exiting_does_not_make_usage_negative`; `A_child_joining_does_not_spike_usage`; `A_gap_clears_the_window`; `Two_cores_read_as_two`; `A_first_sample_is_not_known` |
| `HeatRamp` | `Below_idle_uses_the_system_border`; `A_lit_frame_stays_lit_just_below_idle`; `Each_stop_gives_its_colour`; `A_small_change_stays_on_one_level`; `Each_word_starts_at_its_stop` |
| Dragging | `Dropping_beside_a_window_splits_it_on_that_side`; `The_place_it_left_closes_up`; `A_window_can_move_to_another_tree`; `A_window_cannot_be_dropped_on_itself` |
| `Settings` | Round trip, `A_corrupt_file_gives_defaults`, unknown fields ignored |

**By hand**, each milestone, on a real desktop:

| Apps | Machine states |
| --- | --- |
| Notepad, File Explorer, Calculator, Settings (UWP) | 100%, 150%, 200% |
| Chrome or Edge, VS Code, Teams or another Electron app | Two monitors with different scales |
| Office, Windows Terminal, classic console | Vertical taskbar, auto-hide taskbar |
| Task Manager (elevated), a game, a fullscreen video | Two virtual desktops, dock and undock, sleep and wake |

## 16. Milestones

Each ends with something usable and a check that can fail.

| # | Build | Done when |
| --- | --- | --- |
| M0 | Settle the unknowns in section 3 with small scripts | Every row has an answer on real apps, including one UAC-approved elevated window, and the frame decision is confirmed or changed |
| M1 | Skeleton: `git init`, the renamed solution, tray, panel, Settings, Core, tests | `.\build.ps1` passes. The icon, panel and Settings work, with nothing to show yet. |
| M2 | `WindowTracker`, `CpuSampler`, `FrameService`, `--probe-windows`. **Moves nothing.** | Every open window is listed correctly, frames change with real load, and quitting restores them. |
| M3 | `LayoutTree`, `Arranger`: existing and new windows are placed | Switching on arranges all open windows on one monitor with even gaps, and windows opening, closing and minimizing reflow correctly |
| M4 | Dragging with the drop preview, the gap line, learned minimums, stubborn windows, immovable windows | Dragging any edge moves its neighbour; the line appears in every gap and drags live, stacked and side by side, beside custom-framed apps too; the preview shows exactly where a dragged window lands, beside a window or in its place, across monitors; Esc cancels; no app can cause an endless fight |
| M5 | Monitors and virtual desktops | The hand-test table passes on two monitors of different scales, including undock and redock |
| M6 | Final panel, Settings and exceptions | All of it works without editing JSON |
| M7 | Installer, zip, CI, README, languages | A clean machine installs, upgrades and uninstalls with no admin prompt and no frames left coloured. |

M2 comes before M3 on purpose: frames are harmless and exercise the riskiest code, the window tracking, before anything moves.

### Where this stands

All seven milestones are built, and the design has since moved to predefined layouts. On 2 November 2026 the Release build for x64 passed its unit tests, 19 live checks of the layouts including a real drag with the frame layer up, and live checks of the frames reading real pixels. Those checks used throwaway windows and `--only-title`, with every other window compared before and after. The installer and zip are made for x64. The Debug and ARM64 builds predate the frame widths and themes and have not been rebuilt.

Two lessons from getting there:

- A dependency property on a custom control exists only once something has asked for it, and a template binding to one never touched fails at random and takes the UI process with it. `OffsetStepper` and `SettingsCard` now ask for all of theirs in their constructors. Any new control must too.
- Tests of window behaviour need a real negative check. Comparing every other window before and after is what proved `--only-title` holds.

Not yet checked, and for the first real use to settle:

| Not checked | Why |
| --- | --- |
| Arranging real apps | Deliberately left for the first real use |
| The installer running | It is built, but installing was left to the user |
| Real mouse drags on real apps | Real drags on throwaway windows passed in 6 runs of 7: guide, preview, landing, and Snap kept out. One early run, before drags were logged, showed no preview and did not land, and the cause was not found. Resizing and the gap line were still simulated. |
| Keyboard Snap beside tiling | `Win`+arrow keys have not been tried with docking off |
| Snap Layouts at the top of the screen | Dragging there no longer maximizes; whether the layout bar still appears has not been looked at |
| More than one monitor, and docking | The logic is written; the tests ran on one monitor |
| Virtual desktops | The logic is written; not exercised |
| Elevated windows, VS Code, Office | Open from section 3 |
| ARM64 running | It is only compiled |
| The exceptions list in Settings | Seen on screen, not driven |
| The 18 translations | Machine-written and loaded, not read by native speakers |

## 17. References

Microsoft documentation:

- [DWMWINDOWATTRIBUTE](https://learn.microsoft.com/en-us/windows/win32/api/dwmapi/ne-dwmapi-dwmwindowattribute): border colour, corner preference, border thickness, cloaking
- [Apply rounded corners in desktop apps](https://learn.microsoft.com/en-us/windows/apps/desktop/modernize/ui/apply-rounded-corners): when windows are and are not rounded
- [SetWinEventHook](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setwineventhook): out-of-context events and the message loop they need
- [GetProcessTimes](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-getprocesstimes): access rights and units
- [IVirtualDesktopManager](https://learn.microsoft.com/en-us/windows/win32/api/shobjidl_core/nn-shobjidl_core-ivirtualdesktopmanager): the documented desktop interface

Other tiling managers on Windows, as precedent:

- [GlazeWM](https://github.com/glzr-io/glazewm): DWM border colours and corner styles, Windows 11 only ([sample config](https://github.com/glzr-io/glazewm/blob/main/resources/assets/sample-config.yaml)). Width is an [open request](https://github.com/glzr-io/glazewm/issues/1390).
- [komorebi](https://lgug2z.github.io/komorebi/cli/border-implementation.html): offers both the thin Windows border and an adjustable one of its own
