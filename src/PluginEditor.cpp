
// ============================================================================
//  C:\workspace\Domina\src\PluginEditor.cpp
//  Domina - main editor (Fanan)
// ============================================================================

#include "PluginEditor.h"
#include "BinaryData.h"
#include "DebugTrace.h"

namespace
{
    constexpr int kBaseW    = 1060;
    constexpr int kBaseH    = 620;
    constexpr int kMargin   = 12;
    constexpr int kTitleH   = 34;
    constexpr int kKeysH    = 64;
    constexpr int kRibbonH  = 84;
    constexpr int kSeedH    = 96;
    constexpr int kSeedW    = 540;
    constexpr int kSecTitle = 18;
    constexpr int kKnobH    = 78;
    constexpr int kRowH     = 26;
    constexpr int kKnobLabelH = 13;
    constexpr int kKnobGap  = 14;   // breathing room between stacked knob rows
    constexpr int kGap      = 10;
}

DominaAudioProcessorEditor::DominaAudioProcessorEditor (DominaAudioProcessor& p)
    : AudioProcessorEditor (&p), proc (p), ribbon (p, lnf), seedDigits (p.apvts, lnf)
{
    DOMINA_LOG ("#" + juce::String (proc.traceId) + " editor constructed");

    setLookAndFeel (&lnf);

    // ---- ARPEGGIO ------------------------------------------------------------
    // Built from the core's own list, so a new idiom needs no edit here.
    for (int i = 0; i < fanan::idiomCount(); ++i)
        idiomBox.addItem (fanan::idiomName (i), i + 1);

    addAndMakeVisible (idiomBox);
    idiomAttach = std::make_unique<ComboAttach> (proc.apvts, "arpIdiom", idiomBox);
    idiomLabel.setText ("IDIOM", juce::dontSendNotification);
    idiomLabel.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (idiomLabel);

    snapBox.addItemList ({ "Bar", "Beat", "1/2 Beat", "1/4 Beat", "1/8 Beat" }, 1);
    addAndMakeVisible (snapBox);
    snapAttach = std::make_unique<ComboAttach> (proc.apvts, "locSnap", snapBox);
    snapLabel.setText ("LOC SNAP", juce::dontSendNotification);
    snapLabel.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (snapLabel);

    addAndMakeVisible (sortButton);
    sortAttach = std::make_unique<ButtonAttach> (proc.apvts, "arpSort", sortButton);

    addAndMakeVisible (continuousButton);
    continuousAttach = std::make_unique<ButtonAttach> (proc.apvts, "arpContinuous", continuousButton);

    addAndMakeVisible (syncButton);
    syncAttach = std::make_unique<ButtonAttach> (proc.apvts, "arpSync", syncButton);

    addKnob (bars,     "arpBars",    "BARS");
    addKnob (octaves,  "arpOctaves", "OCTAVES");
    addKnob (complexity, "arpComplexity", "COMPLEXITY");
    addKnob (originality, "arpOriginality", "ORIGINALITY");
    addKnob (w16,      "arpW16",     "1/16");
    addKnob (w8,       "arpW8",      "1/8");
    addKnob (w4,       "arpW4",      "1/4");
    addKnob (rest,     "arpRest",    "RESTS");
    addKnob (gateMin,  "arpGateMin", "GATE MIN");
    addKnob (gateMax,  "arpGateMax", "GATE MAX");
    addKnob (tempo,    "arpTempo",   "FREE BPM");
    addKnob (velScale, "velScale",   "VELOCITY");

    addAndMakeVisible (ribbon);
    addAndMakeVisible (seedDigits);

    // GUIDE sits at the right edge of the seed band, hard against the border.
    guideButton.setColour (juce::TextButton::buttonColourId,  lnf.keyGrey);
    guideButton.setColour (juce::TextButton::textColourOffId, lnf.textSecond);
    guideButton.onClick = [this]
    {
        if (tutorial != nullptr)
        {
            tutorial->toFront (true);
            return;
        }

        tutorial = std::make_unique<TutorialWindow> (lnf);
        tutorial->onClose = [this] { tutorial.reset(); };
    };
    addAndMakeVisible (guideButton);

    // ---- patches ------------------------------------------------------------
    for (auto* b : { &saveButton, &loadButton })
    {
        b->setColour (juce::TextButton::buttonColourId,  lnf.keyGrey);
        b->setColour (juce::TextButton::textColourOffId, lnf.textSecond);
        addAndMakeVisible (*b);
    }

    patchLabel.setJustificationType (juce::Justification::centred);
    patchLabel.setColour (juce::Label::textColourId, lnf.accent);
    addAndMakeVisible (patchLabel);

    saveButton.onClick = [this]
    {
        chooser = std::make_unique<juce::FileChooser> ("Save Domina patch",
                                                       DominaPatch::folder()
                                                         .getChildFile (proc.getPatchName()),
                                                       DominaPatch::wildcard());

        chooser->launchAsync (juce::FileBrowserComponent::saveMode
                                | juce::FileBrowserComponent::canSelectFiles
                                | juce::FileBrowserComponent::warnAboutOverwriting,
                              [this] (const juce::FileChooser& fc)
        {
            const auto f = fc.getResult();
            if (f == juce::File())
                return;

            proc.savePatch (f);
            refreshPatchName();
        });
    };

    loadButton.onClick = [this]
    {
        chooser = std::make_unique<juce::FileChooser> ("Load Domina patch",
                                                       DominaPatch::folder(),
                                                       DominaPatch::wildcard());

        chooser->launchAsync (juce::FileBrowserComponent::openMode
                                | juce::FileBrowserComponent::canSelectFiles,
                              [this] (const juce::FileChooser& fc)
        {
            const auto f = fc.getResult();
            if (f == juce::File() || ! f.existsAsFile())
                return;

            if (! proc.loadPatch (f))
                juce::NativeMessageBox::showMessageBoxAsync (
                    juce::MessageBoxIconType::WarningIcon, "Domina",
                    "That file is not a Domina patch:\n" + f.getFullPathName());

            refreshPatchName();
        });
    };

    refreshPatchName();

    // Accent colour lives in the settings FILE, not in the project and not as a
    // parameter. It belongs to the installation: pick a colour once and every
    // future instance opens that way, in any project, until it is changed
    // again. Keeping a second copy in the project state would mean loading an
    // old song silently overrode the choice, which is two sources of truth for
    // one setting - exactly the kind of thing that ends up feeling broken.
    accentLeds.setSelected (DominaSettings::getAccent());
    accentLeds.onChange = [this] (int i)
    {
        DominaSettings::setAccent (i);   // written through at once
        repaint();                       // children read lnf.accent live
        if (tutorial != nullptr)
            tutorial->repaint();
    };
    addAndMakeVisible (accentLeds);

    // ---- MIDI OUT ------------------------------------------------------------
    // A plain slider rather than a 16-item combo: the channel is a number and
    // scrolling through a list to reach 11 is worse than dragging to it.
    channelSlider.setSliderStyle (juce::Slider::IncDecButtons);
    channelSlider.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 48, 20);
    addAndMakeVisible (channelSlider);
    channelAttach = std::make_unique<SliderAttach> (proc.apvts, "outChannel", channelSlider);

    // RANDOM VELOCITY. The two amounts are how far below and above the VELOCITY
    // knob a note may land, so 76 with -6/+8 spans 70 to 84. They only appear
    // when the toggle is on - three controls for something that is off by
    // default would be three controls of clutter.
    addAndMakeVisible (velRandButton);
    velRandAttach = std::make_unique<ButtonAttach> (proc.apvts, "velRandom", velRandButton);
    velRandButton.onStateChange = [this] { updateVelRandVisibility(); };

    for (auto* sl : { &velDownSlider, &velUpSlider })
    {
        sl->setSliderStyle (juce::Slider::IncDecButtons);
        sl->setTextBoxStyle (juce::Slider::TextBoxLeft, false, 40, 20);
        addChildComponent (*sl);          // hidden until the toggle is on
    }

    velDownAttach = std::make_unique<SliderAttach> (proc.apvts, "velRandDown", velDownSlider);
    velUpAttach   = std::make_unique<SliderAttach> (proc.apvts, "velRandUp",   velUpSlider);

    velRangeLabel.setText ("FROM  -", juce::dontSendNotification);
    velRangeLabel.setJustificationType (juce::Justification::centredLeft);
    addChildComponent (velRangeLabel);

    updateVelRandVisibility();

    channelLabel.setText ("OUT CH", juce::dontSendNotification);
    channelLabel.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (channelLabel);

    muteButton.setClickingTogglesState (true);
    muteButton.setColour (juce::TextButton::buttonColourId,   lnf.keyGrey);
    muteButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xFFA32D2D));
    muteButton.setColour (juce::TextButton::textColourOffId,  lnf.textSecond);
    muteButton.setColour (juce::TextButton::textColourOnId,   lnf.textPrimary);
    addAndMakeVisible (muteButton);
    muteAttach = std::make_unique<ButtonAttach> (proc.apvts, "mute", muteButton);

    // MUTE ARP silences the arpeggiator only - what you play still goes out.
    muteArpButton.setClickingTogglesState (true);
    muteArpButton.setColour (juce::TextButton::buttonColourId,   lnf.keyGrey);
    muteArpButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xFFA32D2D));
    muteArpButton.setColour (juce::TextButton::textColourOffId,  lnf.textSecond);
    muteArpButton.setColour (juce::TextButton::textColourOnId,   lnf.textPrimary);
    addAndMakeVisible (muteArpButton);
    muteArpAttach = std::make_unique<ButtonAttach> (proc.apvts, "muteArp", muteArpButton);

    // ---- always on -----------------------------------------------------------
    addAndMakeVisible (keyboard);

    // A mouse has one pointer, so without latch the on-screen keyboard cannot
    // hold a chord at all - and a chord is what the arp needs.
    holdButton.setClickingTogglesState (true);
    holdButton.setClickingTogglesState (true);
    holdButton.setColour (juce::TextButton::buttonOnColourId, lnf.accentDim);
    holdButton.setColour (juce::TextButton::textColourOnId,   lnf.textPrimary);
    // HOLD no longer latches the on-screen keyboard: it locks the SEQUENCE to
    // the bar that is playing, as though the locators had closed around it.
    holdAttach = std::make_unique<ButtonAttach> (proc.apvts, "arpHold", holdButton);
    addAndMakeVisible (holdButton);

    panicButton.onClick = [this]
    {
        keyboard.allNotesOff();   // clear the on-screen keys
        proc.requestPanic();      // and make the OUTPUT stop, whatever it thinks
    };
    addAndMakeVisible (panicButton);

    logo = juce::ImageCache::getFromMemory (BinaryData::fanan_logo_png,
                                            BinaryData::fanan_logo_pngSize);

    addAndMakeVisible (ccChip);

    // ---- MIDI learn registry -------------------------------------------------
    // Knobs registered themselves in addKnob(); these are the rest.
    registerLearnable (seedDigits.getRandomButton(), fanan::MidiLearn::kSeedRandom, "Random seed");
    registerLearnable (snapBox,       "locSnap",    "Locator snap");
    registerLearnable (idiomBox,      "arpIdiom",   "Idiom");
    registerLearnable (sortButton,       "arpSort",       "Sort notes");
    registerLearnable (continuousButton, "arpContinuous", "Continuous");
    registerLearnable (syncButton,    "arpSync",    "Sync to DAW");
    registerLearnable (muteButton,    "mute",       "Mute output");
    registerLearnable (muteArpButton, "muteArp",    "Mute arp");
    registerLearnable (velRandButton, "velRandom",  "Random velocity");
    registerLearnable (holdButton,    "arpHold",    "Hold bar");
    registerLearnable (channelSlider, "outChannel", "Output channel");

    startTimerHz (20);

    setResizable (true, true);
    if (auto* c = getConstrainer())
        c->setFixedAspectRatio ((double) kBaseW / (double) kBaseH);
    setResizeLimits (kBaseW * 3 / 4, kBaseH * 3 / 4, kBaseW * 3 / 2, kBaseH * 3 / 2);
    setSize (kBaseW, kBaseH);
}

