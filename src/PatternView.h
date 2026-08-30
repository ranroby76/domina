// ============================================================================
//  C:\workspace\Domina\src\PatternView.h
//  Domina - DAW-style pattern window (Fanan Team)
//
//  Shows the baked pattern as MIDI notes over bars, exactly as the arp will
//  play it: note lengths are the gated lengths, the gaps are the rests.
//  Two locators define the region that actually plays; they snap to a musical
//  division (Bar / Beat / 1/2 Beat) so the loop can never land off-grid.
//  Everything outside the locators is dimmed.
//
//  Reads the processor's lock-free snapshots on the GUI thread only.
// ============================================================================

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginProcessor.h"
#include "DominaLookAndFeel.h"

class PatternView : public juce::Component,
                    private juce::Timer
{
public:
    explicit PatternView (DominaAudioProcessor& p, DominaLookAndFeel& laf)
        : proc (p), lnf (laf)
    {
        pStart = proc.apvts.getParameter ("loopStart");
        pEnd   = proc.apvts.getParameter ("loopEnd");
        setWantsKeyboardFocus (false);
        startTimerHz (30);
    }

    ~PatternView() override { stopTimer(); }

    // ---- painting -----------------------------------------------------------
    void paint (juce::Graphics& g) override
    {
        const auto full = getLocalBounds().toFloat();

        g.setColour (lnf.insetBg);
        g.fillRoundedRectangle (full, 4.0f);

        auto body = getLocalBounds();
        auto head = body.removeFromTop (kHeadH);
        auto gutter = body.removeFromLeft (kGutterW);

        const float totalBeats = juce::jmax (1.0f, pattern.totalBeats);
        const float bpb        = juce::jmax (1.0f, pattern.beatsPerBar);
        const float pxPerBeat  = (float) body.getWidth() / totalBeats;

        // ---- vertical range -------------------------------------------------
        int loNote = 127, hiNote = 0;
        computeRange (loNote, hiNote);
        const int span = juce::jmax (1, hiNote - loNote + 1);
        const float laneH = (float) body.getHeight() / (float) span;

        // ---- horizontal grid -------------------------------------------------
        const float sixteenth = 0.25f;
        if (pxPerBeat * sixteenth > 5.0f)
        {
            g.setColour (lnf.gridLine.withAlpha (0.35f));
            for (float b = 0.0f; b <= totalBeats + 0.001f; b += sixteenth)
            {
                const float x = body.getX() + b * pxPerBeat;
                g.drawVerticalLine ((int) x, (float) body.getY(), (float) body.getBottom());
            }
        }

        g.setColour (lnf.gridLine);
        for (float b = 0.0f; b <= totalBeats + 0.001f; b += 1.0f)
        {
            const float x = body.getX() + b * pxPerBeat;
            g.drawVerticalLine ((int) x, (float) body.getY(), (float) body.getBottom());
        }

        g.setColour (lnf.gridBar);
        for (float b = 0.0f; b <= totalBeats + 0.001f; b += bpb)
        {
            const float x = body.getX() + b * pxPerBeat;
            g.drawVerticalLine ((int) x, (float) body.getY(), (float) body.getBottom());
        }

        // ---- lane stripes + note-name gutter ---------------------------------
        g.setFont (lnf.numericFont (juce::jlimit (7.0f, 11.0f, laneH * 0.72f)));
        for (int n = loNote; n <= hiNote; ++n)
        {
            const float y = body.getBottom() - (float) (n - loNote + 1) * laneH;
            const bool black = juce::MidiMessage::isMidiNoteBlack (n);

            if (black)
            {
                g.setColour (juce::Colours::black.withAlpha (0.22f));
                g.fillRect ((float) body.getX(), y, (float) body.getWidth(), laneH);
            }

            if (laneH >= 9.0f)
            {
                g.setColour (n % 12 == 0 ? lnf.textSecond : lnf.textMuted);
                g.drawText (juce::MidiMessage::getMidiNoteName (n, true, true, 3),
                            gutter.getX() + 2, (int) y, gutter.getWidth() - 5, (int) laneH,
                            juce::Justification::centredRight, false);
            }
        }

        // ---- locators --------------------------------------------------------
        float ls = 0.0f, le = totalBeats;
        getLoop (ls, le, totalBeats);
        const float xs = body.getX() + ls * pxPerBeat;
        const float xe = body.getX() + le * pxPerBeat;

        g.setColour (lnf.windowBg.withAlpha (0.72f));
        if (xs > body.getX())
            g.fillRect ((float) body.getX(), (float) body.getY(), xs - body.getX(), (float) body.getHeight());
        if (xe < body.getRight())
            g.fillRect (xe, (float) body.getY(), body.getRight() - xe, (float) body.getHeight());

        // ---- notes -----------------------------------------------------------
        int chordNotes[16];
        const int chordSize = chordPoolFor (chordNotes);

        if (pattern.count == 0)
        {
            g.setColour (lnf.textMuted);
            g.setFont (lnf.labelFont (13.0f));
            g.drawText ("no pattern", body, juce::Justification::centred, false);
        }

        for (int i = 0; i < pattern.count; ++i)
        {
            const auto& n = pattern.notes[i];
            const int midi = fanan::SeededArpCore::resolveRung (n.rung, pattern.nominalLadder,
                                                                chordNotes, chordSize,
                                                                chord.octaves, chord.semitones);
            if (midi < loNote || midi > hiNote)
                continue;

            const float x = body.getX() + n.startBeat * pxPerBeat;
            const float w = juce::jmax (2.0f, n.lengthBeats * pxPerBeat - 1.0f);
            const float y = body.getBottom() - (float) (midi - loNote + 1) * laneH;
            const auto r = juce::Rectangle<float> (x + 0.5f, y + 1.0f,
                                                   w, juce::jmax (2.0f, laneH - 2.0f));

            const bool inLoop = (n.startBeat >= ls - 1.0e-4f && n.startBeat < le - 1.0e-4f);
            const float bright = 0.45f + 0.55f * juce::jlimit (0.0f, 1.0f, n.accent);

            g.setColour (inLoop ? lnf.accent.withMultipliedBrightness (bright)
                                : lnf.accentDim.withAlpha (0.45f));
            g.fillRoundedRectangle (r, 2.0f);

            if (inLoop && r.getWidth() > 5.0f)
            {
                g.setColour (juce::Colours::black.withAlpha (0.35f));
                g.drawRoundedRectangle (r, 2.0f, 1.0f);
            }
        }

        // ---- playhead --------------------------------------------------------
        if (proc.isArpRunning())
        {
            const float px = body.getX() + juce::jlimit (0.0f, totalBeats, proc.getPlayPosition()) * pxPerBeat;
            g.setColour (juce::Colours::white.withAlpha (0.85f));
            g.drawVerticalLine ((int) px, (float) body.getY(), (float) body.getBottom());
        }

        // ---- header ----------------------------------------------------------
        g.setColour (lnf.panelBg);
        g.fillRect (head);
        g.setColour (lnf.gridLine);
        g.drawHorizontalLine (head.getBottom() - 1, (float) head.getX(), (float) head.getRight());

        g.setFont (lnf.numericFont (10.0f));
        const int bars = juce::jmax (1, pattern.bars);
        for (int bar = 0; bar < bars; ++bar)
        {
            const float x = body.getX() + bar * bpb * pxPerBeat;
            g.setColour (lnf.textMuted);
            g.drawText (juce::String (bar + 1), (int) x + 4, head.getY(), 30, head.getHeight(),
                        juce::Justification::centredLeft, false);
        }

        drawLocatorFlag (g, xs, head, true);
        drawLocatorFlag (g, xe, head, false);

        g.setColour (lnf.gridLine);
        g.drawRoundedRectangle (full.reduced (0.5f), 4.0f, 1.0f);
    }

    // ---- locator dragging ---------------------------------------------------
    void mouseDown (const juce::MouseEvent& e) override
    {
        const float totalBeats = juce::jmax (1.0f, pattern.totalBeats);
        float ls = 0.0f, le = totalBeats;
        getLoop (ls, le, totalBeats);

        const float xs = xForBeat (ls), xe = xForBeat (le);
        const float mx = (float) e.position.x;

        if (std::abs (mx - xs) <= kGrabPx)        dragging = 1;
        else if (std::abs (mx - xe) <= kGrabPx)   dragging = 2;
        else if (e.y < kHeadH)                    dragging = (std::abs (mx - xs) < std::abs (mx - xe)) ? 1 : 2;
        else                                      dragging = 0;

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
        if (e.y >= kHeadH)
            return;

        const float totalBeats = juce::jmax (1.0f, pattern.totalBeats);   // reset to the whole pattern
        setParam (pStart, 0.0f);
        setParam (pEnd, totalBeats);
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        const float totalBeats = juce::jmax (1.0f, pattern.totalBeats);
        float ls = 0.0f, le = totalBeats;
        getLoop (ls, le, totalBeats);

        const bool near = std::abs ((float) e.position.x - xForBeat (ls)) <= kGrabPx
                       || std::abs ((float) e.position.x - xForBeat (le)) <= kGrabPx;

        setMouseCursor (near ? juce::MouseCursor::LeftRightResizeCursor
                             : juce::MouseCursor::NormalCursor);
    }

private:
    static constexpr int   kHeadH   = 20;
    static constexpr int   kGutterW = 34;
    static constexpr float kGrabPx  = 7.0f;

    DominaAudioProcessor&    proc;
    DominaLookAndFeel&       lnf;
    juce::RangedAudioParameter* pStart = nullptr;
    juce::RangedAudioParameter* pEnd   = nullptr;

    static bool differs (float a, float b) noexcept { return std::abs (a - b) > 1.0e-6f; }

    fanan::PatternSnapshot pattern;
    fanan::ChordSnapshot   chord;
    int    dragging   = 0;          // 0 none, 1 start, 2 end
    int    lastCount  = -1;
    float  lastPlay   = -1.0f;
    float  lastLoopS  = -1.0f;
    float  lastLoopE  = -1.0f;
    int    lastChordN = -1;

    void timerCallback() override
    {
        bool dirty = false;

        fanan::PatternSnapshot ps;
        if (proc.readPattern (ps))
        {
            if (ps.count != lastCount || differs (ps.totalBeats, pattern.totalBeats)
                || ps.nominalLadder != pattern.nominalLadder)
                dirty = true;
            else
                for (int i = 0; i < ps.count && ! dirty; ++i)
                    dirty = differs (ps.notes[i].startBeat,   pattern.notes[i].startBeat)
                         || differs (ps.notes[i].lengthBeats, pattern.notes[i].lengthBeats)
                         || (ps.notes[i].rung != pattern.notes[i].rung);

            pattern   = ps;
            lastCount = ps.count;
        }

        fanan::ChordSnapshot cs;
        if (proc.readChord (cs))
        {
            if (cs.size != lastChordN || cs.octaves != chord.octaves
                || std::memcmp (cs.notes, chord.notes, sizeof (cs.notes)) != 0)
                dirty = true;

            chord      = cs;
            lastChordN = cs.size;
        }

        const float ps2 = proc.getPlayPosition();
        if (proc.isArpRunning() && std::abs (ps2 - lastPlay) > 0.001f)
        {
            lastPlay = ps2;
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

    juce::Rectangle<int> bodyArea() const
    {
        return getLocalBounds().withTrimmedTop (kHeadH).withTrimmedLeft (kGutterW);
    }

    float xForBeat (float beat) const
    {
        const auto b = bodyArea();
        const float total = juce::jmax (1.0f, pattern.totalBeats);
        return (float) b.getX() + beat / total * (float) b.getWidth();
    }

    float beatForX (float x) const
    {
        const auto b = bodyArea();
        const float total = juce::jmax (1.0f, pattern.totalBeats);
        if (b.getWidth() <= 0)
            return 0.0f;
        return juce::jlimit (0.0f, total, (x - (float) b.getX()) / (float) b.getWidth() * total);
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
        const float total = juce::jmax (1.0f, pattern.totalBeats);
        const float snap  = juce::jmax (0.25f, proc.getSnapBeats());
        float beat = std::round (beatForX (mouseX) / snap) * snap;
        beat = juce::jlimit (0.0f, total, beat);

        float ls = 0.0f, le = total;
        getLoop (ls, le, total);

        if (dragging == 1)
            setParam (pStart, juce::jlimit (0.0f, juce::jmax (0.0f, le - snap), beat));
        else if (dragging == 2)
            setParam (pEnd, juce::jlimit (juce::jmin (total, ls + snap), total, beat));
    }

    // Held chord, or a C major triad so the roll is readable with no keys down.
    int chordPoolFor (int* out) const
    {
        if (chord.size > 0)
        {
            const int n = juce::jmin (16, chord.size);
            for (int i = 0; i < n; ++i)
                out[i] = chord.notes[i];
            return n;
        }

        out[0] = 60; out[1] = 64; out[2] = 67;
        return 3;
    }

    void computeRange (int& loNote, int& hiNote) const
    {
        int chordNotes[16];
        const int n = chordPoolFor (chordNotes);

        loNote = 127;
        hiNote = 0;

        for (int i = 0; i < pattern.count; ++i)
        {
            const int midi = fanan::SeededArpCore::resolveRung (pattern.notes[i].rung,
                                                                pattern.nominalLadder,
                                                                chordNotes, n,
                                                                chord.octaves, chord.semitones);
            loNote = juce::jmin (loNote, midi);
            hiNote = juce::jmax (hiNote, midi);
        }

        if (loNote > hiNote)
        {
            loNote = 60;
            hiNote = 72;
        }

        loNote = juce::jmax (0,   loNote - 1);
        hiNote = juce::jmin (127, hiNote + 1);

        if (hiNote - loNote < 11)                       // keep at least an octave visible
        {
            const int pad = (11 - (hiNote - loNote) + 1) / 2;
            loNote = juce::jmax (0,   loNote - pad);
            hiNote = juce::jmin (127, hiNote + pad);
        }
    }

    void drawLocatorFlag (juce::Graphics& g, float x, juce::Rectangle<int> head, bool isStart) const
    {
        const float h = (float) head.getHeight();
        const float y = (float) head.getY();
        const float w = 7.0f;

        juce::Path p;
        if (isStart)
        {
            p.startNewSubPath (x, y);
            p.lineTo (x + w, y);
            p.lineTo (x, y + h * 0.62f);
        }
        else
        {
            p.startNewSubPath (x, y);
            p.lineTo (x - w, y);
            p.lineTo (x, y + h * 0.62f);
        }
        p.closeSubPath();

        g.setColour (lnf.locator);
        g.fillPath (p);
        g.drawLine (x, y, x, (float) getHeight(), 1.0f);
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PatternView)
};
