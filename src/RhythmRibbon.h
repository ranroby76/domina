
// ============================================================================
//  C:\workspace\Domina\src\RhythmRibbon.h
//  Domina - compact rhythm strip (Fanan)
//
//  Replaces the tall piano roll. One horizontal lane showing only what the
//  seed actually decides: the LENGTH of each note and the GAP between notes.
//  Pitch is deliberately not drawn - the chord decides pitch, the seed decides
//  rhythm - so this view never needs redrawing when the chord changes, and it
//  frees the whole area below for controls.
//
//  Block height encodes the metric accent, so downbeats read as taller.
//  The two locators still define the region that actually plays; they snap to
//  Bar / Beat / 1/2 Beat and everything outside them is dimmed.
// ============================================================================

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginProcessor.h"
#include "DominaLookAndFeel.h"

class RhythmRibbon : public juce::Component,
                     private juce::Timer
{
public:
    RhythmRibbon (DominaAudioProcessor& p, DominaLookAndFeel& laf)
        : proc (p), lnf (laf)
    {
        pStart = proc.apvts.getParameter ("loopStart");
        pEnd   = proc.apvts.getParameter ("loopEnd");
        setWantsKeyboardFocus (false);
        startTimerHz (30);
    }

    ~RhythmRibbon() override { stopTimer(); }

    void paint (juce::Graphics& g) override
    {
        const auto full = getLocalBounds().toFloat();
        g.setColour (lnf.insetBg);
        g.fillRoundedRectangle (full, 4.0f);

        auto body  = getLocalBounds();
        auto ruler = body.removeFromTop (kRulerH);
        auto lane  = body.reduced (0, kLanePad);

        const float totalBeats = viewBeats();
        const float bpb        = juce::jmax (1.0f, pattern.beatsPerBar);
        const float pxPerBeat  = (float) lane.getWidth() / totalBeats;

        // ---- grid ------------------------------------------------------------
        if (pxPerBeat * 0.25f > 4.0f)
        {
            g.setColour (lnf.gridLine.withAlpha (0.30f));
            for (float b = 0.25f; b < totalBeats; b += 0.25f)
                g.drawVerticalLine ((int) (lane.getX() + b * pxPerBeat),
                                    (float) lane.getY(), (float) lane.getBottom());
        }

        g.setColour (lnf.gridLine);
        for (float b = 1.0f; b < totalBeats; b += 1.0f)
            g.drawVerticalLine ((int) (lane.getX() + b * pxPerBeat),
                                (float) lane.getY(), (float) lane.getBottom());

        g.setColour (lnf.gridBar);
        for (float b = 0.0f; b <= totalBeats + 0.001f; b += bpb)
            g.drawVerticalLine ((int) (lane.getX() + b * pxPerBeat),
                                (float) lane.getY(), (float) lane.getBottom());

        // ---- locators --------------------------------------------------------
        float ls = 0.0f, le = totalBeats;
        getLoop (ls, le, totalBeats);
        const float xs = lane.getX() + ls * pxPerBeat;
        const float xe = lane.getX() + le * pxPerBeat;

        // ---- the notes -------------------------------------------------------
        const float laneY = (float) lane.getY();
        const float laneH = (float) lane.getHeight();

        if (pattern.count == 0)
        {
            g.setColour (lnf.textMuted);
            g.setFont (lnf.labelFont (12.0f));
            g.drawText ("no pattern", lane, juce::Justification::centred, false);
        }

        for (int i = 0; i < pattern.count; ++i)
        {
            const auto& n = pattern.notes[i];

            const float x = lane.getX() + n.startBeat * pxPerBeat;
            const float w = juce::jmax (2.0f, n.lengthBeats * pxPerBeat - 1.0f);

            const float accent = juce::jlimit (0.0f, 1.0f, n.accent);
            const float h      = laneH * (0.40f + 0.60f * accent);
            const float y      = laneY + (laneH - h) * 0.5f;

            const bool inLoop = (n.startBeat >= ls - 1.0e-4f && n.startBeat < le - 1.0e-4f);

            g.setColour (inLoop ? lnf.accent.withMultipliedBrightness (0.55f + 0.45f * accent)
                                : lnf.accentDim.withAlpha (0.40f));
            g.fillRoundedRectangle (x + 0.5f, y, w, h, 2.0f);
        }

        // ---- dim outside the locators ---------------------------------------
        g.setColour (lnf.windowBg.withAlpha (0.70f));
        if (xs > lane.getX())
            g.fillRect ((float) lane.getX(), (float) lane.getY(),
                        xs - lane.getX(), (float) lane.getHeight());
        if (xe < lane.getRight())
            g.fillRect (xe, (float) lane.getY(),
                        lane.getRight() - xe, (float) lane.getHeight());

        // ---- playhead --------------------------------------------------------
        if (proc.isArpRunning())
        {
            const float px = lane.getX()
                           + juce::jlimit (0.0f, totalBeats, proc.getPlayPosition()) * pxPerBeat;
            g.setColour (juce::Colours::white.withAlpha (0.85f));
            g.drawVerticalLine ((int) px, (float) lane.getY(), (float) lane.getBottom());
        }

        // ---- ruler -----------------------------------------------------------
        g.setColour (lnf.panelBg);
        g.fillRect (ruler);
        g.setColour (lnf.gridLine);
        g.drawHorizontalLine (ruler.getBottom() - 1, (float) ruler.getX(), (float) ruler.getRight());

        g.setFont (lnf.numericFont (10.0f));
        g.setColour (lnf.textMuted);
        for (int bar = 0; bar < juce::jmax (1, pattern.bars); ++bar)
            g.drawText (juce::String (bar + 1),
                        (int) (lane.getX() + bar * bpb * pxPerBeat) + 4, ruler.getY(),
                        30, ruler.getHeight(), juce::Justification::centredLeft, false);

        drawLocatorFlag (g, xs, ruler, true);
        drawLocatorFlag (g, xe, ruler, false);

        g.setColour (lnf.gridLine);
        g.drawRoundedRectangle (full.reduced (0.5f), 4.0f, 1.0f);
    }

    // ---- locator dragging ---------------------------------------------------
    void mouseDown (const juce::MouseEvent& e) override
    {
        const float total = viewBeats();
        float ls = 0.0f, le = total;
        getLoop (ls, le, total);

        const float xs = xForBeat (ls), xe = xForBeat (le);
        const float mx = (float) e.position.x;

        if (std::abs (mx - xs) <= kGrabPx)      dragging = 1;
        else if (std::abs (mx - xe) <= kGrabPx) dragging = 2;
        else if (e.y < kRulerH)                 dragging = (std::abs (mx - xs) < std::abs (mx - xe)) ? 1 : 2;
        else                                    dragging = 0;

        if (dragging == 1) pStart->beginChangeGesture();
        if (dragging == 2) pEnd->beginChangeGesture();
        if (dragging != 0) applyDrag (mx);
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (dragging != 0)
            applyDrag ((float) e.position.x);
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        if (dragging == 1) pStart->endChangeGesture();
        if (dragging == 2) pEnd->endChangeGesture();
        dragging = 0;
    }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        if (e.y >= kRulerH)
            return;

        setParam (pStart, 0.0f);
        setParam (pEnd, juce::jmax (1.0f, pattern.totalBeats));   // reopen to the whole pattern
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        const float total = viewBeats();
        float ls = 0.0f, le = total;
        getLoop (ls, le, total);

        const bool nearFlag = std::abs ((float) e.position.x - xForBeat (ls)) <= kGrabPx
                           || std::abs ((float) e.position.x - xForBeat (le)) <= kGrabPx;

        setMouseCursor (nearFlag ? juce::MouseCursor::LeftRightResizeCursor
                                 : juce::MouseCursor::NormalCursor);
    }

