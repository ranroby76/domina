
// ============================================================================
//  C:\workspace\Domina\src\VirtualKeyboard.h
//  Domina - blackish on-screen keyboard  (Fanan)
//
//  Not juce::MidiKeyboardComponent: that one draws white keys, and a white
//  keybed under a near-black panel is the single brightest thing on screen.
//
//  REVERSED KEYS. The naturals are black and the sharps are white - the old
//  harpsichord layout. It reads correctly against a near-black panel, and the
//  narrow white sharps give the eye the position markers that the wide keys
//  would otherwise have to carry.
//
//  It drives a juce::MidiKeyboardState that the processor owns, which is the
//  standard way to get GUI-thread key presses into the audio thread safely:
//  the state queues the events and processNextMidiBuffer merges them into the
//  incoming MIDI at the top of processBlock.
//
//  It also SHOWS held notes, including ones arriving from a real keyboard, so
//  it doubles as the chord readout for both pattern engines.
// ============================================================================

#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "DominaLookAndFeel.h"

class VirtualKeyboard : public juce::Component,
                        private juce::MidiKeyboardState::Listener,
                        private juce::Timer
{
public:
    VirtualKeyboard (juce::MidiKeyboardState& s, DominaLookAndFeel& l)
        : state (s), lnf (l)
    {
        state.addListener (this);
        startTimerHz (30);
        setWantsKeyboardFocus (false);
    }

    ~VirtualKeyboard() override
    {
        stopTimer();
        state.removeListener (this);
    }

    void setRange (int lowNote, int highNote)
    {
        lowest  = juce::jlimit (0, 120, lowNote);
        highest = juce::jlimit (lowest + 11, 127, highNote);
        resized();
        repaint();
    }

    int getMidiChannel() const noexcept { return channel; }
    void setMidiChannel (int c) { channel = juce::jlimit (1, 16, c); }

    // LATCH - NOT DRIVEN BY ANYTHING SINCE THE HOLD BUTTON WAS REPURPOSED. That
    // button now locks the SEQUENCE to the playing bar; this latch is left here
    // because it is self-contained and costs nothing, but no control reaches it.
    //
    // A mouse has one pointer, so without this the on-screen keyboard is
    // strictly monophonic and cannot hold a chord for the arp to work on.
    //
    // It still starts OFF. The reasoning for defaulting it on belonged to the
    // guitar engine, which needed a held chord before STRUM or PICK could make
    // any sound at all; that engine is gone. Coming up latched instead means
    // the plugin opens holding notes nobody played, which is worse than the
    // on-screen keyboard being monophonic until you press the button.
    void setHold (bool shouldHold)
    {
        if (hold == shouldHold)
            return;

        hold = shouldHold;

        if (! hold)
            allNotesOff();

        repaint();
    }

    bool isHold() const noexcept { return hold; }

    void allNotesOff()
    {
        for (int n = 0; n < 128; ++n)
            if (state.isNoteOnForChannels (0xffff, n))
                state.noteOff (channel, n, 0.0f);

        held = -1;
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();

        g.setColour (lnf.insetBg);
        g.fillRoundedRectangle (r, 4.0f);

        const auto keys = r.reduced (4.0f, 4.0f);
        const int whites = countWhites();
        if (whites <= 0)
            return;

        const float w = keys.getWidth() / (float) whites;

        // ---- naturals ---------------------------------------------------------
        int wi = 0;
        for (int n = lowest; n <= highest; ++n)
        {
            if (isBlack (n))
                continue;

            juce::Rectangle<float> key (keys.getX() + (float) wi * w, keys.getY(),
                                        w - 1.0f, keys.getHeight());

            const bool on = state.isNoteOnForChannels (0xffff, n);
            g.setColour (on ? lnf.accent : juce::Colour (0xFF33333B));
            g.fillRoundedRectangle (key, 2.0f);
            g.setColour (juce::Colour (0xFF191920));
            g.drawRoundedRectangle (key, 2.0f, 1.0f);

            // C markers, so the eye can find position without white keys to count
            if ((n % 12) == 0)
            {
                g.setColour (on ? lnf.insetBg : lnf.textSecond);
                g.setFont (lnf.labelFont (9.0f));
                g.drawText ("C" + juce::String (n / 12 - 1),
                            key.reduced (1.0f, 3.0f), juce::Justification::centredBottom, false);
            }

            ++wi;
        }

        // ---- sharps -----------------------------------------------------------
        wi = 0;
        for (int n = lowest; n <= highest; ++n)
        {
            if (isBlack (n))
                continue;

            const int sharp = n + 1;
            if (sharp <= highest && isBlack (sharp))
            {
                juce::Rectangle<float> key (keys.getX() + (float) (wi + 1) * w - w * 0.30f,
                                            keys.getY(), w * 0.60f, keys.getHeight() * 0.62f);

                const bool on = state.isNoteOnForChannels (0xffff, sharp);
                g.setColour (on ? lnf.accent : juce::Colour (0xFFDEDEE2));
                g.fillRoundedRectangle (key, 2.0f);
                g.setColour (juce::Colour (0xFF08090B));
                g.drawRoundedRectangle (key, 2.0f, 1.0f);
            }

            ++wi;
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        const int n = noteAt (e.getPosition());
        if (n < 0)
            return;

        if (hold)
        {
            // Toggle: click to add a note to the chord, click again to drop it.
            if (state.isNoteOnForChannels (0xffff, n))
                state.noteOff (channel, n, 0.0f);
            else
                state.noteOn (channel, n, 0.85f);

            repaint();
            return;
        }

        press (n);
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (hold)
            return;    // dragging in latch mode would smear a chord across the neck

        const int n = noteAt (e.getPosition());
        if (n != held)
            press (n);
    }

    void mouseUp (const juce::MouseEvent&) override   { if (! hold) release(); }
    void mouseExit (const juce::MouseEvent&) override { if (! hold) release(); }

private:
    void press (int note)
    {
        release();

        if (note < 0)
            return;

        held = note;
        state.noteOn (channel, note, 0.85f);
    }

    void release()
    {
        if (held < 0)
            return;

        state.noteOff (channel, held, 0.0f);
        held = -1;
    }

    bool isBlack (int n) const
    {
        const int pc = ((n % 12) + 12) % 12;
        return pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10;
    }

    int countWhites() const
    {
        int c = 0;
        for (int n = lowest; n <= highest; ++n)
            if (! isBlack (n))
                ++c;
        return c;
    }

    // Sharps are tested first because they sit on top of the naturals.
    int noteAt (juce::Point<int> p) const
    {
        const auto keys = getLocalBounds().reduced (4, 4).toFloat();
        const int whites = countWhites();
        if (whites <= 0 || ! keys.contains (p.toFloat()))
            return -1;

        const float w = keys.getWidth() / (float) whites;

        int wi = 0;
        for (int n = lowest; n <= highest; ++n)
        {
            if (isBlack (n))
                continue;

            const int sharp = n + 1;
            if (sharp <= highest && isBlack (sharp))
            {
                juce::Rectangle<float> key (keys.getX() + (float) (wi + 1) * w - w * 0.30f,
                                            keys.getY(), w * 0.60f, keys.getHeight() * 0.62f);
                if (key.contains (p.toFloat()))
                    return sharp;
            }

            ++wi;
        }

        wi = 0;
        for (int n = lowest; n <= highest; ++n)
        {
            if (isBlack (n))
                continue;

            juce::Rectangle<float> key (keys.getX() + (float) wi * w, keys.getY(),
                                        w - 1.0f, keys.getHeight());
            if (key.contains (p.toFloat()))
                return n;

            ++wi;
        }

        return -1;
    }

    // Repainting is driven by a timer rather than straight from the listener:
    // the callbacks arrive on the audio thread, and touching a Component from
    // there is a data race even when it looks like it works.
    void handleNoteOn (juce::MidiKeyboardState*, int, int, float) override { dirty = true; }
    void handleNoteOff (juce::MidiKeyboardState*, int, int, float) override { dirty = true; }

    void timerCallback() override
    {
        if (! dirty.exchange (false))
            return;

        repaint();
    }

    juce::MidiKeyboardState& state;
    DominaLookAndFeel&       lnf;

    std::atomic<bool> dirty { true };
    int lowest  = 36;    // C2
    int highest = 84;    // C6
    int held    = -1;
    int channel = 1;
    bool hold   = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VirtualKeyboard)
};



