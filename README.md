<div align="center">

# Ember Pane

**Your windows in order, their heat at a glance.**

Ember Pane lives on the taskbar. Pick a layout and it arranges the apps you have open, with a small margin around each. Each window's border shows how hard its app is working.

</div>

## What it does

- **Pick a layout.** One at a time, two or three columns, one large with one or two small, four areas, or one large in the middle. Windows' own snap layouts work too: a window snapped from its maximize button goes to the matching area. Everything you have open goes into it, including what was already running. With more windows than areas, they share one, in front of each other, and new windows fit in by themselves.
- **A little space around each.** Choose how much, between windows and at the edge of the screen. Corners stay rounded.
- **Resize by dragging between windows.** Rest the pointer in the gap and a line appears. Drag it, and the windows on both sides follow.
- **Move by dragging.** Drag a window by its title bar and the areas appear. Let go over one and the window goes there. It works across monitors.
- **See what is busy.** A window's border turns from blue through amber to red as its app works harder. Pick from four sets of colours in Settings, and choose how wide the frame is. If colours are hard to see on light windows, add a dark line between the window and the colour.
- **Stays out of the way.** Arranging starts off, so nothing moves until you ask. In Settings, any app can be left where you put it, or left alone. Turn arranging off and every window stays where it is.

Your monitors and virtual desktops each keep their own layout, and unplugging a monitor does not lose its layout.

## Install

Download the installer from the latest release:

| Your PC | File |
| --- | --- |
| Most PCs (Intel and AMD) | `EmberPane-setup-<version>.exe` |
| ARM-based PCs (e.g. Snapdragon) | `EmberPane-setup-ARM64-<version>.exe` |

It installs for your user account, with no administrator password needed. Ember Pane needs Windows 11.

The installer isn't code-signed yet, so Windows may show "Windows protected your PC" when you run it. Choose **More info**, then **Run anyway**.

### Without installing

Each release also has zips. Extract the whole zip anywhere and run `EmberPane\EmberPane.exe`. Settings are still kept in `%APPDATA%\EmberPane`, not in the folder. To remove it, quit Ember Pane from its Settings, turn off **Start with Windows** first if you turned it on, then delete the folder.

## Good to know

- **This is an early version.** It has been used on one monitor with one virtual desktop. More monitors, more virtual desktops, ARM-based PCs and the translations are built, but have not been tried yet. With the dark line or a wider frame, Ember Pane draws a click-through layer over your windows. If a video or game misbehaves, set the dark line to 0 in Settings and please open an issue.
- **Windows running as administrator can't be arranged.** Windows does not let an ordinary app move them. They stay where you put them.
- **Some apps have a minimum size.** Ember Pane learns it, and gives that window the room it insists on.
- **Colours are a guide.** One full processor core reads as *Busy*, whatever size your computer is. Two windows of one app show the same colour, because it is the app that is busy.

## Building from source

Needs Visual Studio 2026 with the *Desktop development with C++* workload and the *Windows App SDK C++* component.

```powershell
.\build.ps1                               # Debug x64, runs the tests
.\build.ps1 -Configuration Release -Platform ARM64
.\build.ps1 -Installer                    # Release build plus the setup exe
.\build.ps1 -Zip                          # Release build plus the zip
```

The app is C++/WinRT on WinUI 3. It runs as two processes from the same exe: the tray process (the windows, the icon) is plain Win32 and stays small, and the panel and Settings run in a WinUI process (`--ui`) that starts when you click the icon and exits when they close. `src/EmberPane.Core` holds the logic with no UI or hardware in it (the layout, the rules, the activity measure, settings), covered by `tests/EmberPane.Tests`. The design is written down in [docs/implementation-spec.md](docs/implementation-spec.md).

Handy switches: `--show` opens the panel at startup, `--pin` keeps it open, `--settings` opens Settings, `--theme=dark|light`, `--lang=de` and `--no-arrange`. `--probe-windows` writes `windows-probe.txt`, listing every window and what Ember Pane would do with it. It includes window titles, which nothing else writes down. `--only-title=text --arrange` arranges only windows with that text in their title, for trying things on a desktop whose real windows should stay put.

## Privacy

Ember Pane does not collect any data. It has no account, no analytics and no tracking, and it never connects to the network. Its log names apps, never window titles, and everything stays on your computer.

## License

[MIT](LICENSE)
