#include "pch.h"
#include "Controls/SettingsCard.h"
#if __has_include("SettingsCard.g.cpp")
#include "SettingsCard.g.cpp"
#endif

using namespace winrt;
using namespace Microsoft::UI::Xaml;

namespace winrt::EmberPane::implementation
{
    namespace
    {
        DependencyProperty Register(wchar_t const* name, Windows::UI::Xaml::Interop::TypeName type, Windows::Foundation::IInspectable fallback)
        {
            return DependencyProperty::Register(
                name, type, xaml_typename<EmberPane::SettingsCard>(),
                PropertyMetadata{ fallback, PropertyChangedCallback{ &SettingsCard::OnPropertyChanged } });
        }
    }

    SettingsCard::SettingsCard()
    {
        DefaultStyleKey(box_value(L"EmberPane.SettingsCard"));

        // A dependency property exists once something has asked for it, and
        // the template binds to these by name, whether or not a card sets them.
        HeaderProperty();
        DescriptionProperty();
        GlyphProperty();
        BodyProperty();
        IsWideProperty();
    }

    DependencyProperty SettingsCard::HeaderProperty()
    {
        static auto property = Register(L"Header", xaml_typename<hstring>(), box_value(hstring{}));
        return property;
    }

    DependencyProperty SettingsCard::DescriptionProperty()
    {
        static auto property = Register(L"Description", xaml_typename<hstring>(), box_value(hstring{}));
        return property;
    }

    DependencyProperty SettingsCard::GlyphProperty()
    {
        static auto property = Register(L"Glyph", xaml_typename<hstring>(), box_value(hstring{}));
        return property;
    }

    DependencyProperty SettingsCard::BodyProperty()
    {
        static auto property = Register(L"Body", xaml_typename<Windows::Foundation::IInspectable>(), nullptr);
        return property;
    }

    DependencyProperty SettingsCard::IsWideProperty()
    {
        static auto property = Register(L"IsWide", xaml_typename<bool>(), box_value(false));
        return property;
    }

    void SettingsCard::OnApplyTemplate()
    {
        base_type::OnApplyTemplate();
        UpdateParts();
    }

    void SettingsCard::OnContentChanged(Windows::Foundation::IInspectable const& oldContent,
                                        Windows::Foundation::IInspectable const& newContent)
    {
        base_type::OnContentChanged(oldContent, newContent);
        UpdateParts();
    }

    void SettingsCard::OnPropertyChanged(DependencyObject const& sender, DependencyPropertyChangedEventArgs const&)
    {
        if (auto card = sender.try_as<EmberPane::SettingsCard>())
            get_self<SettingsCard>(card)->UpdateParts();
    }

    void SettingsCard::UpdateParts()
    {
        // Empty parts collapse rather than leaving their margins behind, so a
        // card with no description is exactly one line tall.
        auto show = [](bool visible) { return visible ? Visibility::Visible : Visibility::Collapsed; };

        if (auto icon = GetTemplateChild(L"PART_Icon").try_as<UIElement>())
            icon.Visibility(show(!Glyph().empty()));

        if (auto description = GetTemplateChild(L"PART_Description").try_as<UIElement>())
            description.Visibility(show(!Description().empty()));

        if (auto content = GetTemplateChild(L"PART_Content").try_as<UIElement>())
            content.Visibility(show(Content() != nullptr));

        bool hasBody = Body() != nullptr;

        if (auto body = GetTemplateChild(L"PART_Body").try_as<UIElement>())
            body.Visibility(show(hasBody && !IsWide()));

        if (auto wide = GetTemplateChild(L"PART_WideBody").try_as<UIElement>())
            wide.Visibility(show(hasBody && IsWide()));
    }
}
