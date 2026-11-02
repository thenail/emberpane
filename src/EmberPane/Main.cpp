#include "pch.h"
#include "App.xaml.h"
#include "CommandLine.h"
#include "Host.h"
#include "Link.h"
#include "Probes.h"
#include "Strings.h"
#include "Trace.h"

#include <DispatcherQueue.h>
#pragma comment(lib, "CoreMessaging.lib")

namespace
{
    /// The panel and Settings. What XAML would generate as the entry point,
    /// written out: the generated one, renamed under DISABLE_XAML_GENERATED_MAIN,
    /// decides whether to create the App by a check C++/WinRT types fail, and
    /// then starts XAML with no application at all.
    int RunUi()
    {
        winrt::init_apartment(winrt::apartment_type::single_threaded);
        winrt::Microsoft::UI::Xaml::Application::Start([](auto&&)
        {
            winrt::make<winrt::EmberPane::implementation::App>();
        });
        return 0;
    }

    /// --quit: asks a running host to quit. For the installer, which would
    /// otherwise have to kill it and leave every frame coloured: a colour
    /// outlives the process that set it. Before the mutex, since the host
    /// holds it, and before the trace, which is the running host's.
    int QuitRunningHost()
    {
        using namespace EmberPane;

        if (HWND host = FindWindowExW(HWND_MESSAGE, nullptr, Link::HostClass, nullptr))
        {
            Link::Endpoint endpoint(L"EmberPane.Quit", nullptr);
            endpoint.Send(host, Link::Make(Link::Type::Quit));
        }
        return 0;
    }

    void PumpMessages()
    {
        MSG msg;
        while (GetMessageW(&msg, nullptr, 0, 0))
        {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    /// The tray process: a Win32 message loop and the dispatcher queue
    /// Windows itself provides - not WinUI's, which would load WinUI.
    int RunHost()
    {
        winrt::init_apartment(winrt::apartment_type::single_threaded);

        DispatcherQueueOptions options{ sizeof(options), DQTYPE_THREAD_CURRENT, DQTAT_COM_NONE };
        winrt::Windows::System::DispatcherQueueController controller{ nullptr };
        winrt::check_hresult(CreateDispatcherQueueController(
            options, reinterpret_cast<ABI::Windows::System::IDispatcherQueueController**>(winrt::put_abi(controller))));

        // --probe-windows: a report, then exit. Before the trace is touched,
        // so probing beside a running instance leaves its log alone.
        if (EmberPane::CommandLine::Has(L"--probe-windows"))
        {
            EmberPane::Probes::WriteWindowProbe();
            return 0;
        }

        // One host per session: a second launch - the Start menu entry
        // clicked again, sign-in beside a copy started by hand - just exits.
        // Before the trace is touched, so the running instance keeps its log.
        // The handle stays open for the life of the process.
        CreateMutexW(nullptr, FALSE, L"Local\\EmberPane.Host");
        if (GetLastError() == ERROR_ALREADY_EXISTS)
            return 0;

        EmberPane::Trace::Clear();
        EmberPane::Trace::Log(std::wstring(L"startup: ") + GetCommandLineW());

        EmberPane::Strings::UseDisplayLanguage();
        EmberPane::Probes::WriteDiagnostics();

        {
            EmberPane::Host host(controller.DispatcherQueue());
            PumpMessages();
        }

        // Anything still queued runs down before the thread goes.
        DWORD thread = GetCurrentThreadId();
        controller.ShutdownQueueAsync().Completed([thread](auto&&, auto&&) { PostThreadMessageW(thread, WM_QUIT, 0, 0); });
        PumpMessages();
        return 0;
    }
}

int __stdcall wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
    using EmberPane::CommandLine::Has;

    // --ui: the panel and Settings, started by the host when they are asked
    // for. See Link.h for why they are a process of their own.
    if (Has(L"--ui"))
        return RunUi();

    if (Has(L"--quit"))
        return QuitRunningHost();

    return RunHost();
}
