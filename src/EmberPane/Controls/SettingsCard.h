#pragma once

#include "SettingsCard.g.h"

namespace winrt::EmberPane::implementation
{
    struct SettingsCard : SettingsCardT<SettingsCard>
    {
        SettingsCard();

        hstring Header() const { return unbox_value<hstring>(GetValue(HeaderProperty())); }
        void Header(hstring const& value) { SetValue(HeaderProperty(), box_value(value)); }

        hstring Description() const { return unbox_value<hstring>(GetValue(DescriptionProperty())); }
        void Description(hstring const& value) { SetValue(DescriptionProperty(), box_value(value)); }

        hstring Glyph() const { return unbox_value<hstring>(GetValue(GlyphProperty())); }
        void Glyph(hstring const& value) { SetValue(GlyphProperty(), box_value(value)); }

        Windows::Foundation::IInspectable Body() const { return GetValue(BodyProperty()); }
        void Body(Windows::Foundation::IInspectable const& value) { SetValue(BodyProperty(), value); }

        bool IsWide() const { return unbox_value<bool>(GetValue(IsWideProperty())); }
        void IsWide(bool value) { SetValue(IsWideProperty(), box_value(value)); }

        // Registered on first use rather than at module load: WinUI must be
        // initialised before a dependency property can exist.
        static Microsoft::UI::Xaml::DependencyProperty HeaderProperty();
        static Microsoft::UI::Xaml::DependencyProperty DescriptionProperty();
        static Microsoft::UI::Xaml::DependencyProperty GlyphProperty();
        static Microsoft::UI::Xaml::DependencyProperty BodyProperty();
        static Microsoft::UI::Xaml::DependencyProperty IsWideProperty();

        void OnApplyTemplate();
        void OnContentChanged(Windows::Foundation::IInspectable const& oldContent,
                              Windows::Foundation::IInspectable const& newContent);

        static void OnPropertyChanged(Microsoft::UI::Xaml::DependencyObject const& sender,
                                      Microsoft::UI::Xaml::DependencyPropertyChangedEventArgs const& args);

    private:
        void UpdateParts();
    };
}

namespace winrt::EmberPane::factory_implementation
{
    struct SettingsCard : SettingsCardT<SettingsCard, implementation::SettingsCard>
    {
    };
}
