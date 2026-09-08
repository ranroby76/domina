
// ============================================================================
//  C:\workspace\Domina\src\TutorialWindow.h
//  Domina - seeded MIDI arpeggiator (Fanan)
//
//  The built-in guide. Text is DATA, not code: one raw string per chapter, and
//  a tiny markup that only needs three rules -
//
//      '#' at line start   a heading
//      '-' at line start   a bullet
//      blank line          a gap between blocks
//      anything else       joins the block above it
//
//  That last rule is what lets the source be hard-wrapped for readability
//  without the wrapping leaking into the rendered output. Blocks are laid out
//  with juce::TextLayout at the viewport's width and rebuilt only when the
//  chapter or the width changes, never per frame.
// ============================================================================
#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include "DominaLookAndFeel.h"

// ---------------------------------------------------------------------------
// The guide itself
// ---------------------------------------------------------------------------
namespace DominaGuide
{
    struct Chapter { const char* title; const char* body; };

    inline const Chapter* chapters (int& countOut)
    {
        static const Chapter c[] =
        {
        // ---------------------------------------------------------------- 1
        { "WELCOME", R"(# What Domina is

Domina is an arpeggiator that writes MIDI. It makes no sound of
its own. It plays whatever instrument you put after it, and it
plays it like a musician who never gets tired and never repeats
themselves by accident.

# It composes. It does not cycle.

Most arpeggiators walk up and down the notes you are holding in
an order you pick from a little menu. Up. Down. Up-down. You
have heard all three.

Domina writes a piece of music instead. Give it a number, the
SEED, and it composes a whole pattern: note lengths, rests,
ties, accents, a melodic line that leaps and then answers the
leap the way a player would. Then it performs that pattern
using the chord under your fingers.

- Change the chord and the melody follows. The rhythm does not move
- Change the seed and you have a different piece of music
- The same seed gives the same pattern on any machine, forever

# Six hundred and seventy-two million of them

A million seeds. Twenty idioms plus free running. Eight pattern
lengths. Four octave ranges. That is 672,000,000 distinct
patterns before you have touched a single knob, and every one
of them is then reshaped by COMPLEXITY, ORIGINALITY, three
note-length weights, rests and two gate limits.

You will not reach the end of it. Nobody will.

# The idioms are what to try first

Twenty of them, and not one is a preset. An idiom is a
SKELETON, taken from how a style actually plays, and it is
defined mostly by what it REFUSES to play. Folk vaults clean
over beat 3. Reggae holds back the offbeats of 1 and 3 and
sounds the ones on 2 and 4. Gospel pushes across the bar line
and swallows the downbeat behind it.

Domina is pulled toward that shape without ever being trapped
in it. The bar is never the same twice and it is unmistakably
that style every time. Park ORIGINALITY halfway and the idiom
fuses with the seed's own invention into something that belongs
to neither of them. That is where the good accidents live.

# Built to be played, not programmed

This is the part that matters on a stage. You can re-roll the
seed IN THE MIDDLE OF A BAR and the performance does not
stumble. The chord under your hands keeps sounding. The clock
keeps counting. The new pattern picks up exactly where the old
one left off, in time, on the beat.

Put RANDOM on a footswitch and hunt for a phrase live, in front
of people, with the band still playing.

HOLD locks the sequence onto the bar that is passing, so you
can sit on one figure under a verse and let go into the chorus.
MUTE ARP drops you out to your own bare playing for a bar and
back in again without lifting your hands.

# And you can carve it up

The two amber locators on the ribbon decide exactly which part
of the pattern plays. Close them to a couple of beats for a
stutter. Open them across sixteen bars for a whole section.
BARS sets the length, the locators cut it, LOC SNAP decides
where they land, and the ribbon draws every note as it goes by
so you can see what you are cutting.

# The seed is the recording

Nothing is stored. Nothing is rendered. Six digits rebuild the
whole thing, exactly, on any computer, years from now. Find a
phrase you love and write the number on a napkin.

# And when a napkin is not enough

SAVE and LOAD, beside the seed knobs, write the whole state to a
.dompatch file - every knob, both mute states, the idiom, the
locators and your MIDI learn assignments. They live in
Documents/Fanan/Domina/Patches and they are plain XML, so a
patch can be posted, mailed or read in a text editor.

The name of the loaded patch shows above those buttons and is
saved into your project, so reopening a song tells you what it
is playing rather than just playing it.)" },

        // ---------------------------------------------------------------- 2
        { "QUICK START", R"(# Four steps

- Put Domina on a MIDI track and route its output to an instrument
- Hold a chord
- Press RANDOM until something catches your ear
- Write down the six digits

# Then shape it

The three knobs worth reaching for first, in this order:

- ORIGINALITY decides whose pattern you are hearing
- COMPLEXITY decides how much Domina elaborates it
- The RHYTHM weights decide how busy it is

# If you hear nothing

Check MUTE OUT and MUTE ARP are both off, and that the
instrument after Domina is actually receiving its MIDI. Domina
outputs silence on its audio bus by design - it is a MIDI
device wearing an instrument's clothes so that every host will
load it.)" },

        // ---------------------------------------------------------------- 3
        { "SEED", R"(# Six digits

The seed is a number from 0 to 999999, set by six knobs - one
per digit. Turn any digit and you get a different pattern.

- RANDOM re-rolls all six at once
- Click the readout to type a seed in directly
- Neighbouring seeds are not similar; 41 and 42 are unrelated

# Three streams, not one

Behind the seed are three independent random streams: one for
the musical form, one for the rhythm, one for the pitch. They
are separate on purpose.

Because of that, turning a RHYTHM knob rewrites the rhythm and
leaves the melodic contour where it was. You can hunt for the
right density without losing the tune you just found.)" },

        // ---------------------------------------------------------------- 4
        { "ARPEGGIO", R"(# BARS

How long the pattern is before it repeats, from 1 to 8. It sets
the floor - the loop-end locator can open it further, up to 16.

# OCTAVES

How far the melody is allowed to roam through the chord, from
one octave to four. Some idioms need at least two to show their
character: a montuno's octave drop has nowhere to fall in one.

# COMPLEXITY

How much Domina elaborates whatever it was handed.

- At 0 you hear the raw material: no ties, no motif repetition, no shaping
- Turned up, phrases repeat as motifs, notes tie together, accents follow the metre, and leaps are answered by steps

This is the knob that decides whether a pattern sounds composed
or sounds generated.

# ORIGINALITY

Whose pattern it is.

- At 0 the idiom is not consulted at all - the knobs and the seed decide everything
- At 100 the pattern is reduced to the chosen idiom's own skeleton, whatever the other knobs say
- In between is where the two fuse

ORIGINALITY and COMPLEXITY are separate axes. High ORIGINALITY
with COMPLEXITY low gives you the idiom bare; high with
COMPLEXITY up is the interesting territory.)" },

        // ---------------------------------------------------------------- 5
        { "IDIOMS", R"(# A characteristic start point

An idiom is the way a style speaks - held as data Domina is
pulled toward, not as a phrase it plays back. Pick one from the
IDIOM selector and the pattern takes on its shape while staying
a different pattern every bar.

# What an idiom actually says

Almost always, an idiom is defined by what it does NOT play.

- Folk skips beat 3 and pushes over it
- Reggae holds back the offbeats of 1 and 3 while sounding those of 2 and 4
- Disco vaults beat 2 with a three-three-two grouping
- Gospel leaves beat 4 empty and pushes across the bar line

Those holes are the character. The slots in between are left
free, and the seed fills them - which is why the idiom survives
without the bar ever repeating exactly.

# FREE RUNNING

The first entry in the list is not an idiom. It means no idiom:
the seeded engine on its own, exactly as it behaves with
ORIGINALITY at zero.

# Meter

Idioms are written in a meter and only offered in a project
that matches. The 3/4 idioms grey out in a 4/4 session, and a
4/4 idiom in a 3/4 session stands down and free-runs rather
than smearing itself across the bar.)" },

        // ---------------------------------------------------------------- 6
        { "RHYTHM", R"(# The three weights

1/16, 1/8 and 1/4 are not switches - they are how much of each
note length you want in the mix. Domina reads them as a ratio,
so setting all three to 100 is the same as setting all three to
50.

- Push 1/4 up for long, sparse, held playing
- Push 1/16 up for a rolling, busy texture
- The balance between them shapes the feel more than any single one

# RESTS

How much of the bar is left silent. Rests are placed on weak
positions first, so a gap sounds intentional rather than
accidental.

# GATE MIN and GATE MAX

How long each note is held as a fraction of the space it has.
Domina picks a value between the two for every note.

- Both low: staccato, every note clipped short
- Both high: legato, notes running into each other
- Wide apart: an uneven, human, breathing feel

Note lengths come from the gap to the NEXT note, not from a
fixed value - so gate interacts with the weights. Sparse
patterns with a high gate give genuinely long notes.)" },

        // ---------------------------------------------------------------- 7
        { "LOCATORS", R"(# What the ribbon shows

The strip above the columns is the baked pattern: every note as
a bar, its width the note's length. Pitch is deliberately not
drawn - pitch depends on the chord you are holding, and the
whole point is that the rhythm stays put when the chord moves.

The bright line sweeping across it is the play position.

# The locators

The two amber flags set the part of the pattern that actually
plays.

- Drag either flag to close the loop in
- Double-click the ruler to reopen it to the whole pattern
- LOC SNAP sets what the flags snap to, from a whole bar down to a 1/8 beat

# Opening past BARS

The loop-end locator can be dragged further than BARS, into the
empty bar the ribbon always shows to the right. The pattern
grows to meet it, up to 16 bars, and a fresh empty bar appears
beyond. BARS is the floor; the locator is the length.)" },

        // ---------------------------------------------------------------- 8
        { "MIDI OUT", R"(# CHANNEL

Which MIDI channel Domina transmits on, 1 to 16.

# VELOCITY

Scales every velocity Domina writes, from 10% to 200%. The
shape of the accents is preserved - this moves the whole
pattern louder or softer, it does not flatten it.

# RANDOM VELOCITY

Switch it on and two fields appear: how far BELOW and how far
ABOVE the VELOCITY knob a note is allowed to land. With the
knob at 76 and the fields at 6 and 8, every note is drawn
somewhere between 70 and 84.

- The two are separate on purpose. A player's stray notes land quieter than the ones they meant, not louder, so the default leans down: -6 and +8
- A fresh value is drawn for every note, so a repeated pitch never comes back at the same weight
- The accent shape underneath is untouched - this scatters around it, it does not replace it

Small numbers do more than you expect. Four or five either way
is usually enough to stop a line sounding typed in; past about
fifteen it starts sounding unsteady rather than human.

# SYNC TO DAW

More than a tempo switch. With it on:

- Tempo comes from the host, and FREE BPM is ignored
- The pattern's position comes from the song timeline, so bar 3 of the song is bar 3 of the pattern
- CONTINUOUS is suspended, because the timeline already decides where the pattern is

With it off, Domina runs on its own clock at FREE BPM, starting
from zero the first time you play.

# FREE BPM

The internal tempo, used only when SYNC TO DAW is off.

# MUTE OUT and MUTE ARP

MUTE OUT stops everything. MUTE ARP stops the arpeggiator only
- what you play passes straight through, unarpeggiated. Useful
for auditioning a sound, or for dropping out of the pattern for
a bar without taking your hands off the keys.)" },

        // ---------------------------------------------------------------- 9
        { "HOLD", R"(# HOLD

Press it and the sequence locks to the bar that is playing, as
though the locators had closed around that one bar. Release and
it carries on from where it stood.

Nothing jumps at either edge - the pattern simply stops passing
the end of that bar while you hold it. It is a performance
control: lock a bar under a vocal line, let go into the chorus.

# CONTINUOUS

What happens after a silence.

- On: the pattern keeps its place forever. Stop playing, wait, play again, and you rejoin wherever the clock has got to
- Off: two seconds of silence and the next chord starts the pattern from its beginning

Off is the one to use when you want each entry to be a fresh
statement. On is the one to use when Domina is a bed that keeps
running underneath you.

CONTINUOUS has no effect while SYNC TO DAW is on and the
transport is rolling, because the song position is deciding
where the pattern is.

# SORT NOTES

On, the chord is read low to high whatever order you pressed
the keys in. Off, the pattern uses your press order - so
rolling a chord upward and stabbing it as a block give
different melodies from the same seed. It changes pitch only;
the rhythm is untouched either way.)" },

        // --------------------------------------------------------------- 10
        { "LEARN", R"(# Assigning a control

Domina watches the CC traffic arriving on its input. The chip
in the header shows the last controller it saw.

- Drag that chip onto any knob or button to assign it
- Or right-click a control to arm it, then move a controller
- Right-click an assigned control to clear it

# What can be learned

Every knob and every toggle, plus the RANDOM seed button - so
a footswitch can re-roll the pattern mid-performance.

Assignments are saved with the plugin state, so they travel
with the project.)" },

        // --------------------------------------------------------------- 11
        { "KEYBOARD", R"(# The on-screen keyboard

The keys along the bottom show what Domina is receiving,
whether it came from your controller or from clicking them.

The colours are reversed from a real keyboard on purpose - the
naturals are dark and the sharps are light - because the panel
is dark and a wall of white keys would take over the window.

Clicking is momentary: the mouse has one pointer, so the
on-screen keys cannot hold a chord. Use a controller, or your
host's own keyboard, for anything with more than one note.

PANIC sends all notes off, for when a host leaves something
stuck.)" },

        // --------------------------------------------------------------- 12
        { "TIPS", R"(# Finding a pattern

Hunt with RANDOM and a chord held down, not with the knobs.
Seeds are cheap and the knobs will still be there when you find
one worth shaping.

# Making it sound played

COMPLEXITY around 70-80 and GATE MIN well below GATE MAX. The
gap between the gates is what stops every note being the same
length, and that is most of what makes a line sound performed.

Then add RANDOM VELOCITY at about -5/+5. Uneven lengths and
uneven weights are the two halves of the same thing, and
together they do more than either does alone.

# Fusing two styles

Pick an idiom, set ORIGINALITY around 40-60, and push
COMPLEXITY up. You get the idiom's skeleton with Domina's own
elaboration over it - which is neither one thing nor the other,
and is where the interesting results live.

# Long notes

Turn 1/4 up and 1/16 down, then open GATE MAX. Note lengths
come from the space to the next note, so a sparse pattern is
the only way to get genuinely long notes.

# Keeping what you find

The seed is the recording. Six digits, plus the knob positions,
is the entire patch.)" },
        };

        countOut = (int) (sizeof (c) / sizeof (c[0]));
        return c;
    }
}

