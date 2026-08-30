
// ============================================================================
//  C:\workspace\Domina\src\SeedDigits.h
//  Domina - ARPOMANIAC-style seed dial (Fanan)
//
//  Six knobs, one per decimal place: X, X0, X00, X000, X0000, X00000.
//  They compose the seed - X=3, X0=5, X00=6, X000=7 gives 007653.
//  Six digits is exactly 1,000,000 combinations, which is the whole point.
//
//  The knobs are a VIEW onto the single "arpSeed" parameter, not parameters in
//  their own right: the host still sees one automatable Seed, presets stay one
//  number, and a seed can still be typed or shared as "7653". Host automation
//  and preset recall are polled back into the knobs.
//
//  THE READOUT IS EDITABLE. Double-click it and type a seed. Reaching 462311
//  by turning six knobs is absurd when the number is the thing being shared -
//  a seed is written down and passed around, so it has to be typeable.
//
//  RANDOM re-rolls all six digits independently, each a fresh 0-9 draw. A digit
//  can land on the value it already had - the draws are not exclusive - so a
//  press can leave part (or, once in a million, all) of the seed unchanged.
//
//  MIDI learn is NOT handled here. The editor owns one registry for every
//  learnable control, so this just exposes the button and randomise().
//
//  Note for the player: the digits are not coarse-to-fine. Every seed hashes
//  independently, so changing any digit - including X - produces a completely
//  different arpeggio, not a variation of the current one.
// ============================================================================

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "DominaLookAndFeel.h"

