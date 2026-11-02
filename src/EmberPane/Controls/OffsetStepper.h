#pragma once

#include "OffsetStepper.g.h"

namespace winrt::EmberPane::implementation
{
    struct OffsetStepper : OffsetStepperT<OffsetStepper>
    {
        OffsetStepper();

        int32_t Value() const { return unbox_value<int32_t>(GetValue(ValueProperty())); }
        void Value(int32_t value) { SetValue(ValueProperty(), box_value(value)); }

        int32_t Minimum() const { return m_minimum; }
        void Minimum(int32_t value) { m_minimum = value; UpdateButtons(); }

        int32_t Maximum() const { return m_maximum; }
        void Maximum(int32_t value) { m_maximum = value; UpdateButtons(); }

        int32_t Step() const { return m_step; }
        void Step(int32_t value) { m_step = value; }

        hstring ValueText() const { return unbox_value<hstring>(GetValue(ValueTextProperty())); }
        void ValueText(hstring const& value) { SetValue(ValueTextProperty(), box_value(value)); }

        hstring CaptionText() const { return unbox_value<hstring>(GetValue(CaptionTextProperty())); }
        void CaptionText(hstring const& value) { SetValue(CaptionTextProperty(), box_value(value)); }

        event_token ValueChanged(Windows::Foundation::EventHandler<Windows::Foundation::IInspectable> const& handler)
        {
            return m_valueChanged.add(handler);
        }
        void ValueChanged(event_token const& token) noexcept { m_valueChanged.remove(token); }

        static Microsoft::UI::Xaml::DependencyProperty ValueProperty();
        static Microsoft::UI::Xaml::DependencyProperty ValueTextProperty();
        static Microsoft::UI::Xaml::DependencyProperty CaptionTextProperty();

        void OnApplyTemplate();

        static void OnValueChanged(Microsoft::UI::Xaml::DependencyObject const& sender,
                                   Microsoft::UI::Xaml::DependencyPropertyChangedEventArgs const& args);

    private:
        void Nudge(int32_t amount);
        void UpdateButtons();

        int32_t m_minimum{ -180 };
        int32_t m_maximum{ 180 };
        int32_t m_step{ 15 };

        Microsoft::UI::Xaml::Controls::Button m_decrease{ nullptr };
        Microsoft::UI::Xaml::Controls::Button m_increase{ nullptr };
        event_token m_decreaseClick{};
        event_token m_increaseClick{};

        event<Windows::Foundation::EventHandler<Windows::Foundation::IInspectable>> m_valueChanged;
    };
}

namespace winrt::EmberPane::factory_implementation
{
    struct OffsetStepper : OffsetStepperT<OffsetStepper, implementation::OffsetStepper>
    {
    };
}