// ---------------------------------------------------------------------------
// The scrolling body
// ---------------------------------------------------------------------------
class TutorialBody : public juce::Component
{
public:
    explicit TutorialBody (DominaLookAndFeel& l) : lnf (l) {}

    void setChapter (const juce::String& raw)
    {
        source = raw;
        rebuild();
    }

    void resized() override
    {
        // rebuild() ends by calling setSize(), which calls back into here. If a
        // scrollbar appears or disappears as a result, the width changes and the
        // two can call each other without ever settling - a hung UI thread,
        // which a host reports as the plugin misbehaving. One level deep is all
        // that is ever needed.
        if (rebuilding)
            return;

        const juce::ScopedValueSetter<bool> guard (rebuilding, true);
        rebuild();
    }

    void paint (juce::Graphics& g) override
    {
        // The editor repaints continuously, so skip anything off-screen rather
        // than laying out and drawing the whole guide every frame.
        const auto clip = g.getClipBounds().toFloat();

        for (const auto& b : blocks)
        {
            if (b.y + b.height < clip.getY() || b.y > clip.getBottom())
                continue;

            if (b.rule)
            {
                g.setColour (lnf.gridLine);
                g.fillRect (kPad, (int) b.y + 6, getWidth() - kPad * 2, 1);
                continue;
            }

            if (b.bullet)
            {
                // Drawn, not typed. A bullet character has to survive the
                // compiler's idea of the source encoding to reach the screen
                // intact; a filled circle does not.
                g.setColour (lnf.accent);
                g.fillEllipse ((float) kPad + 3.0f, b.y + 5.5f, 3.5f, 3.5f);
            }

            b.layout.draw (g, { (float) kPad + b.indent, b.y,
                                (float) getWidth() - kPad * 2 - b.indent, b.height });
        }
    }

private:
    struct Block
    {
        juce::TextLayout layout;
        float y = 0.0f, height = 0.0f, indent = 0.0f;
        bool  rule = false;
        bool  bullet = false;
    };

