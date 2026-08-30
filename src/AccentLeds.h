
// ============================================================================
//  C:\workspace\Domina\src\AccentLeds.h
//  Domina - seeded MIDI arpeggiator (Fanan)
//
//  A row of coloured LEDs that swaps the panel's one accent colour. Not a
//  parameter: it changes nothing about what Domina plays, so it has no place in
//  a host's automation list. It rides along in the APVTS state tree instead,
//  which means it saves with the project for free.
// ============================================================================
#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include "DominaLookAndFeel.h"

class AccentLed : public juce::Button
{
public:
    AccentLed (juce::Colour c, const juce::String& name)
        : juce::Button (name), colour (c)
    {
        setClickingTogglesState (true);
        setRadioGroupId (0x00ACCE17);
    }

    void paintButton (juce::Graphics& g, bool hover, bool) override
    {
        auto r = getLocalBounds().toFloat().reduced (2.0f);
        const bool on = getToggleState();

        // Unselected LEDs are dimmed rather than greyed, so the strip still
        // reads as a row of colours and you can see what you are choosing.
        g.setColour (on ? colour : colour.withMultipliedBrightness (hover ? 0.65f : 0.40f));
        g.fillEllipse (r);

        if (on)
        {
            g.setColour (colour.withAlpha (0.35f));
            g.drawEllipse (r.expanded (2.0f), 1.5f);
            g.setColour (juce::Colours::white.withAlpha (0.55f));
            g.drawEllipse (r.reduced (0.5f), 1.0f);
        }
    }

private:
    juce::Colour colour;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AccentLed)
};

class AccentLedStrip : public juce::Component
{
public:
    explicit AccentLedStrip (DominaLookAndFeel& l) : lnf (l)
    {
        static const char* names[] = { "Blue", "Yellow", "Green", "Red", "Orange", "Purple" };

        for (int i = 0; i < DominaLookAndFeel::kAccentCount; ++i)
        {
            auto* led = leds.add (new AccentLed (DominaLookAndFeel::accentChoice (i), names[i]));
            led->onClick = [this, i] { choose (i, true); };
            addAndMakeVisible (led);
        }
    }

    // Set without firing the callback - used when restoring saved state.
    void setSelected (int index)
    {
        choose (index, false);
    }

    int getSelected() const noexcept { return selected; }

    std::function<void (int)> onChange;

    void resized() override
    {
        auto r = getLocalBounds();
        const int n = leds.size();
        if (n <= 0)
            return;

        const int d = juce::jmin (r.getHeight(), r.getWidth() / n);
        auto row = r.withSizeKeepingCentre (d * n, d);

        for (auto* led : leds)
            led->setBounds (row.removeFromLeft (d));
    }

private:
    void choose (int index, bool notify)
    {
        selected = juce::jlimit (0, DominaLookAndFeel::kAccentCount - 1, index);

        for (int i = 0; i < leds.size(); ++i)
            leds[i]->setToggleState (i == selected, juce::dontSendNotification);

        lnf.setAccent (selected);

        if (notify && onChange)
            onChange (selected);
    }

    DominaLookAndFeel&           lnf;
    juce::OwnedArray<AccentLed>  leds;
    int                          selected = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AccentLedStrip)
};
