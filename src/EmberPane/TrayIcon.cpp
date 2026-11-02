#include "pch.h"
#include "TrayIcon.h"
#include "Trace.h"

#include <d2d1.h>
#include <dwrite.h>

namespace EmberPane
{
    namespace
    {
        constexpr UINT CallbackMessage = WM_APP + 1;
        constexpr UINT IconId = 1;

        /// The taskbar follows "Windows mode", not "App mode" - the two are
        /// separate settings, and a white glyph on a light taskbar vanishes.
        bool TaskbarIsDark()
        {
            DWORD value = 0, size = sizeof(value);
            LSTATUS status = RegGetValueW(HKEY_CURRENT_USER,
                LR"(Software\Microsoft\Windows\CurrentVersion\Themes\Personalize)",
                L"SystemUsesLightTheme", RRF_RT_REG_DWORD, nullptr, &value, &size);
            return status == ERROR_SUCCESS && value == 0;
        }

        /// Windows 11 ships Segoe Fluent Icons; Windows 10 only the older
        /// Segoe MDL2 Assets. The glyphs used are in both.
        wchar_t const* IconFontFamily(IDWriteFactory* factory)
        {
            winrt::com_ptr<IDWriteFontCollection> fonts;
            if (FAILED(factory->GetSystemFontCollection(fonts.put())))
                return nullptr;

            for (auto family : { L"Segoe Fluent Icons", L"Segoe MDL2 Assets" })
            {
                UINT32 index = 0;
                BOOL exists = FALSE;
                if (SUCCEEDED(fonts->FindFamilyName(family, &index, &exists)) && exists)
                    return family;
            }
            return nullptr;
        }