class SeedDigits : public juce::Component,
                   private juce::Timer
{
public:
    static constexpr int kDigits   = 6;
    static constexpr int kMaxSeed  = 999999;

    SeedDigits (juce::AudioProcessorValueTreeState& state, DominaLookAndFeel& laf)
        : lnf (laf)
    {
        param = state.getParameter ("arpSeed");
        jassert (param != nullptr);

        static const char* names[kDigits] = { "X", "X0", "X00", "X000", "X0000", "X00000" };

        for (int i = 0; i < kDigits; ++i)
        {
            auto& k = knob[i];
            k.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
            k.setRange (0.0, 9.0, 1.0);
            k.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
            k.setPopupDisplayEnabled (true, true, this);
            k.setDoubleClickReturnValue (true, 0.0);
            k.onValueChange = [this] { if (! syncing) pushToParam(); };
            k.onDragStart   = [this] { if (param != nullptr) param->beginChangeGesture(); };
            k.onDragEnd     = [this] { if (param != nullptr) param->endChangeGesture(); };
            addAndMakeVisible (k);

            label[i].setText (names[i], juce::dontSendNotification);
            label[i].setJustificationType (juce::Justification::centred);
            label[i].setInterceptsMouseClicks (false, false);
            addAndMakeVisible (label[i]);
        }

        // ---- editable readout ------------------------------------------------
        seedBox.setJustificationType (juce::Justification::centredRight);
        seedBox.setColour (juce::Label::textColourId, lnf.accent);
        seedBox.setColour (juce::Label::backgroundColourId, juce::Colours::transparentBlack);
        seedBox.setColour (juce::Label::outlineWhenEditingColourId, lnf.accent);
        seedBox.setColour (juce::Label::backgroundWhenEditingColourId, lnf.insetBg);
        seedBox.setColour (juce::Label::textWhenEditingColourId, lnf.textPrimary);
        seedBox.setFont (lnf.numericFont (14.0f));
        seedBox.setEditable (false, true, false);   // double-click to edit
        seedBox.setTooltip ("Double-click to type a seed");
        seedBox.onTextChange = [this] { applyTypedSeed(); };
        addAndMakeVisible (seedBox);

        randomButton.setColour (juce::TextButton::buttonColourId, lnf.accentDim);
        randomButton.setColour (juce::TextButton::textColourOffId, lnf.textPrimary);
        randomButton.setTooltip ("Re-roll all six digits  -  right-click for MIDI learn");
        randomButton.onClick = [this] { randomise(); };
        addAndMakeVisible (randomButton);

        pullFromParam (true);
        startTimerHz (12);
    }

    ~SeedDigits() override
    {
        stopTimer();
        for (auto& k : knob)
            k.setLookAndFeel (nullptr);
        randomButton.setLookAndFeel (nullptr);
        seedBox.setLookAndFeel (nullptr);
    }

    // Re-roll every digit. Each draw is independent and inclusive of the digit's
    // current value, so digits may repeat.
    void randomise()
    {
        if (param == nullptr)
            return;

        auto& rng = juce::Random::getSystemRandom();

        int value = 0, mul = 1;
        {
            const juce::ScopedValueSetter<bool> guard (syncing, true);

            for (int i = 0; i < kDigits; ++i)
            {
                const int digit = rng.nextInt (10);          // 0-9, repeats allowed
                knob[i].setValue ((double) digit, juce::dontSendNotification);
                value += digit * mul;
                mul   *= 10;
            }
        }

        shown = juce::jlimit (0, kMaxSeed, value);
        refreshBox();

        // one gesture, so the host records a single seed change
        param->beginChangeGesture();
        param->setValueNotifyingHost (param->convertTo0to1 ((float) shown));
        param->endChangeGesture();

        repaint();
    }

    // The editor registers this with the MIDI-learn system.
    juce::Component& getRandomButton() noexcept { return randomButton; }

    void paint (juce::Graphics& g) override
    {
        auto r = readoutBounds;

        g.setColour (lnf.insetBg);
        g.fillRoundedRectangle (r.toFloat(), 3.0f);
        g.setColour (lnf.gridLine);
        g.drawRoundedRectangle (r.toFloat().reduced (0.5f), 3.0f, 1.0f);

        g.setColour (lnf.textMuted);
        g.setFont (lnf.labelFont (10.0f));
        g.drawText ("SEED", r.reduced (7, 0), juce::Justification::centredLeft, false);
    }

    void resized() override
    {
        auto area = getLocalBounds();

        auto strip = area.removeFromBottom (kReadoutH);
        randomButton.setBounds (strip.removeFromRight (kButtonW).reduced (1, 0));
        strip.removeFromRight (4);
        readoutBounds = strip.reduced (2, 0);
        seedBox.setBounds (readoutBounds.reduced (7, 0).withTrimmedLeft (36));

        area.removeFromBottom (3);

        const int w = area.getWidth() / kDigits;

        // drawn most-significant first, so the row reads like the number
        for (int slot = 0; slot < kDigits; ++slot)
        {
            const int i = kDigits - 1 - slot;
            auto cell = area.withX (area.getX() + slot * w).withWidth (w).reduced (2, 0);
            label[i].setBounds (cell.removeFromTop (12));
            knob[i].setBounds (cell);
        }
    }

private:
    static constexpr int kReadoutH = 20;
    static constexpr int kButtonW  = 74;

    void timerCallback() override { pullFromParam (false); }

    int composed() const
    {
        int v = 0, mul = 1;
        for (int i = 0; i < kDigits; ++i)
        {
            v += juce::jlimit (0, 9, (int) knob[i].getValue()) * mul;
            mul *= 10;
        }
        return v;
    }

    void refreshBox()
    {
        seedBox.setText (juce::String (shown).paddedLeft ('0', kDigits),
                         juce::dontSendNotification);
    }

    // Typed seeds are forgiving: anything non-numeric is stripped, and a short
    // number is just a small seed rather than an error.
    void applyTypedSeed()
    {
        if (param == nullptr)
            return;

        const juce::String digits = seedBox.getText().retainCharacters ("0123456789");
        const int v = juce::jlimit (0, kMaxSeed, digits.getIntValue());

        shown = v;
        refreshBox();

        {
            const juce::ScopedValueSetter<bool> guard (syncing, true);
            int rest = v;
            for (int i = 0; i < kDigits; ++i)
            {
                knob[i].setValue (rest % 10, juce::dontSendNotification);
                rest /= 10;
            }
        }

        param->beginChangeGesture();
        param->setValueNotifyingHost (param->convertTo0to1 ((float) v));
        param->endChangeGesture();

        repaint();
    }

    void pushToParam()
    {
        if (param == nullptr)
            return;

        const int v = juce::jlimit (0, kMaxSeed, composed());
        shown = v;
        refreshBox();
        param->setValueNotifyingHost (param->convertTo0to1 ((float) v));
        repaint();
    }

    // Host automation, preset recall or a typed seed changed the parameter
    // behind our back: split it back out across the six knobs.
    void pullFromParam (bool force)
    {
        if (param == nullptr)
            return;

        const int v = juce::jlimit (0, kMaxSeed,
                                    (int) std::lround (param->convertFrom0to1 (param->getValue())));

        if (! force && v == shown)
            return;

        // Never yank the text out from under someone mid-edit.
        if (seedBox.isBeingEdited())
            return;

        shown = v;
        refreshBox();

        const juce::ScopedValueSetter<bool> guard (syncing, true);
        int rest = v;
        for (int i = 0; i < kDigits; ++i)
        {
            knob[i].setValue (rest % 10, juce::dontSendNotification);
            rest /= 10;
        }
        repaint();
    }

    DominaLookAndFeel&          lnf;
    juce::RangedAudioParameter* param = nullptr;
    juce::Slider     knob[kDigits];
    juce::Label      label[kDigits];
    juce::Label      seedBox;
    juce::TextButton randomButton { "RANDOM" };
    juce::Rectangle<int> readoutBounds;
    int  shown      = -1;
    bool syncing    = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SeedDigits)
};



