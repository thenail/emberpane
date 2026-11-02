# Microsoft Store

The Store package is an MSIX of the same build the installer and the zip take. `build.ps1 -Store` copies the files into `build\store\layout\<platform>`, adds [Package.appxmanifest](Package.appxmanifest) (as `AppxManifest.xml`) and the logos in [Images](Images), and packs them:

```powershell
.\build.ps1 -Platform x64 -Store
.\build.ps1 -Platform ARM64 -Store
```

This gives `build\store\EmberPane-store-<version>.msix` and `EmberPane-store-ARM64-<version>.msix`. CI builds both for every run as its `store` artifact. They are unsigned: the Store signs what it publishes.

The app finds out at run time that it is packaged ([Native/Shell.cpp](../../src/EmberPane/Native/Shell.cpp)), and then does two things differently:

- **Start with Windows** is the manifest's `StartupTask`, not the Run key. A package's registry writes are private to it, so a Run value it wrote would never be seen. Once the user turns it off in Windows' Startup apps, only they can turn it back on there, and the switch in Settings shows that.
- **The logs** (`trace.txt`, `diagnostics.txt` and the probes' reports) go beside the settings in `%APPDATA%\EmberPane`, because the install folder is read-only. Settings from an installer or zip copy are picked up as they are.

Store ID **9MXCXN7NR110**; once live, the app's page is `https://apps.microsoft.com/detail/9MXCXN7NR110`.

## First submission

1. The name **Ember Pane** is reserved, and [Package.appxmanifest](Package.appxmanifest) holds the identity from **Product management > Product identity** in [Partner Center](https://partner.microsoft.com/dashboard/apps-and-games/overview).
2. Build the packages, or download the `store` artifact of the latest CI run.
3. Start a submission:
   - **Pricing and availability**: Free.
   - **Properties**: category *Utilities & tools*. Privacy policy URL: `https://github.com/thenail/emberpane#privacy`. Website: `https://github.com/thenail/emberpane`. Support: `https://github.com/thenail/emberpane/issues`.
   - **Age ratings**: the questionnaire. No user interaction, sharing of location or purchases gives the lowest rating.
   - **Packages**: upload both `.msix` files. Leave the device families at Desktop.
   - **Submission options**: the manifest's `runFullTrust` is a restricted capability and needs a reason. For example: *Ember Pane is a desktop tray app built with WinUI 3 and Win32. It moves and resizes other apps' windows, colours their borders through the Desktop Window Manager, and reads their CPU time. These Win32 APIs run only in a full-trust process.*
   - **Store listings**: English first, and any of the app's other 18 languages you want. Description, feature list and screenshots can come from the [README](../../README.md). The Store wants at least one screenshot, 1366×768 or larger.
4. Submit. Certification usually takes a few days.

## Later versions

Build (or download from CI) the packages for the new version, start a new submission from the last one, and replace the two packages. The version comes from `EmberPane.rc`, as always: the Store refuses a package whose version is not higher than the one before.

## Testing a package locally

Windows installs only signed packages, apart from a folder registered in Developer Mode (**Settings > System > For developers**). With that turned on, and no other copy of Ember Pane running:

```powershell
Add-AppxPackage -Register build\store\layout\x64\AppxManifest.xml
```

Start it from the Start menu. To remove it: `Get-AppxPackage 10870thenail.EmberPane | Remove-AppxPackage`.

The Windows App Certification Kit runs the checks the Store runs, from an elevated prompt:

```powershell
& "${env:ProgramFiles(x86)}\Windows Kits\10\App Certification Kit\appcert.exe" test -appxpackagepath build\store\EmberPane-store-<version>.msix -reportoutputpath build\store\wack.xml
```