        /// Draws the glyph onto the DIB selected into dc, which starts fully
        /// transparent. Direct2D and DirectWrite, the same text stack XAML
        /// draws the in-app icons with, so the tray glyph gets the same
        /// smooth edges. GDI's ANTIALIASED_QUALITY is only a request: at icon
        /// sizes GDI often honours the font's small-size hinting instead and
        /// draws hard, stair-stepped edges.
        bool DrawGlyph(HDC dc, wchar_t glyph, int size, bool light)
        {
            winrt::com_ptr<ID2D1Factory> d2d;
            winrt::com_ptr<IDWriteFactory> dwrite;
            if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, d2d.put())) ||
                FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                           reinterpret_cast<IUnknown**>(dwrite.put()))))
                return false;

            auto family = IconFontFamily(dwrite.get());
            if (!family)
                return false;

            // One pixel per DIP: the icon is already sized in real pixels.
            // Premultiplied, which is exactly what an alpha icon wants.
            // Software: a hardware target would bring Direct3D and the GPU
            // driver into the tray process, and keep them there, for sixteen
            // pixels drawn now and then.
            auto properties = D2D1::RenderTargetProperties(
                D2D1_RENDER_TARGET_TYPE_SOFTWARE,
                D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
                96.0f, 96.0f);

            winrt::com_ptr<ID2D1DCRenderTarget> target;
            RECT rect{ 0, 0, size, size };
            if (FAILED(d2d->CreateDCRenderTarget(&properties, target.put())) ||
                FAILED(target->BindDC(dc, &rect)))
                return false;

            winrt::com_ptr<IDWriteTextFormat> format;
            if (FAILED(dwrite->CreateTextFormat(family, nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
                                                DWRITE_FONT_STRETCH_NORMAL, static_cast<float>(size), L"",
                                                format.put())))
                return false;
            format->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

            winrt::com_ptr<ID2D1SolidColorBrush> brush;
            float tone = light ? 1.0f : 0.0f;
            if (FAILED(target->CreateSolidColorBrush(D2D1::ColorF(tone, tone, tone, 1.0f), brush.put())))
                return false;

            target->BeginDraw();
            target->Clear(D2D1::ColorF(0, 0, 0, 0));

            // Grayscale: ClearType would bake colour fringes for one
            // particular background into the icon.
            target->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
            target->DrawText(&glyph, 1, format.get(),
                             D2D1::RectF(0, 0, static_cast<float>(size), static_cast<float>(size)), brush.get());

            return SUCCEEDED(target->EndDraw());
        }
    }

    HICON CreateGlyphIcon(wchar_t glyph, int size)
    {
        BITMAPINFO bmi{};
        bmi.bmiHeader.biSize = sizeof(bmi.bmiHeader);
        bmi.bmiHeader.biWidth = size;
        bmi.bmiHeader.biHeight = -size; // top-down
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;

        void* bits = nullptr;
        HDC dc = CreateCompatibleDC(nullptr);
        HBITMAP color = CreateDIBSection(dc, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
        HGDIOBJ oldBitmap = SelectObject(dc, color);

        if (!DrawGlyph(dc, glyph, size, TaskbarIsDark()))
            Trace::Log(L"tray: could not draw the glyph");

        SelectObject(dc, oldBitmap);
        DeleteDC(dc);

        HBITMAP mask = CreateBitmap(size, size, 1, 1, nullptr);
        ICONINFO info{ TRUE, 0, 0, mask, color };
        HICON icon = CreateIconIndirect(&info);

        DeleteObject(mask);
        DeleteObject(color);
        return icon;
    }

    TrayIcon::TrayIcon(wchar_t glyph, std::function<void()> clicked, std::function<void()> rightClicked)
        : m_glyph(glyph), m_clicked(std::move(clicked)), m_rightClicked(std::move(rightClicked))
    {
        WNDCLASSEXW wc{ sizeof(wc) };
        wc.lpfnWndProc = WndProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = L"EmberPane.Tray";
        RegisterClassExW(&wc);

        m_hwnd = CreateWindowExW(0, wc.lpszClassName, L"EmberPane", 0, 0, 0, 0, 0,
                                 HWND_MESSAGE, nullptr, wc.hInstance, this);

        // Explorer restarting drops every tray icon; it broadcasts this
        // message so owners can add theirs back.
        m_taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");

        Add();
    }

    TrayIcon::~TrayIcon()
    {
        NOTIFYICONDATAW data{ sizeof(data) };
        data.hWnd = m_hwnd;
        data.uID = IconId;
        Shell_NotifyIconW(NIM_DELETE, &data);

        if (m_icon) DestroyIcon(m_icon);
        if (m_hwnd) DestroyWindow(m_hwnd);
    }

    void TrayIcon::SetGlyph(wchar_t glyph)
    {
        m_glyph = glyph;

        HICON previous = m_icon;
        m_icon = CreateGlyphIcon(m_glyph, GetSystemMetricsForDpi(SM_CXSMICON, GetDpiForSystem()));

        NOTIFYICONDATAW data{ sizeof(data) };
        data.hWnd = m_hwnd;
        data.uID = IconId;
        data.uFlags = NIF_ICON;
        data.hIcon = m_icon;

        if (Shell_NotifyIconW(NIM_MODIFY, &data))
        {
            // The shell keeps its own copy; the old handle would otherwise
            // leak one per change.
            if (previous) DestroyIcon(previous);
        }
        else
        {
            // The update failed, so the tray still holds the old handle.
            DestroyIcon(m_icon);
            m_icon = previous;
        }
    }

    void TrayIcon::Add()
    {
        if (m_icon) DestroyIcon(m_icon);
        m_icon = CreateGlyphIcon(m_glyph, GetSystemMetricsForDpi(SM_CXSMICON, GetDpiForSystem()));

        NOTIFYICONDATAW data{ sizeof(data) };
        data.hWnd = m_hwnd;
        data.uID = IconId;
        data.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP;
        data.uCallbackMessage = CallbackMessage;
        data.hIcon = m_icon;
        wcscpy_s(data.szTip, L"Ember Pane");

        Shell_NotifyIconW(NIM_ADD, &data);

        data.uVersion = NOTIFYICON_VERSION_4;
        Shell_NotifyIconW(NIM_SETVERSION, &data);
    }

    LRESULT CALLBACK TrayIcon::WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
    {
        if (message == WM_NCCREATE)
        {
            auto create = reinterpret_cast<CREATESTRUCTW*>(lParam);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
        }

        auto self = reinterpret_cast<TrayIcon*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (!self)
            return DefWindowProcW(hwnd, message, wParam, lParam);

        if (message == CallbackMessage)
        {
            // NOTIFYICON_VERSION_4: the event is in the low word of lParam.
            switch (LOWORD(lParam))
            {
            case NIN_SELECT:
            case NIN_KEYSELECT:
                Trace::Log(L"tray: click");
                if (self->m_clicked) self->m_clicked();
                break;
            case WM_CONTEXTMENU:
                Trace::Log(L"tray: right-click");
                if (self->m_rightClicked) self->m_rightClicked();
                break;
            }
            return 0;
        }

        if (message == self->m_taskbarCreated)
        {
            self->Add();
            return 0;
        }

        return DefWindowProcW(hwnd, message, wParam, lParam);
    }
}