DominaAudioProcessorEditor::~DominaAudioProcessorEditor()
{
    DOMINA_LOG ("#" + juce::String (proc.traceId) + " editor destroyed");

    stopTimer();

    for (auto* k : { &bars, &octaves, &complexity, &originality, &w16, &w8, &w4, &rest,
                     &gateMin, &gateMax, &tempo, &velScale })
        k->slider.setLookAndFeel (nullptr);

    snapBox.setLookAndFeel (nullptr);
    idiomBox.setLookAndFeel (nullptr);
    channelSlider.setLookAndFeel (nullptr);
    velRandButton.setLookAndFeel (nullptr);
    velDownSlider.setLookAndFeel (nullptr);
    velUpSlider.setLookAndFeel (nullptr);
    sortButton.setLookAndFeel (nullptr);
    continuousButton.setLookAndFeel (nullptr);
    syncButton.setLookAndFeel (nullptr);
    muteButton.setLookAndFeel (nullptr);
    muteArpButton.setLookAndFeel (nullptr);
    guideButton.setLookAndFeel (nullptr);
    saveButton.setLookAndFeel (nullptr);
    loadButton.setLookAndFeel (nullptr);
    tutorial.reset();                 // the window holds a reference to lnf
    setLookAndFeel (nullptr);
}