    void rebuild()
    {
        blocks.clear();

        const float w = (float) juce::jmax (80, getWidth() - kPad * 2);
        float y = (float) kPad;

        juce::StringArray lines;
        lines.addLines (source);

        juce::String pending;
        bool pendingHeading = false, pendingBullet = false;

        auto flush = [&]
        {
            if (pending.isEmpty())
                return;

            Block b;
            b.indent = pendingBullet ? 14.0f : 0.0f;

            juce::AttributedString a;
            a.setWordWrap (juce::AttributedString::byWord);
            a.setLineSpacing (2.0f);
            auto font = lnf.labelFont (pendingHeading ? 15.0f : 13.5f);
            if (pendingHeading)
                font = font.boldened();

            a.append (pending, font, pendingHeading ? lnf.accent : lnf.textPrimary);
            b.bullet = pendingBullet;

            b.layout.createLayout (a, w - b.indent);
            b.y      = y;
            b.height = b.layout.getHeight();
            y += b.height + (pendingHeading ? 8.0f : 5.0f);

            blocks.push_back (std::move (b));
            pending.clear();
            pendingHeading = pendingBullet = false;
        };

        for (auto line : lines)
        {
            const auto t = line.trim();

            if (t.isEmpty())            { flush(); y += 7.0f; continue; }
            if (t.startsWithChar ('#')) { flush(); pendingHeading = true;
                                          pending = t.substring (1).trim(); continue; }
            if (t.startsWithChar ('-')) { flush(); pendingBullet = true;
                                          pending = t.substring (1).trim(); continue; }

            // anything else CONTINUES the block above, so the source can be
            // hard-wrapped without the wrapping showing up in the output
            pending = pending.isEmpty() ? t : pending + " " + t;
        }

        flush();

        const int wanted = (int) y + kPad;
        if (wanted != getHeight())
            setSize (getWidth(), wanted);
    }

