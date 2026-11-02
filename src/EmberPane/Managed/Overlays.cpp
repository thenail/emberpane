#include "pch.h"
#include "Managed/Overlays.h"

#include <d2d1.h>

namespace EmberPane::Managed
{
    namespace
    {
        constexpr wchar_t PreviewClass[] = L"EmberPane.DropPreview";
        constexpr wchar_t HandleClass[] = L"EmberPane.GapHandle";
        constexpr wchar_t GuideClass[] = L"EmberPane.DropGuide";
        constexpr wchar_t FrameClass[] = L"EmberPane.FrameLayer";

        /// Without the frosted glass: enough to see, and what is beneath stays
        /// readable.
        constexpr BYTE PlainAlpha = 96;

        /// Over the blur, a grey like the one Snap's preview settles on,
        /// as 0xAABBGGRR.
        constexpr DWORD FrostTint = 0x70808080;

        constexpr int LineDip = 3;

        constexpr ULONGLONG DragIntervalMs = 33;

        /// Frosted glass, by the same route the shell's own surfaces use. It
        /// is undocumented, so it is looked up rather than linked, and if it
        /// is ever gone the preview is merely plain. The documented system
        /// backdrops will not do: acrylic turns solid on an inactive window,
        /// and this window must never be active.
        bool Frost(HWND hwnd)
        {
            struct AccentPolicy
            {
                int State;
                int Flags;
                DWORD Tint;
                int Animation;
            };

            struct CompositionData
            {
                int Attribute;
                void* Data;
                SIZE_T Size;
            };

            constexpr int AccentAcrylicBlurBehind = 4;
            constexpr int AttributeAccentPolicy = 19;

            using Setter = BOOL(WINAPI*)(HWND, CompositionData*);
            auto set = reinterpret_cast<Setter>(
                GetProcAddress(GetModuleHandleW(L"user32.dll"), "SetWindowCompositionAttribute"));
            if (!set) return false;

            AccentPolicy accent{ AccentAcrylicBlurBehind, 0, FrostTint, 0 };
            CompositionData data{ AttributeAccentPolicy, &accent, sizeof(accent) };
            return set(hwnd, &data) != FALSE;
        }

        void PlaceTopmost(HWND hwnd, Core::Rect const& rect, UINT flags)
        {
            SetWindowPos(hwnd, HWND_TOPMOST, rect.Left, rect.Top, rect.Width(), rect.Height(),
                         SWP_NOACTIVATE | flags);
        }
    }

    COLORREF AccentColor()
    {
        DWORD argb = 0;
        BOOL opaque = FALSE;
        if (FAILED(DwmGetColorizationColor(&argb, &opaque)))
            return RGB(0x00, 0x67, 0xC0);

        return RGB((argb >> 16) & 0xFF, (argb >> 8) & 0xFF, argb & 0xFF);
    }

    // ------------------------------------------------------------ the preview

    DropPreview::~DropPreview()
    {
        Hide();
    }

    void DropPreview::Show(Core::Rect const& rect, HWND dragged)
    {
        if (m_shown == rect) return;

        if (!m_hwnd)
        {
            static ATOM const registered = []
            {
                WNDCLASSEXW wc{ sizeof(wc) };
                wc.lpfnWndProc = DefWindowProcW;
                wc.hInstance = GetModuleHandleW(nullptr);
                wc.lpszClassName = PreviewClass;
                wc.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
                return RegisterClassExW(&wc);
            }();
            (void)registered;

            m_hwnd = CreateWindowExW(WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
                                     PreviewClass, L"", WS_POPUP, 0, 0, 0, 0, nullptr, nullptr,
                                     GetModuleHandleW(nullptr), nullptr);

            SetLayeredWindowAttributes(m_hwnd, 0, Frost(m_hwnd) ? 255 : PlainAlpha, LWA_ALPHA);

            DWM_WINDOW_CORNER_PREFERENCE corner = DWMWCP_ROUND;
            DwmSetWindowAttribute(m_hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &corner, sizeof(corner));
        }

        // Below the window in the user's hand, as Snap's preview is.
        SetWindowPos(m_hwnd, dragged, rect.Left, rect.Top, rect.Width(), rect.Height(),
                     SWP_NOACTIVATE | SWP_SHOWWINDOW);
        m_shown = rect;
    }

    void DropPreview::Hide()
    {
        if (m_hwnd) DestroyWindow(m_hwnd);
        m_hwnd = nullptr;
        m_shown.reset();
    }

    // ------------------------------------------------------------ the guide

    namespace
    {
        D2D1_COLOR_F ToColor(COLORREF color, float alpha = 1)
        {
            return { GetRValue(color) / 255.0f, GetGValue(color) / 255.0f, GetBValue(color) / 255.0f, alpha };
        }

