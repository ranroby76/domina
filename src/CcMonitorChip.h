
// ============================================================================
//  C:\workspace\Domina\src\CcMonitorChip.h
//  Domina - last-moved CC chip  (Fanan)
//
//  Shows whatever controller moved most recently, and is the thing you drag.
//  The whole assignment gesture is: move a knob on the controller, look at the
//  chip, drag it onto the control you want. Nothing is armed, nothing is
//  waiting, and you can see what you are about to bind before you bind it.
//
//  It is deliberately live rather than a list: a controller usually has one
//  knob under your hand at a time, so "the last one you touched" is almost
//  always the one you meant.
// ============================================================================

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "DominaLookAndFeel.h"
#include "MidiLearn.h"

class CcMonitorChip : public juce::Component,
                      public  juce::SettableTooltipClient,
                      private juce::Timer
{
public:
    CcMonitorChip (fanan::MidiLearn& l, DominaLookAndFeel& laf)
        : learn (l), lnf (laf)
    {
        setTooltip ("Move a control on your MIDI controller, then drag this onto any knob");
        startTimerHz (20);
    }

    ~CcMonitorChip() override { stopTimer(); }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        const bool have = cc >= 0;

        g.setColour (lnf.keyGrey);
        g.fillRoundedRectangle (r, 4.0f);
        g.setColour (have ? lnf.accent : lnf.gridLine);
        g.drawRoundedRectangle (r.reduced (0.5f), 4.0f, 1.0f);

        auto text = r.reduced (7, 0);

        g.setColour (lnf.textMuted);
        g.setFont (lnf.labelFont (9.0f));
        g.drawText ("MIDI", text.removeFromTop (r.getHeight() * 0.45f),
                    juce::Justification::centredLeft, false);

        g.setColour (have ? lnf.accent : lnf.textMuted);
        g.setFont (lnf.numericFont (12.0f));
        g.drawText (have ? ("CC " + juce::String (cc)) : juce::String ("- -"),
                    text, juce::Justification::centredLeft, false);

        if (! have)
            return;

        // A small grip, so it reads as draggable rather than as a readout.
        g.setColour (lnf.textMuted);
        for (int i = 0; i < 3; ++i)
            g.fillRect (r.getRight() - 10.0f, r.getY() + 6.0f + (float) i * 4.0f, 5.0f, 1.5f);
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (cc < 0 || dragging)
            return;

        auto* container = juce::DragAndDropContainer::findParentDragContainerFor (this);
        if (container == nullptr)
            return;

        dragging = true;
        container->startDragging ("MIDICC:" + juce::String (cc), this);
        juce::ignoreUnused (e);
    }

    void mouseUp (const juce::MouseEvent&) override { dragging = false; }

    int getCc() const noexcept { return cc; }

private:
    void timerCallback() override
    {
        const uint32_t stamp = learn.lastStampValue();
        if (stamp == seenStamp)
            return;

        seenStamp = stamp;

        const int c = learn.lastCc();
        if (c == cc)
            return;

        cc = c;
        repaint();
    }

    fanan::MidiLearn&  learn;
    DominaLookAndFeel& lnf;

    uint32_t seenStamp = 0;
    int      cc = -1;
    bool     dragging = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CcMonitorChip)
};