    static constexpr int kPad = 18;

    bool                rebuilding = false;
    DominaLookAndFeel&  lnf;
    juce::String        source;
    std::vector<Block>  blocks;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TutorialBody)
};

// ---------------------------------------------------------------------------
// Selector + body
// ---------------------------------------------------------------------------
class TutorialComponent : public juce::Component
{
public:
    explicit TutorialComponent (DominaLookAndFeel& l) : lnf (l), body (l)
    {
        int n = 0;
        const auto* ch = DominaGuide::chapters (n);

        for (int i = 0; i < n; ++i)
        {
            auto* b = tabs.add (new juce::TextButton (ch[i].title));
            b->setClickingTogglesState (true);
            b->setRadioGroupId (0x00D0417A);
            b->setColour (juce::TextButton::buttonColourId,   lnf.keyGrey);
            b->setColour (juce::TextButton::buttonOnColourId, lnf.accentDim);
            b->setColour (juce::TextButton::textColourOffId,  lnf.textSecond);
            b->setColour (juce::TextButton::textColourOnId,   lnf.textPrimary);
            b->onClick = [this, i] { show (i); };
            addAndMakeVisible (b);
        }

        view.setViewedComponent (&body, false);
        view.setScrollBarsShown (true, false);
        addAndMakeVisible (view);

        show (0);
    }

