#include "pch.h"
#include "Controls/OffsetStepper.h"
#if __has_include("OffsetStepper.g.cpp")
#include "OffsetStepper.g.cpp"
#endif

using namespace winrt;
using namespace Microsoft::UI::Xaml;

namespace winrt::EmberPane::implementation
{
    OffsetStepper::OffsetStepper()
    {
        DefaultStyleKey(box_value(L"EmberPane.OffsetStepper"));

        // A dependency property exists once something has asked for it, and
        // the template binds to these by name. One never set from code would
        // otherwise not be found, and the failure takes the UI with it.
        ValueProperty();
        ValueTextProperty();
        CaptionTextProperty();
    }

    DependencyProperty OffsetStepper::ValueProperty()
    {
        static auto property = DependencyProperty::Register(
            L"Value", xaml_typename<int32_t>(), xaml_typename<EmberPane::OffsetStepper>(),
            PropertyMetadata{ box_value(0), PropertyChangedCallback{ &OffsetStepper::OnValueChanged } });
        return property;
    }

    DependencyProperty OffsetStepper::ValueTextProperty()
    {
        static auto property = DependencyProperty::Register(
            L"ValueText", xaml_typename<hstring>(), xaml_typename<EmberPane::OffsetStepper>(),
            PropertyMetadata{ box_value(L"--:--") });
        return property;
    }

    DependencyProperty OffsetStepper::CaptionTextProperty()
    {
        static auto property = DependencyProperty::Register(
            L"CaptionText", xaml_typename<hstring>(), xaml_typename<EmberPane::OffsetStepper>(),
            PropertyMetadata{ box_value(hstring{}) });
        return property;
    }

    void OffsetStepper::OnApplyTemplate()
    {
        base_type::OnApplyTemplate();

        if (m_decrease) m_decrease.Click(m_decreaseClick);
        if (m_increase) m_increase.Click(m_increaseClick);

        m_decrease = GetTemplateChild(L"PART_Decrease").try_as<Controls::Button>();
        m_increase = GetTemplateChild(L"PART_Increase").try_as<Controls::Button>();

        if (m_decrease)
            m_decreaseClick = m_decrease.Click([this](auto&&, auto&&) { Nudge(-m_step); });
        if (m_increase)
            m_increaseClick = m_increase.Click([this](auto&&, auto&&) { Nudge(m_step); });

        UpdateButtons();
    }

    void OffsetStepper::OnValueChanged(DependencyObject const& sender, DependencyPropertyChangedEventArgs const&)
    {
        if (auto stepper = sender.try_as<EmberPane::OffsetStepper>())
        {
            auto self = get_self<OffsetStepper>(stepper);
            self->UpdateButtons();
            self->m_valueChanged(stepper, nullptr);
        }
    }

    void OffsetStepper::Nudge(int32_t amount)
    {
        int32_t next = std::clamp(Value() + amount, m_minimum, m_maximum);
        if (next != Value())
            Value(next);
    }

    void OffsetStepper::UpdateButtons()
    {
        // At a limit the button is disabled, which the stock button template
        // already draws as dimmed and inert.
        if (m_decrease) m_decrease.IsEnabled(Value() > m_minimum);
        if (m_increase) m_increase.IsEnabled(Value() < m_maximum);
    }
}