private:
    static constexpr int   kRulerH  = 18;
    static constexpr int   kLanePad = 6;
    static constexpr float kGrabPx  = 7.0f;

    static bool differs (float a, float b) noexcept { return std::abs (a - b) > 1.0e-6f; }

    void timerCallback() override
    {
        bool dirty = false;

        // Rhythm only, so the chord is irrelevant here: nothing to poll but the
        // pattern itself, the locators and the playhead.
        fanan::PatternSnapshot ps;
        if (proc.readPattern (ps))
        {
            // BARS decides the pattern length and the locators can only narrow
            // it, so growing BARS used to widen the ribbon while leaving the
            // right locator where it was - the pattern got longer and you kept
            // hearing the old length, with nothing on screen saying why.
            //
            // A loop that was fully open stays fully open, which is what a DAW
            // does when you lengthen a clip. A loop the user deliberately
            // closed is left exactly where they put it.
            // Never while a drag is in progress: the locator now SETS the
            // length, so following the length back would move the flag under the
            // user's own finger and snap it to the bar they were dragging past.
            if (dragging == 0
                && differs (ps.totalBeats, pattern.totalBeats)
                && pattern.totalBeats > 0.0f)
            {
                const float le = pEnd->convertFrom0to1 (pEnd->getValue());

                if (le >= pattern.totalBeats - 0.001f && ps.totalBeats > le + 0.001f)
                    setParam (pEnd, juce::jmax (1.0f, ps.totalBeats));
            }

            if (ps.count != pattern.count || differs (ps.totalBeats, pattern.totalBeats))
                dirty = true;
            else
                for (int i = 0; i < ps.count && ! dirty; ++i)
                    dirty = differs (ps.notes[i].startBeat,   pattern.notes[i].startBeat)
                         || differs (ps.notes[i].lengthBeats, pattern.notes[i].lengthBeats)
                         || differs (ps.notes[i].accent,      pattern.notes[i].accent);

            pattern = ps;
        }

        const float pos = proc.getPlayPosition();
        if (proc.isArpRunning() && differs (pos, lastPlay))
        {
            lastPlay = pos;
            dirty = true;
        }

        const float ls = pStart->convertFrom0to1 (pStart->getValue());
        const float le = pEnd->convertFrom0to1 (pEnd->getValue());
        if (differs (ls, lastLoopS) || differs (le, lastLoopE))
        {
            lastLoopS = ls;
            lastLoopE = le;
            dirty = true;
        }

        if (dirty)
            repaint();
    }

    juce::Rectangle<int> laneArea() const
    {
        return getLocalBounds().withTrimmedTop (kRulerH).reduced (0, kLanePad);
    }

    // The ribbon shows the pattern PLUS one empty bar. The loop-end locator now
    // sets the pattern's length, so there has to be somewhere to drag it TO -
    // clamping the drag to the pattern would mean it could never grow. Pull the
    // locator into the spare bar and the pattern extends to meet it, and a fresh
    // spare bar appears beyond.
    float viewBeats() const
    {
        const float bpb = juce::jmax (1.0f, pattern.beatsPerBar);
        return juce::jmin ((float) fanan::kMaxPatternBars * bpb,
                           juce::jmax (1.0f, pattern.totalBeats) + bpb);
    }

    float xForBeat (float beat) const
    {
        const auto l = laneArea();
        return (float) l.getX() + beat / viewBeats() * (float) l.getWidth();
    }

    float beatForX (float x) const
    {
        const auto l = laneArea();
        if (l.getWidth() <= 0)
            return 0.0f;
        const float view = viewBeats();
        return juce::jlimit (0.0f, view,
                             (x - (float) l.getX()) / (float) l.getWidth() * view);
    }

    void getLoop (float& s, float& e, float total) const
    {
        s = juce::jlimit (0.0f, total, pStart->convertFrom0to1 (pStart->getValue()));
        e = juce::jlimit (0.0f, total, pEnd->convertFrom0to1 (pEnd->getValue()));
        if (e - s < 0.25f)
            e = juce::jmin (total, s + 0.25f);
    }

    void setParam (juce::RangedAudioParameter* p, float value)
    {
        p->setValueNotifyingHost (p->convertTo0to1 (value));
    }

    void applyDrag (float mouseX)
    {
        const float total = viewBeats();
        const float snap  = juce::jmax (0.25f, proc.getSnapBeats());
        float beat = juce::jlimit (0.0f, total, std::round (beatForX (mouseX) / snap) * snap);

        float ls = 0.0f, le = total;
        getLoop (ls, le, total);

        if (dragging == 1)
            setParam (pStart, juce::jlimit (0.0f, juce::jmax (0.0f, le - snap), beat));
        else if (dragging == 2)
            setParam (pEnd, juce::jlimit (juce::jmin (total, ls + snap), total, beat));
    }

    void drawLocatorFlag (juce::Graphics& g, float x, juce::Rectangle<int> ruler, bool isStart) const
    {
        const float h = (float) ruler.getHeight();
        const float y = (float) ruler.getY();
        const float w = 7.0f;

        juce::Path p;
        p.startNewSubPath (x, y);
        p.lineTo (isStart ? x + w : x - w, y);
        p.lineTo (x, y + h * 0.65f);
        p.closeSubPath();

        g.setColour (lnf.locator);
        g.fillPath (p);
        g.drawLine (x, y, x, (float) getHeight(), 1.0f);
    }

    DominaAudioProcessor&       proc;
    DominaLookAndFeel&          lnf;
    juce::RangedAudioParameter* pStart = nullptr;
    juce::RangedAudioParameter* pEnd   = nullptr;

    fanan::PatternSnapshot pattern;
    int   dragging  = 0;          // 0 none, 1 start, 2 end
    float lastPlay  = -1.0f;
    float lastLoopS = -1.0f;
    float lastLoopE = -1.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RhythmRibbon)
};