    void paint (juce::Graphics& g) override { g.fillAll (lnf.windowBg); }

    void resized() override
    {
        auto r = getLocalBounds().reduced (10);

        // two rows of six, so twelve chapters fit without shrinking to nothing
        const int cols = 6;
        const int rows = (tabs.size() + cols - 1) / cols;
        auto strip = r.removeFromTop (rows * 26 + (rows - 1) * 4);

        for (int row = 0; row < rows; ++row)
        {
            auto line = strip.removeFromTop (26);
            if (row < rows - 1)
                strip.removeFromTop (4);

            const int w = line.getWidth() / cols;

            for (int col = 0; col < cols; ++col)
            {
                const int i = row * cols + col;
                if (i >= tabs.size())
                    break;

                tabs[i]->setBounds (line.removeFromLeft (col == cols - 1 ? line.getWidth() : w)
                                        .reduced (2, 0));
            }
        }

        r.removeFromTop (8);
        view.setBounds (r);
        body.setSize (view.getMaximumVisibleWidth(), body.getHeight());
    }

private:
    void show (int index)
    {
        int n = 0;
        const auto* ch = DominaGuide::chapters (n);
        index = juce::jlimit (0, n - 1, index);

        for (int i = 0; i < tabs.size(); ++i)
            tabs[i]->setToggleState (i == index, juce::dontSendNotification);

        body.setSize (juce::jmax (80, view.getMaximumVisibleWidth()), 10);
        body.setChapter (ch[index].body);
        view.setViewPosition (0, 0);
    }