// ---------------------------------------------------------------------------
// MIDI learn
// ---------------------------------------------------------------------------
void DominaAudioProcessorEditor::registerLearnable (juce::Component& c,
                                                    const juce::String& id,
                                                    const juce::String& name)
{
    learnables.push_back ({ &c, id, name });

    // A listener rather than a subclass: this has to work for Sliders,
    // ComboBoxes and Buttons alike, and wrapping three widget families to catch
    // one mouse button is not worth the code.
    c.addMouseListener (this, false);
}

const DominaAudioProcessorEditor::Learnable*
DominaAudioProcessorEditor::learnableAt (juce::Point<int> pointInEditor) const
{
    for (const auto& l : learnables)
    {
        if (l.comp == nullptr || ! l.comp->isShowing())
            continue;

        if (getLocalArea (l.comp, l.comp->getLocalBounds()).contains (pointInEditor))
            return &l;
    }

    return nullptr;
}

void DominaAudioProcessorEditor::mouseDown (const juce::MouseEvent& e)
{
    if (! e.mods.isPopupMenu())
        return;

    if (const auto* l = learnableAt (e.getEventRelativeTo (this).getPosition()))
        showLearnMenu (*l);
}

void DominaAudioProcessorEditor::showLearnMenu (const Learnable& l)
{
    const int cc = proc.midiLearn.ccFor (l.id);
    const juce::String id = l.id;

    juce::PopupMenu m;
    m.addSectionHeader (l.name);
    m.addItem (1, "Learn  (then move a control)");
    m.addItem (2, "Forget", cc >= 0);
    m.addSeparator();
    m.addItem (3, cc >= 0 ? ("Assigned to CC " + juce::String (cc))
                          : juce::String ("Not assigned"), false);

    m.showMenuAsync (juce::PopupMenu::Options().withMousePosition(),
                     [this, id] (int result)
                     {
                         if (result == 1)      proc.midiLearn.arm (id);
                         else if (result == 2) proc.midiLearn.unassign (id);

                         repaint();
                     });
}

