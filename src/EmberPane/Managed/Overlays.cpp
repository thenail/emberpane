#include "pch.h"
#include "Managed/Overlays.h"

#include <array>
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

        /// Each strip is its own update of the frame layer.
        constexpr size_t MaxDirtyStrips = 24;

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

    void DropPreview::Show(Core::Rect const& rect, HWND above)
    {
        if (m_shown == rect)
        {
            if (GetWindow(above, GW_HWNDNEXT) != m_hwnd)
                SetWindowPos(m_hwnd, above, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            return;
        }

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
        SetWindowPos(m_hwnd, above, rect.Left, rect.Top, rect.Width(), rect.Height(),
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

    void DropGuide::Show(Scene const& scene, HWND dragged)
    {
        // Beneath the window in the user's hand, which nothing of ours may
        // cover, and kept there whatever comes forward meanwhile.
        if (m_shown == scene)
        {
            if (GetWindow(dragged, GW_HWNDNEXT) != m_hwnd)
                SetWindowPos(m_hwnd, dragged, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            return;
        }

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

            m_hwnd = CreateWindowExW(WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
                                     GuideClass, L"", WS_POPUP, 0, 0, 0, 0, nullptr, nullptr,
                                     GetModuleHandleW(nullptr), nullptr);
        }

        Paint(scene);

        if (moved)
            SetWindowPos(m_hwnd, dragged, scene.Area.Left, scene.Area.Top, scene.Area.Width(), scene.Area.Height(),
                         SWP_NOACTIVATE | SWP_SHOWWINDOW);
        else if (GetWindow(dragged, GW_HWNDNEXT) != m_hwnd)
            SetWindowPos(m_hwnd, dragged, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);

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

    namespace
    {
        /// The pixels a band can touch, in the layer's coordinates, as four
        /// strips that never overlap: the top and bottom take the corners.
        std::array<RECT, 4> SidesOf(FrameLayer::Band const& band, Core::Rect const& origin)
        {
            // Out past the stroke and its smoothing, and in past the corners.
            int reach = std::max(band.Outer, 0) + 1;
            int inward = band.Radius + std::max(-band.Inner, 0) + 1;

            int left = band.Frame.Left - origin.Left;
            int top = band.Frame.Top - origin.Top;
            int right = band.Frame.Right - origin.Left;
            int bottom = band.Frame.Bottom - origin.Top;

            int topEnd = top + inward;
            int bottomStart = std::max(bottom - inward, topEnd);
            int leftEnd = left + inward;
            int rightStart = std::max(right - inward, leftEnd);

            return { RECT{ left - reach, top - reach, right + reach, topEnd },
                     RECT{ left - reach, bottomStart, right + reach, bottom + reach },
                     RECT{ left - reach, topEnd, leftEnd, bottomStart },
                     RECT{ rightStart, topEnd, right + reach, bottomStart } };
        }

        /// From a point to the edge of a rounded rectangle; negative inside.
        float RoundedDistance(float x, float y, float left, float top, float right, float bottom, float radius)
        {
            float halfWidth = (right - left) / 2;
            float halfHeight = (bottom - top) / 2;
            radius = std::max(0.0f, std::min({ radius, halfWidth, halfHeight }));

            float qx = std::abs(x - (left + right) / 2) - (halfWidth - radius);
            float qy = std::abs(y - (top + bottom) / 2) - (halfHeight - radius);
            float outside = std::hypot(std::max(qx, 0.0f), std::max(qy, 0.0f));
            return outside + std::min(std::max(qx, qy), 0.0f) - radius;
        }

        /// A band, over what is there: the pixels from Inner to Outer out from
        /// the rounded edge of the window, smoothed over half a pixel each side
        /// as a stroke would be.
        void DrawBand(BYTE* bits, size_t stride, FrameLayer::Band const& band, Core::Rect const& origin,
                      RECT const& area)
        {
            float left = static_cast<float>(band.Frame.Left - origin.Left);
            float top = static_cast<float>(band.Frame.Top - origin.Top);
            float right = static_cast<float>(band.Frame.Right - origin.Left);
            float bottom = static_cast<float>(band.Frame.Bottom - origin.Top);
            float radius = static_cast<float>(band.Radius);

            int blue = GetBValue(band.Color);
            int green = GetGValue(band.Color);
            int red = GetRValue(band.Color);

            std::vector<RECT> holes;
            for (auto const& hole : band.Holes)
            {
                RECT local{ hole.Left - origin.Left, hole.Top - origin.Top, hole.Right - origin.Left,
                            hole.Bottom - origin.Top };
                RECT inside{};
                if (IntersectRect(&inside, &local, &area))
                    holes.push_back(inside);
            }

            for (int y = area.top; y < area.bottom; ++y)
            {
                BYTE* pixel = bits + y * stride + static_cast<size_t>(area.left) * 4;
                for (int x = area.left; x < area.right; ++x, pixel += 4)
                {
                    POINT at{ x, y };
                    if (std::any_of(holes.begin(), holes.end(), [&](RECT const& hole) { return PtInRect(&hole, at); }))
                        continue;

                    float d = RoundedDistance(x + 0.5f, y + 0.5f, left, top, right, bottom, radius);
                    float coverage = std::clamp(std::min(d - band.Inner, band.Outer - d) + 0.5f, 0.0f, 1.0f);
                    if (coverage <= 0) continue;

                    // Premultiplied, as the layer is.
                    int alpha = static_cast<int>(std::lround(coverage * 255));
                    int keep = 255 - alpha;
                    pixel[0] = static_cast<BYTE>((blue * alpha + pixel[0] * keep + 127) / 255);
                    pixel[1] = static_cast<BYTE>((green * alpha + pixel[1] * keep + 127) / 255);
                    pixel[2] = static_cast<BYTE>((red * alpha + pixel[2] * keep + 127) / 255);
                    pixel[3] = static_cast<BYTE>(alpha + (pixel[3] * keep + 127) / 255);
                }
            }
        }
    }


    FrameLayer::~FrameLayer()
    {
        Trim();
        if (m_hwnd) DestroyWindow(m_hwnd);
    }

    void FrameLayer::Show(Scene const& scene, HWND front, Stack stack)
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
        }

        // Also after Hide, with the scene unchanged.
        if (!IsWindowVisible(m_hwnd))
            ShowWindow(m_hwnd, SW_SHOWNOACTIVATE);

        if (stack == Stack::InFront)
            InFrontOf(front);
        else
            Beneath(front);
    }

    void FrameLayer::Hide()
    {
        if (m_hwnd && IsWindowVisible(m_hwnd))
            ShowWindow(m_hwnd, SW_HIDE);
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

    void FrameLayer::Beneath(HWND window)
    {
        if (GetWindow(window, GW_HWNDNEXT) == m_hwnd) return;

        SetWindowPos(m_hwnd, window, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
    }

    void FrameLayer::Warm()
    {
        if (m_shown && !m_painted)
            Paint(*m_shown);
    }

    void FrameLayer::Trim()
    {
        if (m_dc)
        {
            SelectObject(m_dc, m_old);
            DeleteObject(m_bitmap);
            DeleteDC(m_dc);
        }

        m_dc = nullptr;
        m_bitmap = nullptr;
        m_old = nullptr;
        m_bits = nullptr;
        m_width = 0;
        m_height = 0;
        m_painted = false;
    }

    void FrameLayer::Paint(Scene const& scene)
    {
        int width = std::max(1, scene.Area.Width());
        int height = std::max(1, scene.Area.Height());

        HDC screen = GetDC(nullptr);

        if (!m_bits || width != m_width || height != m_height)
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
            m_bits = static_cast<BYTE*>(bits);

            m_width = width;
            m_height = height;
        }

        if (m_bits)
        {
            // Only what changed, while a window is being resized: the whole
            // monitor's worth, drawn and handed over on every step, keeps the
            // frames well behind the windows.
            auto dirty = m_shown && m_shown->Area == scene.Area && m_painted
                             ? Changed(*m_shown, scene)
                             : std::optional<std::vector<RECT>>{};

            if (dirty)
                for (auto const& strip : *dirty)
                    DrawPart(scene, strip);
            else
                DrawPart(scene, { 0, 0, width, height });

            POINT at{ scene.Area.Left, scene.Area.Top };
            SIZE extent{ width, height };
            POINT from{};
            BLENDFUNCTION blend{ AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };

            UPDATELAYEREDWINDOWINFO info{ sizeof(info) };
            info.hdcDst = screen;
            info.pptDst = &at;
            info.psize = &extent;
            info.hdcSrc = m_dc;
            info.pptSrc = &from;
            info.pblend = &blend;
            info.dwFlags = ULW_ALPHA;

            if (dirty)
            {
                for (auto const& strip : *dirty)
                {
                    info.prcDirty = &strip;
                    UpdateLayeredWindowIndirect(m_hwnd, &info);
                }
            }
            else
            {
                UpdateLayeredWindowIndirect(m_hwnd, &info);
            }

            m_painted = true;
        }

        ReleaseDC(nullptr, screen);
    }

    void FrameLayer::DrawPart(Scene const& scene, RECT const& part)
    {
        // Straight into the surface's memory, not with Direct2D: while other
        // windows are being resized, its software renderer waits on the
        // graphics system for far longer than the drawing takes.
        size_t stride = static_cast<size_t>(m_width) * 4;

        GdiFlush();
        for (int y = part.top; y < part.bottom; ++y)
            std::memset(m_bits + y * stride + static_cast<size_t>(part.left) * 4, 0,
                        static_cast<size_t>(part.right - part.left) * 4);

        for (auto const& band : scene.Bands)
        {
            for (auto const& side : SidesOf(band, scene.Area))
            {
                RECT area{};
                if (IntersectRect(&area, &side, &part))
                    DrawBand(m_bits, stride, band, scene.Area, area);
            }
        }
    }

    std::optional<std::vector<RECT>> FrameLayer::Changed(Scene const& was, Scene const& now)
    {
        // The bands that came or went. Each is drawn only along its four
        // sides, and the sides are thin, so only those strips are redrawn.
        std::vector<Band const*> changed;
        for (auto const& band : was.Bands)
            if (std::find(now.Bands.begin(), now.Bands.end(), band) == now.Bands.end())
                changed.push_back(&band);
        for (auto const& band : now.Bands)
            if (std::find(was.Bands.begin(), was.Bands.end(), band) == was.Bands.end())
                changed.push_back(&band);

        RECT layer{ 0, 0, now.Area.Width(), now.Area.Height() };
        std::vector<RECT> strips;

        for (auto band : changed)
        {
            for (auto const& side : SidesOf(*band, now.Area))
            {
                RECT clipped{};
                if (IntersectRect(&clipped, &side, &layer))
                    strips.push_back(clipped);
            }
        }

        // The same side before and after, a step apart, is one strip.
        auto area = [](RECT const& r) { return static_cast<long long>(r.right - r.left) * (r.bottom - r.top); };
        for (bool merged = true; merged;)
        {
            merged = false;
            for (size_t i = 0; i < strips.size() && !merged; ++i)
            {
                for (size_t j = i + 1; j < strips.size() && !merged; ++j)
                {
                    RECT both{};
                    UnionRect(&both, &strips[i], &strips[j]);
                    if (area(both) > area(strips[i]) + area(strips[j])) continue;

                    strips[i] = both;
                    strips.erase(strips.begin() + j);
                    merged = true;
                }
            }
        }

        // Past this, all at once is as quick, and one update rather than many.
        long long total = 0;
        for (auto const& strip : strips)
            total += area(strip);
        if (strips.size() > MaxDirtyStrips || total * 4 > area(layer))
            return std::nullopt;

        return strips;
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
