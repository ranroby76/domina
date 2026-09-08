
// ============================================================================
//  C:\workspace\Domina\src\PluginProcessor.h
//  Domina - seeded MIDI arpeggiator (Fanan)
//
//  Notes in, notes out. Domina holds SeededArpCore and turns whatever chord is
//  held into a stream of MIDI notes on the host's timeline. It makes no sound
//  of its own: the audio bus exists only because it is loaded as an INSTRUMENT,
//  and it is filled with silence every block.
//
//  Instrument rather than MIDI effect on purpose - see the note in CMakeLists.
//  The bus setup and isBusesLayoutSupported are still written against
//  JucePlugin_IsMidiEffect, so flipping the two CMake flags back is the whole
//  change if a host ever makes the effect form worth having.
//
//  WHY THERE IS NO SOUND ENGINE. There was one, and a guitar strummer and
//  picker on top of it. All of it competed with the arp for the same window and
//  none of it made the arp better. A MIDI arpeggiator plays through whatever
//  instrument you already own, which is a larger instrument collection than any
//  built-in sampler could be.
//
//  SCHEDULING. The pattern is baked once per cycle and then walked in segments
//  between incoming MIDI events, so note positions are sample-accurate rather
//  than quantised to the block. Note-offs that fall past the end of a block are
//  carried in `pendingOffs` in BEATS, so they survive a tempo change between
//  the block that started the note and the block that ends it.
// ============================================================================

#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

#include "MidiLearn.h"
#include "SeededArpCore.h"

// traceId below is initialised from DominaTrace::nextInstanceId(), so the
// declaration has to be visible HERE, not only in the .cpp files.
#include "DebugTrace.h"
#include "PatchManager.h"

// clap-juce-extensions defines HAS_CLAP_JUCE_EXTENSIONS=1 on every target it is
// linked into, so this needs no help from CMakeLists and stays 0 automatically
// when CLAP is skipped. The VST3 compiles the same file, so everything below
// must be guarded.
#if defined (HAS_CLAP_JUCE_EXTENSIONS) && HAS_CLAP_JUCE_EXTENSIONS
 #include <clap-juce-extensions/clap-juce-extensions.h>
 #define DOMINA_CLAP 1
#else
 #define DOMINA_CLAP 0
#endif