// CC values are applied HERE, on the message thread, rather than in the audio
// callback: setValueNotifyingHost runs host listeners, and a host is entitled
// to do anything it likes inside them.
void DominaAudioProcessorEditor::applyPendingMidi()
{
    auto& learn = proc.midiLearn;

    for (int slot = 0; slot < learn.numSlots(); ++slot)
    {
        float v = 0.0f;
        if (! learn.consume (slot, v))
            continue;

        const auto& id = learn.idAt (slot);

        if (id == fanan::MidiLearn::kSeedRandom)
        {
            // Momentary: fire on the upper half only, so a fader parked high
            // does not re-roll on every wiggle.
            if (v >= 0.5f)
                seedDigits.randomise();

            continue;
        }

        if (auto* param = proc.apvts.getParameter (id))
        {
            param->beginChangeGesture();
            param->setValueNotifyingHost (v);
            param->endChangeGesture();
        }
    }

    if (learn.consumeArmDone())
        repaint();
}

bool DominaAudioProcessorEditor::isInterestedInDragSource (const SourceDetails& d)
{
    return d.description.toString().startsWith ("MIDICC:");
}

void DominaAudioProcessorEditor::itemDragEnter (const SourceDetails& d) { itemDragMove (d); }

void DominaAudioProcessorEditor::itemDragMove (const SourceDetails& d)
{
    const auto* l = learnableAt (d.localPosition);
    juce::Component* c = (l != nullptr) ? l->comp : nullptr;

    if (c == dropHighlight)
        return;

    dropHighlight = c;
    repaint();
}

void DominaAudioProcessorEditor::itemDragExit (const SourceDetails&)
{
    dropHighlight = nullptr;
    repaint();
}