        D2D1_RECT_F ToRect(Core::Rect const& r, Core::Rect const& origin)
        {
            return { static_cast<float>(r.Left - origin.Left), static_cast<float>(r.Top - origin.Top),
                     static_cast<float>(r.Right - origin.Left), static_cast<float>(r.Bottom - origin.Top) };
        }

    }

    DropGuide::~DropGuide()
    {
        Hide();
    }

    void DropGuide::Show(Scene const& scene)
    {
        if (m_shown == scene) return;

        bool moved = !m_shown || m_shown->Area != scene.Area;

        if (!m_hwnd)
        {
            static ATOM const registered = []
            {
                WNDCLASSEXW wc{ sizeof(wc) };
                wc.lpfnWndProc = DefWindowProcW;
                wc.hInstance = GetModuleHandleW(nullptr);
                wc.lpszClassName = GuideClass;
                return RegisterClassExW(&wc);
            }();
            (void)registered;

            m_hwnd = CreateWindowExW(WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE |
                                         WS_EX_TOPMOST,
                                     GuideClass, L"", WS_POPUP, 0, 0, 0, 0, nullptr, nullptr,
                                     GetModuleHandleW(nullptr), nullptr);
        }

        Paint(scene);

        if (moved)
            PlaceTopmost(m_hwnd, scene.Area, SWP_SHOWWINDOW);

        m_shown = scene;
    }

    void DropGuide::Hide()
    {
        if (m_hwnd) DestroyWindow(m_hwnd);
        m_hwnd = nullptr;
        m_shown.reset();
    }

    void DropGuide::Paint(Scene const& scene)
    {
        int width = std::max(1, scene.Area.Width());
        int height = std::max(1, scene.Area.Height());
        float scale = scene.Dpi / 96.0f;

        BITMAPINFO bmi{};
        bmi.bmiHeader.biSize = sizeof(bmi.bmiHeader);
        bmi.bmiHeader.biWidth = width;
        bmi.bmiHeader.biHeight = -height; // top-down
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;

        HDC screen = GetDC(nullptr);
        HDC dc = CreateCompatibleDC(screen);
        void* bits = nullptr;
        HBITMAP bitmap = CreateDIBSection(dc, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
        HGDIOBJ old = SelectObject(dc, bitmap);

        winrt::com_ptr<ID2D1Factory> d2d;
        winrt::com_ptr<ID2D1DCRenderTarget> target;

        // Software, as for the tray glyph: a hardware target would bring
        // Direct3D into the tray process for the length of a drag and beyond.
        auto properties = D2D1::RenderTargetProperties(
            D2D1_RENDER_TARGET_TYPE_SOFTWARE,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96.0f, 96.0f);

        RECT bounds{ 0, 0, width, height };
        if (SUCCEEDED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, d2d.put())) &&
            SUCCEEDED(d2d->CreateDCRenderTarget(&properties, target.put())) &&
            SUCCEEDED(target->BindDC(dc, &bounds)))
        {
            COLORREF accent = AccentColor();

            winrt::com_ptr<ID2D1SolidColorBrush> brush;
            target->CreateSolidColorBrush(ToColor(accent), brush.put());

            target->BeginDraw();
            target->Clear(D2D1::ColorF(0, 0, 0, 0));

            float corner = 8 * scale;
            float line = 2 * scale;
            for (auto const& place : scene.Places)
            {
                auto rect = ToRect(place, scene.Area);
                D2D1_ROUNDED_RECT outline{ { rect.left + line / 2, rect.top + line / 2, rect.right - line / 2,
                                             rect.bottom - line / 2 }, corner, corner };

                brush->SetColor(ToColor(accent, 0.10f));
                target->FillRoundedRectangle(outline, brush.get());
                brush->SetColor(ToColor(accent, 0.85f));
                target->DrawRoundedRectangle(outline, brush.get(), line);
            }

            target->EndDraw();

            POINT at{ scene.Area.Left, scene.Area.Top };
            SIZE extent{ width, height };
            POINT from{};
            BLENDFUNCTION blend{ AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
            UpdateLayeredWindow(m_hwnd, screen, &at, &extent, dc, &from, 0, &blend, ULW_ALPHA);
        }

        SelectObject(dc, old);
        DeleteObject(bitmap);
        DeleteDC(dc);
        ReleaseDC(nullptr, screen);
    }

    // ------------------------------------------------------------ the frames

    FrameLayer::~FrameLayer()
    {
        Trim();
        if (m_hwnd) DestroyWindow(m_hwnd);
    }