    DominaLookAndFeel&                lnf;
    juce::OwnedArray<juce::TextButton> tabs;
    juce::Viewport                     view;
    TutorialBody                       body;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TutorialComponent)
};

// ---------------------------------------------------------------------------
// The overlay
//
// A CHILD OF THE EDITOR, not a desktop window. A plugin that opens a real
// window has to create and tear down a native window from inside its editor's
// lifetime, and on macOS that is a reliable way to crash a host - the more so
// when the editor is closed while the window is still open.
//
// It is also better behaved: it cannot end up hidden behind the DAW, it moves
// with the plugin, and it closes when the plugin does because it IS the plugin.
// ---------------------------------------------------------------------------
class TutorialOverlay : public juce::Component
{
public:
    explicit TutorialOverlay (DominaLookAndFeel& l) : lnf (l), body (l)
    {
        addAndMakeVisible (body);

        closeButton.setColour (juce::TextButton::buttonColourId,  lnf.keyGrey);
        closeButton.setColour (juce::TextButton::textColourOffId, lnf.textPrimary);
        closeButton.onClick = [this] { setVisible (false); };
        addAndMakeVisible (closeButton);

        setInterceptsMouseClicks (true, true);   // swallow clicks on the panel behind
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colours::black.withAlpha (0.72f));

        g.setColour (lnf.windowBg);
        g.fillRoundedRectangle (panel().toFloat(), 6.0f);

        g.setColour (lnf.accent.withAlpha (0.5f));
        g.drawRoundedRectangle (panel().toFloat(), 6.0f, 1.0f);
    }

    void resized() override
    {
        auto r = panel().reduced (10);
        closeButton.setBounds (r.removeFromTop (24).removeFromRight (78));
        r.removeFromTop (6);
        body.setBounds (r);
    }

    void visibilityChanged() override
    {
        if (isVisible())
            toFront (true);
    }

private:
    juce::Rectangle<int> panel() const
    {
        return getLocalBounds().reduced (juce::jmax (12, getWidth()  / 10),
                                         juce::jmax (12, getHeight() / 14));
    }

    DominaLookAndFeel& lnf;
    TutorialComponent  body;
    juce::TextButton   closeButton { "CLOSE" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TutorialOverlay)
};