void DominaAudioProcessorEditor::itemDropped (const SourceDetails& d)
{
    dropHighlight = nullptr;

    const int cc = d.description.toString().fromFirstOccurrenceOf (":", false, false).getIntValue();

    if (const auto* l = learnableAt (d.localPosition))
        if (cc >= 0)
            proc.midiLearn.assign (l->id, cc);

    repaint();
}

// A bound control gets a small teal dot in its top-right corner; an armed one
// gets a ring. Painted over the children so no widget needs to know about it.
void DominaAudioProcessorEditor::paintLearnBadges (juce::Graphics& g)
{
    for (const auto& l : learnables)
    {
        if (l.comp == nullptr || ! l.comp->isShowing())
            continue;

        const auto b = getLocalArea (l.comp, l.comp->getLocalBounds());

        if (l.comp == dropHighlight)
        {
            g.setColour (lnf.accent);
            g.drawRoundedRectangle (b.toFloat().expanded (2.0f), 4.0f, 2.0f);
        }

        if (proc.midiLearn.isArmed (l.id))
        {
            g.setColour (lnf.accent);
            g.drawRoundedRectangle (b.toFloat().expanded (2.0f), 4.0f, 1.5f);
        }

        if (proc.midiLearn.ccFor (l.id) >= 0)
        {
            g.setColour (lnf.accent);
            g.fillEllipse ((float) b.getRight() - 6.0f, (float) b.getY() + 1.0f, 5.0f, 5.0f);
        }
    }
}

void DominaAudioProcessorEditor::paintOverChildren (juce::Graphics& g)
{
    paintLearnBadges (g);
}

void DominaAudioProcessorEditor::refreshPatchName()
{
    const auto n = proc.getPatchName();

    if (patchLabel.getText() != n)
        patchLabel.setText (n, juce::dontSendNotification);
}

void DominaAudioProcessorEditor::updateVelRandVisibility()
{
    const bool on = velRandButton.getToggleState();

    velRangeLabel.setVisible (on);
    velDownSlider.setVisible (on);
    velUpSlider.setVisible (on);
}

void DominaAudioProcessorEditor::timerCallback()
{
    applyPendingMidi();
    refreshPatchName();      // a project recall changes it behind our back

   #if DOMINA_TRACE
    // Drained on the message thread; the audio thread only ever writes into the
    // ring. Appended raw so the file is the exact order things happened in.
    {
        const auto notes = proc.drainNoteTrace();
        if (notes.isNotEmpty())
            DominaTrace::log (juce::String ("#") + juce::String (proc.traceId)
                                + " notes:" + juce::newLine + notes);
    }
   #endif

    // An idiom written in another meter would be smeared across the bar, so the
    // core refuses to apply it. Showing that is better than leaving a button
    // that looks live and does nothing.
    const float meter = proc.getHostBeatsPerBar();

    if (std::abs (meter - lastMeter) > 0.01f)
    {
        lastMeter = meter;

        for (int i = 0; i < fanan::idiomCount(); ++i)
            idiomBox.setItemEnabled (i + 1, fanan::idiomFitsMeter (i, (double) meter));
    }
}

// ---------------------------------------------------------------------------
// Widgets
// ---------------------------------------------------------------------------
void DominaAudioProcessorEditor::addKnob (Knob& k, const juce::String& paramID,
                                          const juce::String& text)
{
    k.slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    k.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 66, 15);
    k.slider.setPopupDisplayEnabled (true, true, this);
    addAndMakeVisible (k.slider);

    k.label.setText (text, juce::dontSendNotification);
    k.label.setJustificationType (juce::Justification::centred);
    k.label.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (k.label);

    k.attach = std::make_unique<SliderAttach> (proc.apvts, paramID, k.slider);

    registerLearnable (k.slider, paramID, text);
}

void DominaAudioProcessorEditor::paintSectionTitle (juce::Graphics& g,
                                                    juce::Rectangle<int> r,
                                                    const juce::String& t)
{
    g.setColour (lnf.textMuted);
    g.setFont (lnf.labelFont (10.5f));
    g.drawText (t, r, juce::Justification::centredLeft, false);

    const int textW = 8 + (int) juce::GlyphArrangement::getStringWidthInt (lnf.labelFont (10.5f), t);
    if (r.getWidth() > textW)
    {
        g.setColour (lnf.gridLine);
        g.drawHorizontalLine (r.getCentreY(), (float) (r.getX() + textW), (float) r.getRight());
    }
}

void DominaAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (lnf.windowBg);

    auto panel = [&] (juce::Rectangle<int> r)
    {
        if (r.isEmpty())
            return;
        g.setColour (lnf.panelBg);
        g.fillRoundedRectangle (r.toFloat(), 5.0f);
        g.setColour (lnf.gridLine);
        g.drawRoundedRectangle (r.toFloat().reduced (0.5f), 5.0f, 1.0f);
    };

    g.setColour (lnf.textPrimary);
    g.setFont (lnf.labelFont (21.0f).boldened());
    g.drawText ("DOMINA", kMargin + 4, kMargin - 2, 220, kTitleH,
                juce::Justification::centredLeft, false);

    // The logo carries the company name now, so the strapline dropped it.
    g.setColour (lnf.accent);
    g.setFont (lnf.labelFont (10.0f));
    g.drawText ("SEEDED MIDI ARPEGGIATOR",
                kMargin + 100, kMargin, 400, kTitleH,
                juce::Justification::centredLeft, false);

    // Centred in the header, sized off its own aspect so it can be replaced
    // with a different logo without touching this.
    if (logo.isValid() && logo.getHeight() > 0 && ! titleBar.isEmpty())
    {
        const int h = juce::jmax (1, titleBar.getHeight() - 4);
        const int w = juce::roundToInt (h * (logo.getWidth() / (double) logo.getHeight()));

        g.drawImage (logo,
                     juce::Rectangle<int> (0, 0, w, h).withCentre (titleBar.getCentre()).toFloat(),
                     juce::RectanglePlacement::centred);
    }

    panel (seedPanel);
    panel (secArp.withTop (secArp.getY() - 6).withBottom (keyboard.getY() - kGap));
    panel (secRhythm.withTop (secRhythm.getY() - 6).withBottom (keyboard.getY() - kGap));
    panel (secOut.withTop (secOut.getY() - 6).withBottom (keyboard.getY() - kGap));

    paintSectionTitle (g, secArp.reduced (8, 0),    "ARPEGGIO");
    paintSectionTitle (g, secRhythm.reduced (8, 0), "RHYTHM");
    paintSectionTitle (g, secOut.reduced (8, 0),    "MIDI OUT");
}

void DominaAudioProcessorEditor::layoutKnobRow (juce::Rectangle<int> area,
                                                std::initializer_list<Knob*> knobs)
{
    const int n = (int) knobs.size();
    if (n <= 0 || area.isEmpty())
        return;

    const int w = area.getWidth() / n;
    int i = 0;

    for (auto* k : knobs)
    {
        // Label UNDER the slider, which puts it under the value box the slider
        // draws at its own bottom. Reading order is dial -> number -> name.
        auto cell = area.withX (area.getX() + i * w).withWidth (w).reduced (4, 0);
        k->label.setBounds (cell.removeFromBottom (kKnobLabelH));
        k->slider.setBounds (cell);
        ++i;
    }
}

void DominaAudioProcessorEditor::layoutControlRow (juce::Rectangle<int> row, juce::Label& lab,
                                                   juce::Component& control, int labelW)
{
    lab.setBounds (row.removeFromLeft (labelW));
    control.setBounds (row.reduced (0, 1));
}