    void FrameLayer::Show(Scene const& scene, HWND front)
    {
        if (m_shown != scene)
        {
            if (!m_hwnd)
            {
                static ATOM const registered = []
                {
                    WNDCLASSEXW wc{ sizeof(wc) };
                    wc.lpfnWndProc = DefWindowProcW;
                    wc.hInstance = GetModuleHandleW(nullptr);
                    wc.lpszClassName = FrameClass;
                    return RegisterClassExW(&wc);
                }();
                (void)registered;

                m_hwnd = CreateWindowExW(WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
                                         FrameClass, L"", WS_POPUP, 0, 0, 0, 0, nullptr, nullptr,
                                         GetModuleHandleW(nullptr), nullptr);
            }

            Paint(scene);
            m_shown = scene;

            if (!IsWindowVisible(m_hwnd))
                ShowWindow(m_hwnd, SW_SHOWNOACTIVATE);
        }

        InFrontOf(front);
    }

    void FrameLayer::InFrontOf(HWND front)
    {
        HWND before = GetWindow(front, GW_HWNDPREV);
        if (before == m_hwnd) return;

        // Put after a topmost window, it would become topmost itself.
        if (!before || (GetWindowLongPtrW(before, GWL_EXSTYLE) & WS_EX_TOPMOST))
            before = HWND_TOP;

        SetWindowPos(m_hwnd, before, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
    }

    void FrameLayer::Trim()
    {
        m_brush = nullptr;
        m_target = nullptr;
        m_factory = nullptr;

        if (m_dc)
        {
            SelectObject(m_dc, m_old);
            DeleteObject(m_bitmap);
            DeleteDC(m_dc);
        }

        m_dc = nullptr;
        m_bitmap = nullptr;
        m_old = nullptr;
        m_width = 0;
        m_height = 0;
    }

    void FrameLayer::Paint(Scene const& scene)
    {
        int width = std::max(1, scene.Area.Width());
        int height = std::max(1, scene.Area.Height());

        HDC screen = GetDC(nullptr);

        if (!m_target || width != m_width || height != m_height)
        {
            Trim();

            BITMAPINFO bmi{};
            bmi.bmiHeader.biSize = sizeof(bmi.bmiHeader);
            bmi.bmiHeader.biWidth = width;
            bmi.bmiHeader.biHeight = -height; // top-down
            bmi.bmiHeader.biPlanes = 1;
            bmi.bmiHeader.biBitCount = 32;
            bmi.bmiHeader.biCompression = BI_RGB;

            m_dc = CreateCompatibleDC(screen);
            void* bits = nullptr;
            m_bitmap = CreateDIBSection(m_dc, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
            m_old = SelectObject(m_dc, m_bitmap);

            // Software, as for the guide and the tray glyph.
            auto properties = D2D1::RenderTargetProperties(
                D2D1_RENDER_TARGET_TYPE_SOFTWARE,
                D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96.0f, 96.0f);

            RECT bounds{ 0, 0, width, height };
            if (SUCCEEDED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, m_factory.put())) &&
                SUCCEEDED(m_factory->CreateDCRenderTarget(&properties, m_target.put())) &&
                SUCCEEDED(m_target->BindDC(m_dc, &bounds)))
            {
                m_target->CreateSolidColorBrush(D2D1::ColorF(0), m_brush.put());
            }

            m_width = width;
            m_height = height;
        }

        if (m_brush)
        {
            m_target->BeginDraw();
            m_target->Clear(D2D1::ColorF(0, 0, 0, 0));

            for (auto const& band : scene.Bands)
            {
                // A stroke is centred on its line. On whole pixels at both
                // sides, it is as crisp as Windows' own border.
                auto rect = ToRect(band.Frame, scene.Area);
                float middle = (band.Inner + band.Outer) / 2.0f;
                float radius = std::max(0.0f, band.Radius + middle);

                m_brush->SetColor(ToColor(band.Color));
                m_target->DrawRoundedRectangle({ { rect.left - middle, rect.top - middle, rect.right + middle,
                                                   rect.bottom + middle }, radius, radius },
                                               m_brush.get(), static_cast<float>(band.Outer - band.Inner));
            }

            m_target->EndDraw();

            POINT at{ scene.Area.Left, scene.Area.Top };
            SIZE extent{ width, height };
            POINT from{};
            BLENDFUNCTION blend{ AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
            UpdateLayeredWindow(m_hwnd, screen, &at, &extent, m_dc, &from, 0, &blend, ULW_ALPHA);
        }

        ReleaseDC(nullptr, screen);
    }

    // ------------------------------------------------------------ the gap handle

    GapHandle::GapHandle(Handlers handlers)
        : m_handlers(std::move(handlers))
    {
        WNDCLASSEXW wc{ sizeof(wc) };
        wc.lpfnWndProc = WndProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = HandleClass;
        RegisterClassExW(&wc);
    }

    GapHandle::~GapHandle()
    {
        Hide();
    }

    void GapHandle::Show(Core::Rect const& gap, Core::Axis direction, UINT dpi)
    {
        m_direction = direction;
        m_dpi = dpi;

        if (!m_hwnd)
        {
            m_hwnd = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TOPMOST, HandleClass,
                                     L"", WS_POPUP, gap.Left, gap.Top, gap.Width(), gap.Height(), nullptr, nullptr,
                                     GetModuleHandleW(nullptr), this);
        }

        Paint(gap);
        PlaceTopmost(m_hwnd, gap, SWP_SHOWWINDOW);
    }

