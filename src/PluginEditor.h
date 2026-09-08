
// ============================================================================
//  C:\workspace\Domina\src\PluginEditor.h
//  Domina - main editor (Fanan)
//
//  One page again. Rhythm ribbon across the top, the six-digit seed dial
//  beneath it, then the controls in three columns, then the keyboard.
//
//  The tab strip, the strum and pick grids and the sound page are gone with the
//  engines they belonged to. What stayed is what serves the arp: the ribbon,
//  the seed, the keyboard for auditioning chords, and the MIDI-learn system.
//
//  It is a DragAndDropContainer and Target because MIDI learn works by dragging
//  the CC chip onto a control; it is a Timer because the learn system applies
//  incoming CC values on the message thread.
// ============================================================================

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <memory>
#include <vector>

#include "PluginProcessor.h"
#include "CcMonitorChip.h"
#include "TutorialWindow.h"
#include "AccentLeds.h"
#include "UiSettings.h"
#include "DominaLookAndFeel.h"
#include "RhythmRibbon.h"
#include "SeedDigits.h"
#include "VirtualKeyboard.h"

class DominaAudioProcessorEditor : public juce::AudioProcessorEditor,
                                   public  juce::DragAndDropContainer,
                                   public  juce::DragAndDropTarget,
                                   private juce::Timer
{
public:
    explicit DominaAudioProcessorEditor (DominaAudioProcessor&);
    ~DominaAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;

private:
    using SliderAttach = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboAttach  = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using ButtonAttach = juce::AudioProcessorValueTreeState::ButtonAttachment;

    struct Knob
    {
        juce::Slider slider;
        juce::Label  label;
        std::unique_ptr<SliderAttach> attach;
    };

    void addKnob (Knob& k, const juce::String& paramID, const juce::String& text);
    void layoutKnobRow (juce::Rectangle<int> area, std::initializer_list<Knob*> knobs);
    // Takes a Component, not a ComboBox: the row is the same whether the control
    // is a combo or the channel stepper, and one signature beats an overload.
    void layoutControlRow (juce::Rectangle<int> row, juce::Label& lab,
                           juce::Component& control, int labelW);
    void paintSectionTitle (juce::Graphics& g, juce::Rectangle<int> r, const juce::String& t);

    // ---- MIDI learn ----------------------------------------------------------
    struct Learnable { juce::Component* comp; juce::String id; juce::String name; };

    void registerLearnable (juce::Component& c, const juce::String& id, const juce::String& name);
    const Learnable* learnableAt (juce::Point<int> pointInEditor) const;
    void showLearnMenu (const Learnable& l);
    void applyPendingMidi();
    void paintLearnBadges (juce::Graphics& g);

    bool isInterestedInDragSource (const SourceDetails&) override;
    void itemDragEnter (const SourceDetails&) override;
    void itemDragMove  (const SourceDetails&) override;
    void itemDragExit  (const SourceDetails&) override;
    void itemDropped   (const SourceDetails&) override;

    // Component already inherits MouseListener, which is what lets this catch
    // right-clicks on registered children via addMouseListener.
    void mouseDown (const juce::MouseEvent& e) override;

    void timerCallback() override;
    void updateVelRandVisibility();
    void refreshPatchName();

    DominaAudioProcessor& proc;
    DominaLookAndFeel     lnf;

    std::vector<Learnable> learnables;
    juce::Component*       dropHighlight = nullptr;

    juce::ComboBox snapBox, idiomBox;
    std::unique_ptr<ComboAttach> snapAttach, idiomAttach;
    std::unique_ptr<SliderAttach> channelAttach;
    juce::Slider channelSlider;

    juce::ToggleButton velRandButton { "RANDOM VELOCITY" };
    juce::Slider velDownSlider, velUpSlider;
    juce::Label  velRangeLabel;
    std::unique_ptr<ButtonAttach> velRandAttach;
    std::unique_ptr<SliderAttach> velDownAttach, velUpAttach;
    juce::Label snapLabel, channelLabel, idiomLabel;

    juce::ToggleButton sortButton { "Sort notes" };
    juce::ToggleButton continuousButton { "Continuous" };
    juce::ToggleButton syncButton { "Sync to DAW" };
    std::unique_ptr<ButtonAttach> sortAttach, continuousAttach, syncAttach;

    juce::TextButton muteButton    { "MUTE OUT" };
    juce::TextButton muteArpButton { "MUTE ARP" };
    std::unique_ptr<ButtonAttach> muteAttach, muteArpAttach, holdAttach;

    Knob bars, octaves, complexity, originality;
    Knob w16, w8, w4, rest, gateMin, gateMax;
    Knob tempo, velScale;

    RhythmRibbon ribbon;
    SeedDigits   seedDigits;

    VirtualKeyboard  keyboard { proc.keyboardState, lnf };
    juce::TextButton holdButton  { "HOLD" };
    juce::TextButton guideButton { "GUIDE" };
    juce::TextButton saveButton  { "SAVE" };
    juce::TextButton loadButton  { "LOAD" };
    juce::Label      patchLabel;
    std::unique_ptr<juce::FileChooser> chooser;   // must outlive launchAsync
    AccentLedStrip   accentLeds { lnf };
    std::unique_ptr<TutorialWindow> tutorial;
    juce::TextButton panicButton { "PANIC" };

    CcMonitorChip ccChip { proc.midiLearn, lnf };

    juce::Image logo;
    float lastMeter = 0.0f;      // so idiom greying only redraws when it changes

    juce::Rectangle<int> titleBar, secArp, secRhythm, secOut, seedPanel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DominaAudioProcessorEditor)
};