void DominaAudioProcessorEditor::resized()
{
    auto r = getLocalBounds().reduced (kMargin);

    // ---- title row -----------------------------------------------------------
    auto titleRow = r.removeFromTop (kTitleH);
    titleBar = titleRow;
    muteButton.setBounds (titleRow.removeFromRight (100).reduced (0, 5));
    titleRow.removeFromRight (8);
    muteArpButton.setBounds (titleRow.removeFromRight (100).reduced (0, 5));
    titleRow.removeFromRight (12);
    ccChip.setBounds (titleRow.removeFromRight (96).reduced (0, 3));
    r.removeFromTop (kGap);

    // ---- keyboard, always at the bottom --------------------------------------
    {
        auto keyRow  = r.removeFromBottom (kKeysH);
        auto keyBtns = keyRow.removeFromLeft (78);
        holdButton .setBounds (keyBtns.removeFromTop (kKeysH / 2).reduced (2));
        panicButton.setBounds (keyBtns.reduced (2));
        keyRow.removeFromLeft (8);
        keyboard.setBounds (keyRow);
    }
    r.removeFromBottom (kGap);

    // ---- ribbon + seed -------------------------------------------------------
    ribbon.setBounds (r.removeFromTop (kRibbonH));
    r.removeFromTop (kGap);

    auto seedBand = r.removeFromTop (kSeedH);
    seedPanel = seedBand;

    // Both edges are taken off BEFORE the knobs are placed, and by the same
    // amount, so the seed row stays centred in the window rather than being
    // pushed off-axis by whatever sits beside it.
    {
        auto ledCol   = seedBand.removeFromLeft  (196);
        auto patchCol = seedBand.removeFromRight (196);

        accentLeds.setBounds (ledCol.withSizeKeepingCentre (96, 16));

        // patch name over a row of SAVE / LOAD / GUIDE
        auto stack = patchCol.withSizeKeepingCentre (188, 48);
        patchLabel.setBounds (stack.removeFromTop (18));
        stack.removeFromTop (4);

        const int w = stack.getWidth() / 3;
        saveButton .setBounds (stack.removeFromLeft (w).reduced (2, 0));
        loadButton .setBounds (stack.removeFromLeft (w).reduced (2, 0));
        guideButton.setBounds (stack.reduced (2, 0));
    }

    seedDigits.setBounds (seedBand.withSizeKeepingCentre (juce::jmin (kSeedW, seedBand.getWidth() - 16),
                                                          seedBand.getHeight() - 8));
    r.removeFromTop (kGap);

    // ---- three columns -------------------------------------------------------
    const int cw = r.getWidth() / 3;
    auto c1 = r.removeFromLeft (cw).reduced (6, 0);
    auto c2 = r.removeFromLeft (cw).reduced (6, 0);
    auto c3 = r.reduced (6, 0);

    // ARPEGGIO
    c1.removeFromTop (6);
    secArp = c1.removeFromTop (kSecTitle);
    c1.removeFromTop (4);
    layoutControlRow (c1.removeFromTop (kRowH).reduced (8, 0), idiomLabel, idiomBox, 46);
    c1.removeFromTop (6);
    layoutKnobRow (c1.removeFromTop (kKnobH).reduced (4, 0), { &bars, &octaves });
    c1.removeFromTop (kKnobGap);
    layoutKnobRow (c1.removeFromTop (kKnobH).reduced (4, 0), { &complexity, &originality });

    // RHYTHM
    c2.removeFromTop (6);
    secRhythm = c2.removeFromTop (kSecTitle);
    c2.removeFromTop (4);
    layoutKnobRow (c2.removeFromTop (kKnobH).reduced (4, 0), { &w16, &w8, &w4 });
    c2.removeFromTop (kKnobGap);
    layoutKnobRow (c2.removeFromTop (kKnobH).reduced (4, 0), { &rest, &gateMin, &gateMax });

    // MIDI OUT
    c3.removeFromTop (6);
    secOut = c3.removeFromTop (kSecTitle);
    c3.removeFromTop (4);
    layoutControlRow (c3.removeFromTop (kRowH).reduced (8, 0), snapLabel, snapBox, 66);
    c3.removeFromTop (4);
    layoutControlRow (c3.removeFromTop (kRowH).reduced (8, 0), channelLabel, channelSlider, 66);
    c3.removeFromTop (4);
    {
        // SORT NOTES and CONTINUOUS share one row, half the column each.
        auto toggles = c3.removeFromTop (24).reduced (8, 0);
        sortButton      .setBounds (toggles.removeFromLeft (toggles.getWidth() / 2));
        continuousButton.setBounds (toggles);
    }
    c3.removeFromTop (2);
    syncButton.setBounds (c3.removeFromTop (24).reduced (8, 0));
    c3.removeFromTop (2);

    // The row under the toggle is always reserved, so turning RANDOM VELOCITY
    // on does not shove the knobs below it down the panel.
    velRandButton.setBounds (c3.removeFromTop (24).reduced (8, 0));
    {
        auto row = c3.removeFromTop (22).reduced (8, 0);
        velRangeLabel.setBounds (row.removeFromLeft (52));
        velDownSlider.setBounds (row.removeFromLeft (juce::jmax (60, row.getWidth() / 2 - 12)));
        row.removeFromLeft (6);
        velUpSlider.setBounds (row);
    }
    c3.removeFromTop (4);
    layoutKnobRow (c3.removeFromTop (kKnobH).reduced (4, 0), { &tempo, &velScale });
}