    void GapHandle::Hide()
    {
        if (!m_hwnd) return;

        auto hwnd = std::exchange(m_hwnd, nullptr);
        m_dragging = false;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        DestroyWindow(hwnd);
    }

    void GapHandle::Paint(Core::Rect const& gap)
    {
        int width = std::max(1, gap.Width());
        int height = std::max(1, gap.Height());

        BITMAPINFO bmi{};
        bmi.bmiHeader.biSize = sizeof(bmi.bmiHeader);
        bmi.bmiHeader.biWidth = width;
        bmi.bmiHeader.biHeight = -height; // top-down
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;

        HDC screen = GetDC(nullptr);
        HDC dc = CreateCompatibleDC(screen);
        void* bits = nullptr;
        HBITMAP bitmap = CreateDIBSection(dc, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
        HGDIOBJ old = SelectObject(dc, bitmap);

        // Alpha 1: the desktop shows through, but the mouse is caught, as it
        // would not be at alpha 0. Measured on Windows 11 build 26300.
        auto pixels = static_cast<std::uint32_t*>(bits);
        std::fill_n(pixels, static_cast<size_t>(width) * height, 0x01000000u);

        COLORREF accent = AccentColor();
        std::uint32_t line = 0xFF000000u | (GetRValue(accent) << 16) | (GetGValue(accent) << 8) | GetBValue(accent);

        int thickness = std::max(1, MulDiv(LineDip, m_dpi, 96));
        if (m_direction == Core::Axis::SideBySide)
        {
            int start = std::max(0, (width - thickness) / 2);
            for (int y = 0; y < height; ++y)
                for (int x = start; x < std::min(width, start + thickness); ++x)
                    pixels[static_cast<size_t>(y) * width + x] = line;
        }
        else
        {
            int start = std::max(0, (height - thickness) / 2);
            for (int y = start; y < std::min(height, start + thickness); ++y)
                std::fill_n(pixels + static_cast<size_t>(y) * width, width, line);
        }

        POINT at{ gap.Left, gap.Top };
        SIZE size{ width, height };
        POINT from{};
        BLENDFUNCTION blend{ AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
        UpdateLayeredWindow(m_hwnd, screen, &at, &size, dc, &from, 0, &blend, ULW_ALPHA);

        SelectObject(dc, old);
        DeleteObject(bitmap);
        DeleteDC(dc);
        ReleaseDC(nullptr, screen);
    }

    LRESULT CALLBACK GapHandle::WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
    {
        if (message == WM_NCCREATE)
        {
            auto create = reinterpret_cast<CREATESTRUCTW*>(lParam);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
        }

        auto self = reinterpret_cast<GapHandle*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (!self || !self->m_hwnd)
            return DefWindowProcW(hwnd, message, wParam, lParam);

        return self->Handle(message, wParam, lParam);
    }

    LRESULT GapHandle::Handle(UINT message, WPARAM wParam, LPARAM lParam)
    {
        switch (message)
        {
        case WM_SETCURSOR:
            SetCursor(LoadCursorW(nullptr, m_direction == Core::Axis::SideBySide ? IDC_SIZEWE : IDC_SIZENS));
            return TRUE;

        case WM_MOUSEACTIVATE:
            return MA_NOACTIVATE;

        case WM_LBUTTONDOWN:
            m_dragging = true;
            SetCapture(m_hwnd);
            if (m_handlers.Started) m_handlers.Started();
            return 0;

        case WM_MOUSEMOVE:
            if (m_dragging && GetTickCount64() - m_lastDrag >= DragIntervalMs)
            {
                m_lastDrag = GetTickCount64();
                POINT cursor{};
                GetCursorPos(&cursor);
                if (m_handlers.Dragged) m_handlers.Dragged({ cursor.x, cursor.y });
            }
            return 0;

        case WM_LBUTTONUP:
            ReleaseCapture();
            return 0;

        case WM_CAPTURECHANGED:
            if (m_dragging)
            {
                m_dragging = false;
                if (m_handlers.Finished) m_handlers.Finished();
            }
            return 0;

        }

        return DefWindowProcW(m_hwnd, message, wParam, lParam);
    }
}