class DominaAudioProcessor : public juce::AudioProcessor
                            #if DOMINA_CLAP
                             , public clap_juce_extensions::clap_juce_audio_processor_capabilities
                            #endif
{
public:
    DominaAudioProcessor();
    ~DominaAudioProcessor() override;

    // ---- AudioProcessor -----------------------------------------------------
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    using juce::AudioProcessor::processBlock;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override                     { return true; }

    const juce::String getName() const override         { return JucePlugin_Name; }
    bool acceptsMidi() const override                   { return true; }
    bool producesMidi() const override                  { return true; }
    // Tracks the CMake flag rather than a hardcoded answer, so the two can
    // never disagree about what this plugin is.
    bool isMidiEffect() const override                  { return JucePlugin_IsMidiEffect != 0; }
    double getTailLengthSeconds() const override        { return 0.0; }

    int getNumPrograms() override                       { return 1; }
    int getCurrentProgram() override                    { return 0; }
    void setCurrentProgram (int) override               {}
    const juce::String getProgramName (int) override    { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;

    // The on-screen keyboard writes here; processBlock merges it into the
    // incoming MIDI. Standard JUCE route from GUI thread to audio thread.
    juce::MidiKeyboardState keyboardState;

    // Learnable controls. Lock-free, so the editor arms and polls it directly.
    fanan::MidiLearn midiLearn;

    // ---- read by the editor (GUI thread, lock-free) -------------------------
    bool  readPattern (fanan::PatternSnapshot& out) const { return patternPub.read (out); }
    bool  readChord   (fanan::ChordSnapshot& out)   const { return chordPub.read (out); }
    float getPlayPosition() const noexcept { return playPosition.load (std::memory_order_relaxed); }
    bool  isArpRunning()   const noexcept { return arpActive.load (std::memory_order_relaxed); }

    float getSnapBeats() const;

    // The project's meter, so the editor can grey out idioms written in another
    // one. Published from processBlock; the editor polls it on its timer.
    float getHostBeatsPerBar() const noexcept
    { return hostBeatsPerBar.load (std::memory_order_relaxed); }

   #if DOMINA_CLAP
    // ---- CLAP note output ---------------------------------------------------
    //
    // Without this, the wrapper falls back to its own path and sends everything
    // as CLAP_EVENT_MIDI. That is legal CLAP and works in Bitwig and REAPER, but
    // Studio One - and therefore Fender Studio, which is Studio One rebranded -
    // enumerates the note output port, offers it for routing, and then discards
    // every CLAP_EVENT_MIDI that arrives on it. Note on/off go out as real CLAP
    // note events instead, which it does accept.
    //
    // Overriding supportsOutboundEvents() takes over the WHOLE outbound path,
    // so anything Domina passes through has to be handled here too.
    bool supportsOutboundEvents() override                  { return true; }
    void addOutboundEventsToQueue (const clap_output_events* outEvents,
                                   const juce::MidiBuffer& midiBuffer,
                                   int sampleOffset) override;

    // Advertise and prefer the CLAP dialect on the OUTPUT port only. The input
    // port stays on the MIDI dialect: the wrapper converts inbound CLAP note
    // events to juce::MidiMessage either way, but CC and pitch bend only reach
    // us as raw MIDI, and MIDI learn needs them.
    bool supportsNoteDialectClap (bool isInput) override    { return ! isInput; }
    bool prefersNoteDialectClap  (bool isInput) override    { return ! isInput; }
   #endif

   #if DOMINA_CLAP
    // Every sounding note's CLAP note id, indexed by channel and key, -1 when
    // the key is not held. A note-off has to carry the SAME id as the note-on
    // it ends: the spec says -1 means "unspecified" and the host should then
    // match on port/channel/key, but a host that does not implement that
    // fallback simply never ends the note, and the result is a chord that
    // sustains forever while the instrument downstream stacks voices.
    int32_t clapNoteIds[16][128] {};
    int32_t clapNextNoteId = 0;
   #endif

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    void   refreshArpParams();
    void   renderSegment (double segStartBeat, double segEndBeat,
                          int segStartSample, int segEndSample);
    void   flushAllPendingOffs (int atSample);

    // Hands the held chord between the arp and the direct pass-through when
    // MUTE ARP is toggled, so nothing is left sounding by the path it just left.
    void   handleArpMuteTransition (bool nowMuted, int atSample, double atBeat);

    // Sends a note-off for everything Domina currently has sounding, by either
    // path. The ONLY way to be certain nothing is stranded when the output
    // changes hands.
    void   silenceEverything (int atSample);
    void   silenceBruteForce (int atSample);

   public:
    // PANIC from the editor. Queued for the audio thread rather than emitted
    // there: MIDI must not be written from the message thread.
    void   requestPanic() noexcept { panicPending.store (true, std::memory_order_relaxed); }

    // ---- patches ------------------------------------------------------------
    //
    // ONE serialisation, used by both the project state and the .dompatch file.
    // If these ever became two functions they would drift, and a patch saved by
    // one build would load into another with pieces missing.
    juce::ValueTree captureState();          // not const: copyState() is not
    void            applyState (const juce::ValueTree& tree);

    bool savePatch (const juce::File& file);
    bool loadPatch (const juce::File& file);

    // The active patch's name, remembered in the project state so reopening a
    // song shows what is loaded rather than just its values.
    juce::String getPatchName() const
    { return apvts.state.getProperty ("patchName", "Init").toString(); }
   private:
    std::atomic<bool> panicPending { false };
    int    sampleForBeat (double beat, int loSample, int hiSample) const;
    double beatAtSample (int sample) const;

    // src tags WHICH path emitted this, purely for the diagnostic trace:
    //   0 arp   1 pass-through   2 pending flush   3 silence-all
    //   4 panic 5 mute re-arm
    void   emitNoteOn  (int sample, int note, float velocity, int src = 0);
    void   emitNoteOff (int sample, int note, int src = 0);

    double gatherBeatsForChordAt (int atSample, int numSamples, int gatherSamples) const;

    struct PendingOff { int note; double beat; };
    struct TimedNote  { int sample; bool isOn; int note; float vel; };

    fanan::SeededArpCore          arp;
    std::vector<fanan::ArpEvent>  arpEvents;
    std::vector<PendingOff>       pendingOffs;
    std::vector<TimedNote>        segmentNotes;
    std::vector<int>              noteOnSamples;

    // Output is assembled here and swapped into the host's buffer at the end of
    // the block, so the input can be walked while the output is being written.
    juce::MidiBuffer outMidi;
    juce::MidiBuffer inMidi;

    fanan::SeqLockPublisher<fanan::PatternSnapshot> patternPub;
    fanan::SeqLockPublisher<fanan::ChordSnapshot>   chordPub;
    fanan::ChordSnapshot                            lastChord;
    std::atomic<float> playPosition   { 0.0f };
    std::atomic<float> hostBeatsPerBar { 4.0f };
    std::atomic<bool>  arpActive    { false };

    bool  keyDown[128] {};
    float keyVel[128]  {};

    std::atomic<float> *pSeed {}, *pBars {}, *pOctaves {},
                       *pW16 {}, *pW8 {}, *pW4 {}, *pRest {},
                       *pGateMin {}, *pGateMax {}, *pComplexity {},
                       *pSort {}, *pTempo {}, *pSnap {},
                       *pLoopStart {}, *pLoopEnd {},
                       *pSync {}, *pMute {}, *pOutChannel {}, *pVelScale {},
                       *pContinuous {}, *pIdiom {}, *pOriginality {}, *pMuteArp {}, *pHold {},
                       *pVelRandom {}, *pVelRandDn {}, *pVelRandUp {};

    // Audio thread only, so a plain Random is fine and no lock is needed.
    juce::Random velRng;

   #if DOMINA_TRACE
   public:
    const int traceId = DominaTrace::nextInstanceId();
   private:
   #endif

    // What Domina has actually told the world is sounding, maintained by
    // emitNoteOn/emitNoteOff. Not the same as keyDown: the arp sounds notes
    // nobody pressed, and the pass-through sounds the ones they did.
    bool outSounding[128] {};

   #if DOMINA_TRACE
    // Every note Domina emits, captured on the audio thread into a lock-free
    // ring and drained to the log by the editor's timer. A stuck note is an
    // "on" with no matching "off", and this is the only way to SEE that rather
    // than infer it from what it sounds like.
    struct NoteEvent { int block; short sample; unsigned char note, on, src; };

    static constexpr int kTraceSize = 8192;

    NoteEvent        traceRing[kTraceSize] {};
    std::atomic<int> traceHead { 0 };
    int              traceTail = 0;
    int              blockCounter = 0;
    int              quietDrains  = 0;

    void pushNoteTrace (int sample, int note, bool on, int src) noexcept;

    // The block's clock, published every block for the trace. The note events
    // showed the beat window frozen; these say WHICH value froze.
    //
    // SEPARATE atomics, not one atomic struct: a struct this size is not
    // lock-free, so std::atomic<BlockState> would take a mutex on the audio
    // thread. Individual doubles are lock-free on x64. The fields can be a
    // block out of step with each other, which for a diagnostic is nothing.
    std::atomic<double> stBlockStart { 0.0 }, stBlockBeats { 0.0 }, stBpm { 0.0 };
    std::atomic<double> stLoopStart  { 0.0 }, stLoopEnd    { 0.0 };
    std::atomic<double> stPlayPos    { 0.0 }, stHostPpq    { 0.0 };
    std::atomic<int>    stPlaying    { 0 },   stSync       { 0 }, stSamples { 0 };

   public:
    // Message thread only. Returns "" when there is nothing new.
    juce::String drainNoteTrace();
    juce::String stateLine();
   private:
   #endif

    // MUTE ARP passes the played notes straight through instead of arpeggiating.
    // Flipping it has to hand the held chord from one path to the other, so the
    // previous state is remembered to catch the edge.
    int    lastMuteArp     = -1;
    int    lastHold        = -1;

    double curSampleRate   = 44100.0;
    double freeBeat        = 0.0;       // internal clock when not following the host
    double expectedNextPpq = -1.0e9;    // transport-jump detection
    double blockStartBeat  = 0.0;
    double beatsPerSample  = 0.0;

    // A host block can reach processBlock in several pieces (see the note in
    // processBlock), all of them reporting the same transport position. These
    // remember what the host last SAID so a repeat can be told apart from the
    // transport genuinely standing still.
    double lastHostPpq     = 0.0;
    bool   haveHostPpq     = false;

    // The furthest beat already turned into notes. Nothing may be rendered
    // twice: see the monotonic guard in processBlock.
    double lastRenderedBeat = 0.0;
    bool   haveRendered     = false;

    // Samples elapsed since the arp last had anything sounding. Drives the
    // CONTINUOUS-off restart; counted in samples so it does not drift.
    juce::int64 idleSamples = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DominaAudioProcessor)
};



