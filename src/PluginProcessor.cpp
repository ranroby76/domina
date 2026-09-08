
// ============================================================================
//  C:\workspace\Domina\src\PluginProcessor.cpp
//  Domina - seeded MIDI arpeggiator (Fanan)
// ============================================================================

#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "DebugTrace.h"

#include <cstring>

// ---------------------------------------------------------------------------
// Parameters
// ---------------------------------------------------------------------------
juce::AudioProcessorValueTreeState::ParameterLayout DominaAudioProcessor::createParameterLayout()
{
    using namespace juce;
    std::vector<std::unique_ptr<RangedAudioParameter>> p;

    // ---- arpeggiator --------------------------------------------------------
    // There is no MODE. Up / Down / Up-Down were a weaker version of what the
    // seeded engine already does, and OFF is what MUTE OUT is for.
    //
    // Every continuous control below reads 0-100 in 101 steps. The generator
    // wants 0..1 and refreshArpParams divides; nobody should have to read 0.35
    // off a knob. BARS and OCTAVES stay as counts - they are quantities, not
    // amounts, and "BARS 47" would mean nothing.
    p.push_back (std::make_unique<AudioParameterInt> (
        ParameterID { "arpSeed", 1 }, "Seed", 0, 999999, 973));

    p.push_back (std::make_unique<AudioParameterInt> (
        ParameterID { "arpBars", 1 }, "Bars", 1, 8, 4));

    p.push_back (std::make_unique<AudioParameterInt> (
        ParameterID { "arpOctaves", 1 }, "Octaves", 1, 4, 1));

    p.push_back (std::make_unique<AudioParameterFloat> (
        ParameterID { "arpComplexity", 1 }, "Complexity",
        NormalisableRange<float> (0.0f, 100.0f, 1.0f), 80.0f));

    // The idiom list comes from the core, so adding one there is the whole
    // change - nothing here has to be kept in step by hand.
    {
        StringArray idioms;
        for (int i = 0; i < fanan::idiomCount(); ++i)
            idioms.add (fanan::idiomName (i));

        p.push_back (std::make_unique<AudioParameterChoice> (
            ParameterID { "arpIdiom", 1 }, "Idiom", idioms, 0));
    }

    // ORIGINALITY: whose skeleton. 0 = the knobs and the seed decide everything
    // and the idiom is not consulted; 100 = the pattern is reduced to the
    // idiom's own skeleton whatever the other controls say. COMPLEXITY above is
    // the other axis - how much the generator elaborates whatever it was handed.
    p.push_back (std::make_unique<AudioParameterFloat> (
        ParameterID { "arpOriginality", 1 }, "Originality",
        NormalisableRange<float> (0.0f, 100.0f, 1.0f), 75.0f));

    p.push_back (std::make_unique<AudioParameterFloat> (
        ParameterID { "arpW16", 1 }, "1/16 Weight",
        NormalisableRange<float> (0.0f, 100.0f, 1.0f), 100.0f));

    p.push_back (std::make_unique<AudioParameterFloat> (
        ParameterID { "arpW8", 1 }, "1/8 Weight",
        NormalisableRange<float> (0.0f, 100.0f, 1.0f), 100.0f));

    p.push_back (std::make_unique<AudioParameterFloat> (
        ParameterID { "arpW4", 1 }, "1/4 Weight",
        NormalisableRange<float> (0.0f, 100.0f, 1.0f), 35.0f));

    p.push_back (std::make_unique<AudioParameterFloat> (
        ParameterID { "arpRest", 1 }, "Rests",
        NormalisableRange<float> (0.0f, 100.0f, 1.0f), 12.0f));

    p.push_back (std::make_unique<AudioParameterFloat> (
        ParameterID { "arpGateMin", 1 }, "Gate Min",
        NormalisableRange<float> (0.0f, 100.0f, 1.0f), 45.0f));

    p.push_back (std::make_unique<AudioParameterFloat> (
        ParameterID { "arpGateMax", 1 }, "Gate Max",
        NormalisableRange<float> (0.0f, 100.0f, 1.0f), 90.0f));

    p.push_back (std::make_unique<AudioParameterBool> (
        ParameterID { "arpSort", 1 }, "Sort Notes", true));

    // ON is the behaviour Domina has always had: the pattern is anchored by the
    // first chord and every chord after it joins the phase already running.
    // OFF makes a long enough silence forget that anchor - see processBlock.
    p.push_back (std::make_unique<AudioParameterBool> (
        ParameterID { "arpContinuous", 1 }, "Continuous", true));

    p.push_back (std::make_unique<AudioParameterFloat> (
        ParameterID { "arpTempo", 1 }, "Free Tempo",
        NormalisableRange<float> (40.0f, 240.0f, 0.1f), 120.0f));

    p.push_back (std::make_unique<AudioParameterBool> (
        ParameterID { "arpSync", 1 }, "Sync to DAW", true));

    // ---- locators -----------------------------------------------------------
    p.push_back (std::make_unique<AudioParameterChoice> (
        ParameterID { "locSnap", 1 }, "Locator Snap",
        StringArray { "Bar", "Beat", "1/2 Beat", "1/4 Beat", "1/8 Beat" }, 1));

    p.push_back (std::make_unique<AudioParameterFloat> (
        ParameterID { "loopStart", 1 }, "Loop Start",
        NormalisableRange<float> (0.0f, 128.0f, 0.25f), 0.0f));

    p.push_back (std::make_unique<AudioParameterFloat> (
        ParameterID { "loopEnd", 1 }, "Loop End",
        NormalisableRange<float> (0.0f, 128.0f, 0.25f), 16.0f));

    // ---- MIDI output --------------------------------------------------------
    p.push_back (std::make_unique<AudioParameterBool> (
        ParameterID { "mute", 1 }, "Mute Output", false));

    // MUTE ARP silences the arpeggiator, not the plugin: what you play passes
    // straight out. MUTE OUT silences everything.
    p.push_back (std::make_unique<AudioParameterBool> (
        ParameterID { "muteArp", 1 }, "Mute Arp", false));

    // HOLD locks the sequence to the bar that is playing, as though the
    // locators had closed around it. Released, the pattern carries on.
    p.push_back (std::make_unique<AudioParameterBool> (
        ParameterID { "arpHold", 1 }, "Hold Bar", false));

    p.push_back (std::make_unique<AudioParameterInt> (
        ParameterID { "outChannel", 1 }, "Output Channel", 1, 16, 1));

    p.push_back (std::make_unique<AudioParameterFloat> (
        ParameterID { "velScale", 1 }, "Velocity",
        NormalisableRange<float> (10.0f, 200.0f, 1.0f), 100.0f));

    // HUMANISE. The three move together: the toggle arms it, and the two
    // amounts are how far BELOW and ABOVE the VELOCITY knob a note may land.
    // Asymmetric on purpose - a player's stray notes are usually quieter than
    // the ones they meant, not louder.
    p.push_back (std::make_unique<AudioParameterBool> (
        ParameterID { "velRandom", 1 }, "Random Velocity", false));

    p.push_back (std::make_unique<AudioParameterInt> (
        ParameterID { "velRandDown", 1 }, "Random Vel Down", 0, 50, 6));

    p.push_back (std::make_unique<AudioParameterInt> (
        ParameterID { "velRandUp", 1 }, "Random Vel Up", 0, 50, 8));

    return { p.begin(), p.end() };
}

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------
DominaAudioProcessor::DominaAudioProcessor()
   #if JucePlugin_IsMidiEffect
    : AudioProcessor (BusesProperties()),          // a MIDI effect has no audio buses
   #else
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
   #endif
      apvts (*this, nullptr, "PARAMS", createParameterLayout())
{
    DominaTrace::installCrashHandler();
    DOMINA_LOG ("#" + juce::String (traceId) + " processor constructed");

    pSeed       = apvts.getRawParameterValue ("arpSeed");
    pBars       = apvts.getRawParameterValue ("arpBars");
    pOctaves    = apvts.getRawParameterValue ("arpOctaves");
    pComplexity = apvts.getRawParameterValue ("arpComplexity");
    pW16        = apvts.getRawParameterValue ("arpW16");
    pW8         = apvts.getRawParameterValue ("arpW8");
    pW4         = apvts.getRawParameterValue ("arpW4");
    pRest       = apvts.getRawParameterValue ("arpRest");
    pGateMin    = apvts.getRawParameterValue ("arpGateMin");
    pGateMax    = apvts.getRawParameterValue ("arpGateMax");
    pSort       = apvts.getRawParameterValue ("arpSort");
    pContinuous = apvts.getRawParameterValue ("arpContinuous");
    pIdiom      = apvts.getRawParameterValue ("arpIdiom");
    pOriginality = apvts.getRawParameterValue ("arpOriginality");
    pMuteArp    = apvts.getRawParameterValue ("muteArp");
    pHold       = apvts.getRawParameterValue ("arpHold");
    pTempo      = apvts.getRawParameterValue ("arpTempo");
    pSnap       = apvts.getRawParameterValue ("locSnap");
    pLoopStart  = apvts.getRawParameterValue ("loopStart");
    pLoopEnd    = apvts.getRawParameterValue ("loopEnd");
    pSync       = apvts.getRawParameterValue ("arpSync");
    pMute       = apvts.getRawParameterValue ("mute");
    pOutChannel = apvts.getRawParameterValue ("outChannel");
    pVelScale   = apvts.getRawParameterValue ("velScale");
    pVelRandom  = apvts.getRawParameterValue ("velRandom");
    pVelRandDn  = apvts.getRawParameterValue ("velRandDown");
    pVelRandUp  = apvts.getRawParameterValue ("velRandUp");

    refreshArpParams();
    arp.rebakeIfNeeded();
    patternPub.publish (arp.getPattern());
    arp.fillChordSnapshot (lastChord);
    chordPub.publish (lastChord);
}

float DominaAudioProcessor::getSnapBeats() const
{
    const int choice = (int) pSnap->load();
    const float bpb  = juce::jmax (1.0f, arp.params.beatsPerBar);

    // The list is in BEAT units, so it keeps halving.
    switch (choice)
    {
        case 0:  return bpb;        // bar
        case 1:  return 1.0f;       // beat
        case 2:  return 0.5f;
        case 3:  return 0.25f;
        default: return 0.125f;
    }
}

// ---------------------------------------------------------------------------
// Prepare / layout / state
// ---------------------------------------------------------------------------
DominaAudioProcessor::~DominaAudioProcessor()
{
    DOMINA_LOG ("#" + juce::String (traceId) + " processor destroyed");
}

void DominaAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    DOMINA_LOG ("#" + juce::String (traceId) + " prepareToPlay  rate=" + juce::String (sampleRate)
                  + "  block=" + juce::String (samplesPerBlock));

    curSampleRate = sampleRate;

    arpEvents.reserve (1024);
    pendingOffs.reserve (256);
    segmentNotes.reserve (2048);
    noteOnSamples.reserve (512);

    outMidi.ensureSize (16384);
    inMidi.ensureSize (16384);

    keyboardState.reset();

    arp.reset();
    pendingOffs.clear();
    for (int n = 0; n < 128; ++n) { keyDown[n] = false; keyVel[n] = 0.0f; }

    freeBeat        = 0.0;
    expectedNextPpq = -1.0e9;
    lastMuteArp     = -1;
    lastHold        = -1;
    lastHostPpq      = 0.0;
    haveHostPpq      = false;
    lastRenderedBeat = 0.0;
    haveRendered     = false;

    for (auto& s : outSounding)
        s = false;
    idleSamples     = 0;

   #if DOMINA_CLAP
    for (auto& ch : clapNoteIds)
        for (auto& id : ch)
            id = -1;

    clapNextNoteId = 0;
   #endif

    juce::ignoreUnused (samplesPerBlock);
}

bool DominaAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
   #if JucePlugin_IsMidiEffect
    juce::ignoreUnused (layouts);
    return true;
   #else
    const auto out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();
   #endif
}

// EVERYTHING that describes a Domina sound, in one tree. The project state and
// a .dompatch file are both this.
juce::ValueTree DominaAudioProcessor::captureState()
{
    auto state = apvts.copyState();
    state.setProperty ("midiLearn",  midiLearn.toString(), nullptr);
    state.setProperty ("dominaVer",  JucePlugin_VersionString, nullptr);
    return state;
}

void DominaAudioProcessor::applyState (const juce::ValueTree& tree)
{
    if (! tree.isValid() || tree.getType() != apvts.state.getType())
        return;

    apvts.replaceState (tree);
    midiLearn.fromString (tree.getProperty ("midiLearn", juce::String()).toString());
}

bool DominaAudioProcessor::savePatch (const juce::File& file)
{
    auto state = captureState();
    state.setProperty ("patchName", file.getFileNameWithoutExtension(), nullptr);

    // remember it as the active patch, so the project keeps the name too
    apvts.state.setProperty ("patchName", file.getFileNameWithoutExtension(), nullptr);

    auto inner = state.createXml();
    if (inner == nullptr)
        return false;

    juce::XmlElement root (DominaPatch::rootTag());
    root.setAttribute ("format", 1);
    root.addChildElement (inner.release());

    return root.writeTo (DominaPatch::withExtension (file));
}

bool DominaAudioProcessor::loadPatch (const juce::File& file)
{
    auto root = juce::XmlDocument::parse (file);

    if (root == nullptr)
        return false;

    // Accept the wrapper OR a bare state tree, so a patch pulled out of a
    // project file by hand still loads.
    auto* inner = root->hasTagName (DominaPatch::rootTag())
                    ? root->getFirstChildElement()
                    : root.get();

    if (inner == nullptr)
        return false;

    const auto tree = juce::ValueTree::fromXml (*inner);
    if (! tree.isValid())
        return false;

    applyState (tree);
    apvts.state.setProperty ("patchName", file.getFileNameWithoutExtension(), nullptr);
    return true;
}

void DominaAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    DOMINA_LOG ("#" + juce::String (traceId) + " getStateInformation");

    if (auto xml = captureState().createXml())
        copyXmlToBinary (*xml, destData);
}

void DominaAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    DOMINA_LOG ("#" + juce::String (traceId) + " setStateInformation  bytes=" + juce::String (sizeInBytes));

    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType()))
        return;

    applyState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessorEditor* DominaAudioProcessor::createEditor()
{
    DOMINA_LOG ("#" + juce::String (traceId) + " createEditor");
    return new DominaAudioProcessorEditor (*this);
}

// ---------------------------------------------------------------------------
// Arp parameter refresh + timing helpers
// ---------------------------------------------------------------------------
void DominaAudioProcessor::refreshArpParams()
{
    arp.params.seed           = (uint32_t) juce::jlimit (0, 999999, (int) pSeed->load());
    arp.params.patternBars    = (int) pBars->load();
    arp.params.octaveRange    = (int) pOctaves->load();
    arp.params.complexity     = pComplexity->load() * 0.01f;
    arp.params.idiom          = (int) pIdiom->load();
    arp.params.originality    = juce::jlimit (0.0f, 1.0f, pOriginality->load() * 0.01f);
    arp.params.weight16       = pW16->load() * 0.01f;
    arp.params.weight8        = pW8->load() * 0.01f;
    arp.params.weight4        = pW4->load() * 0.01f;
    arp.params.restProb       = pRest->load() * 0.01f;
    arp.params.gateMin        = pGateMin->load() * 0.01f;
    arp.params.gateMax        = pGateMax->load() * 0.01f;
    arp.params.sortNotes      = pSort->load() > 0.5f;
    arp.params.loopStartBeats = pLoopStart->load();
    arp.params.loopEndBeats   = pLoopEnd->load();

    // The locators are the loop, and the only loop.
    arp.params.restart = fanan::RestartMode::Free;
}

double DominaAudioProcessor::beatAtSample (int sample) const
{
    return blockStartBeat + sample * beatsPerSample;
}

int DominaAudioProcessor::sampleForBeat (double beat, int loSample, int hiSample) const
{
    const int s = (int) juce::roundToIntAccurate ((beat - blockStartBeat) / beatsPerSample);
    return juce::jlimit (loSample, juce::jmax (loSample, hiSample - 1), s);
}

// Output channel and velocity scaling are applied at the point of emission, so
// nothing upstream has to know about them.
void DominaAudioProcessor::emitNoteOn (int sample, int note, float velocity, int src)
{
    if (pMute->load() > 0.5f)
        return;

    const int ch  = juce::jlimit (1, 16, (int) pOutChannel->load());
    float scale = juce::jlimit (10.0f, 200.0f, pVelScale->load());

    // Humanise around the knob, not around the note: an offset in the same
    // units the knob shows, so "76 with -6/+8" really does mean 70 to 84.
    // Drawn per note, so a repeated pitch never lands at the same weight twice.
    if (pVelRandom->load() > 0.5f)
    {
        const int dn = (int) pVelRandDn->load();
        const int up = (int) pVelRandUp->load();

        if (dn > 0 || up > 0)
            scale = juce::jlimit (1.0f, 200.0f,
                                  scale + (float) velRng.nextInt ({ -dn, up + 1 }));
    }

    const float s = scale * 0.01f;
    const int v   = juce::jlimit (1, 127, juce::roundToInt (velocity * s * 127.0f));

    // NEVER STACK A PITCH ON ITSELF. Whatever upstream decides to re-trigger a
    // note already sounding, the instrument downstream must get a clean off
    // first - two copies of one pitch a few milliseconds apart comb-filter, and
    // that is heard as detuning rather than as a repeat.
    const int safeNote = juce::jlimit (0, 127, note);

    if (outSounding[safeNote])
        outMidi.addEvent (juce::MidiMessage::noteOff (ch, safeNote), sample);

    outMidi.addEvent (juce::MidiMessage::noteOn (ch, note, (juce::uint8) v), sample);
    outSounding[safeNote] = true;

   #if DOMINA_TRACE
    pushNoteTrace (sample, note, true, src);
   #endif
}

// Note-offs are NOT muted: muting mid-phrase must not leave a note hanging in
// whatever instrument is downstream.
void DominaAudioProcessor::emitNoteOff (int sample, int note, int src)
{
    const int ch = juce::jlimit (1, 16, (int) pOutChannel->load());
    outMidi.addEvent (juce::MidiMessage::noteOff (ch, note), sample);
    outSounding[juce::jlimit (0, 127, note)] = false;

   #if DOMINA_TRACE
    pushNoteTrace (sample, note, false, src);
   #endif
}

#if DOMINA_TRACE
void DominaAudioProcessor::pushNoteTrace (int sample, int note, bool on, int src) noexcept
{
    const int h = traceHead.load (std::memory_order_relaxed);

    traceRing[h % kTraceSize] = { blockCounter,
                                  (short) juce::jlimit (-1, 32000, sample),
                                  (unsigned char) juce::jlimit (0, 127, note),
                                  (unsigned char) (on ? 1 : 0),
                                  (unsigned char) src };

    traceHead.store (h + 1, std::memory_order_release);
}

juce::String DominaAudioProcessor::drainNoteTrace()
{
    const int head = traceHead.load (std::memory_order_acquire);

    // A state line even when NOTHING played. Twelve seconds of silence looked
    // like twelve seconds of missing log, which hid the bug rather than showing
    // it - one line a second says "still alive, here is the clock, no notes".
    if (head == traceTail)
    {
        if (++quietDrains < 20)
            return {};

        quietDrains = 0;
        juce::String idle;
        idle << "  (no notes)" << juce::newLine;
        return idle + stateLine();
    }

    quietDrains = 0;

    // If the audio thread lapped us, skip what was overwritten and say so.
    juce::String out;
    if (head - traceTail > kTraceSize)
    {
        out << "  [trace overflow, " << (head - traceTail - kTraceSize) << " lost]" << juce::newLine;
        traceTail = head - kTraceSize;
    }

    static const char* srcName[] = { "arp", "thru", "pend", "hush", "panic", "rearm" };

    while (traceTail < head)
    {
        const auto& e = traceRing[traceTail % kTraceSize];
        out << "  blk " << e.block
            << "  smp " << (int) e.sample
            << (e.on ? "  ON  " : "  off ") << (int) e.note
            << "  " << (e.src < 6 ? srcName[e.src] : "?")
            << juce::newLine;
        ++traceTail;
    }

    out << stateLine();
    return out;
}

juce::String DominaAudioProcessor::stateLine()
{
    juce::String out;
    const auto r = std::memory_order_relaxed;

    int live = 0;
    for (int n = 0; n < 128; ++n)
        if (outSounding[n])
            ++live;

    out << "  -- sounding " << live
        << "  start "  << juce::String (stBlockStart.load (r), 4)
        << "  beats "  << juce::String (stBlockBeats.load (r), 6)
        << "  bpm "    << juce::String (stBpm.load (r), 2)
        << "  loop "   << juce::String (stLoopStart.load (r), 3)
        << ".."        << juce::String (stLoopEnd.load (r), 3)
        << "  pos "    << juce::String (stPlayPos.load (r), 3)
        << "  ppq "    << juce::String (stHostPpq.load (r), 4)
        << "  play "   << stPlaying.load (r)
        << " sync "    << stSync.load (r)
        << " smp "     << stSamples.load (r)
        << juce::newLine;
    return out;
}
#endif

// BELT AND BRACES, deliberately.
//
// A stuck note IS our bookkeeping being wrong, so silencing that depends only
// on the bookkeeping cannot be trusted to fix it. Three layers, cheapest first:
//
//   1. explicit note-offs for what we believe is sounding - targeted, and the
//      only layer with no side effect on the instrument downstream
//   2. CC 123 All Notes Off - reaches anything we have lost track of, though
//      not every instrument implements it
//   3. CC 120 All Sound Off - the forceful one, cuts releases too
//
// The full 0-127 sweep is NOT here. It belongs on an explicit panic, not on
// every mute toggle: 128 note-offs per click is a lot of traffic and some
// instruments stumble audibly on it.
void DominaAudioProcessor::silenceEverything (int atSample)
{
    for (int n = 0; n < 128; ++n)
        if (outSounding[n])
            emitNoteOff (atSample, n, 3);

    const int ch = juce::jlimit (1, 16, (int) pOutChannel->load());
    outMidi.addEvent (juce::MidiMessage::allNotesOff (ch), atSample);
    outMidi.addEvent (juce::MidiMessage::allSoundOff  (ch), atSample);
}

// The version that cannot be ignored: a note-off on every pitch, whatever
// anyone believes is sounding. For PANIC and for an inbound all-notes-off,
// where being heavy-handed is the whole point.
void DominaAudioProcessor::silenceBruteForce (int atSample)
{
    silenceEverything (atSample);

    const int ch = juce::jlimit (1, 16, (int) pOutChannel->load());

    for (int n = 0; n < 128; ++n)
        outMidi.addEvent (juce::MidiMessage::noteOff (ch, n), atSample);

    for (auto& s : outSounding)
        s = false;
}

// Handing the held chord between the arp and the direct path.
//
// SILENCE FIRST, ALWAYS. Flushing the arp's pending note-offs is not enough,
// because it only knows about the arp's own notes. Un-muting used to leave the
// PASS-THROUGH notes sounding with nothing left to ever turn them off - the arp
// would then start playing on top of a chord that never released, which is the
// legato drone. It has to be everything Domina has sounding, by either path.
void DominaAudioProcessor::handleArpMuteTransition (bool nowMuted, int atSample, double atBeat)
{
    flushAllPendingOffs (atSample);
    silenceEverything (atSample);
    arp.reset();
    arp.params.chordGatherBeats = 0.0f;   // this chord is already fully known

    for (int n = 0; n < 128; ++n)
    {
        if (! keyDown[n])
            continue;

        if (nowMuted)
            emitNoteOn (atSample, n, keyVel[n], 5);
        else
            arp.noteOn (n, keyVel[n], atBeat);
    }
}

void DominaAudioProcessor::flushAllPendingOffs (int atSample)
{
    for (const auto& off : pendingOffs)
        emitNoteOff (atSample, off.note, 2);

    pendingOffs.clear();
}

// How long to wait before starting the pattern for a chord beginning at
// atSample. The window only exists to catch the rest of a chord whose keys
// arrive a few milliseconds apart; whenever we can prove the chord is already
// complete, the answer is zero and the arp starts exactly on time.
double DominaAudioProcessor::gatherBeatsForChordAt (int atSample, int numSamples,
                                                    int gatherSamples) const
{
    int sameSample = 0;
    int lastInWindow = atSample;

    for (const int s : noteOnSamples)
    {
        if (s == atSample)
            ++sameSample;
        else if (s > atSample && s <= atSample + gatherSamples)
            lastInWindow = juce::jmax (lastInWindow, s);
    }

    if (sameSample >= 2 && lastInWindow == atSample)
        return 0.0;

    if (lastInWindow == atSample && atSample + gatherSamples <= numSamples)
        return 0.0;

    return gatherSamples * beatsPerSample;
}

void DominaAudioProcessor::renderSegment (double segStartBeat, double segEndBeat,
                                          int segStartSample, int segEndSample)
{
    if (segEndSample <= segStartSample)
        return;

    segmentNotes.clear();

    for (size_t i = 0; i < pendingOffs.size();)
    {
        if (pendingOffs[i].beat < segEndBeat)
        {
            segmentNotes.push_back ({ sampleForBeat (pendingOffs[i].beat, segStartSample, segEndSample),
                                      false, pendingOffs[i].note, 0.0f });
            pendingOffs[i] = pendingOffs.back();
            pendingOffs.pop_back();
        }
        else
        {
            ++i;
        }
    }

    if (pMuteArp == nullptr || pMuteArp->load() < 0.5f)
    {
        arpEvents.clear();
        arp.process (segStartBeat, segEndBeat, arpEvents);

        for (const auto& ev : arpEvents)
        {
            segmentNotes.push_back ({ sampleForBeat (ev.startBeat, segStartSample, segEndSample),
                                      true, ev.note, ev.velocity });

            const double offBeat = ev.startBeat + ev.lengthBeats;
            if (offBeat < segEndBeat)
                segmentNotes.push_back ({ sampleForBeat (offBeat, segStartSample, segEndSample),
                                          false, ev.note, 0.0f });
            else if (pendingOffs.size() < pendingOffs.capacity())
                pendingOffs.push_back ({ ev.note, offBeat });
        }
    }

    // same-sample ordering: note-offs before note-ons, or a repeated pitch
    // silences itself the instant it restarts
    std::sort (segmentNotes.begin(), segmentNotes.end(),
               [] (const TimedNote& a, const TimedNote& b)
               {
                   if (a.sample != b.sample) return a.sample < b.sample;
                   return (! a.isOn) && b.isOn;
               });

    for (const auto& t : segmentNotes)
    {
        if (t.isOn) emitNoteOn  (t.sample, t.note, t.vel);
        else        emitNoteOff (t.sample, t.note);
    }
}

// ---------------------------------------------------------------------------
// processBlock
// ---------------------------------------------------------------------------

// How long a silence has to last before CONTINUOUS-off restarts the pattern.
static constexpr double kIdleRestartSeconds = 2.0;

void DominaAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                         juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    buffer.clear();

   #if DOMINA_TRACE
    ++blockCounter;
   #endif

    keyboardState.processNextMidiBuffer (midiMessages, 0, numSamples, true);

    // The host buffer is both input and output, so take a copy of the input and
    // build the output separately rather than writing into what we are reading.
    inMidi.clear();
    inMidi.addEvents (midiMessages, 0, numSamples, 0);
    midiMessages.clear();
    outMidi.clear();

    if (panicPending.exchange (false, std::memory_order_relaxed))
    {
        for (int n = 0; n < 128; ++n) keyDown[n] = false;
        arp.reset();
        flushAllPendingOffs (0);
        silenceBruteForce (0);
    }

    refreshArpParams();

    // ---- tempo & transport --------------------------------------------------
    double hostBpm      = 120.0;
    bool   hostBpmValid = false;
    double hostPpq = -1.0;
    double bpb     = 4.0;
    bool   playing = false;

    if (auto* ph = getPlayHead())
    {
        if (auto pos = ph->getPosition())
        {
            if (auto b = pos->getBpm())
            {
                if (*b > 1.0)
                {
                    hostBpm      = *b;
                    hostBpmValid = true;
                }
            }

            if (auto ts = pos->getTimeSignature()) bpb = ts->numerator * (4.0 / juce::jmax (1, ts->denominator));
            if (auto q  = pos->getPpqPosition())   hostPpq = *q;
            playing = pos->getIsPlaying();
        }
    }

    // Sync only follows the host when the host actually supplies a tempo;
    // otherwise FREE BPM takes over rather than sitting silently at a default.
    const bool sync = pSync->load() > 0.5f && hostBpmValid;

    double bpm = sync ? hostBpm : (double) pTempo->load();
    if (bpm < 1.0)
        bpm = 120.0;

    arp.params.beatsPerBar = (float) juce::jmax (1.0, bpb);
    hostBeatsPerBar.store (arp.params.beatsPerBar, std::memory_order_relaxed);
    beatsPerSample         = bpm / 60.0 / curSampleRate;
    // Sanity ceiling. If the host ever reports a wild tempo or an enormous
    // block, an unbounded beat span makes the arp empty a whole pattern into a
    // single buffer - the same audible failure by another route. Sixteen beats
    // is far more than any real block.
    const double blockBeats = juce::jlimit (0.0, 16.0, beatsPerSample * numSamples);

    bool jumped = false;

    if (arp.rebakeIfNeeded())
        patternPub.publish (arp.getPattern());

    if (playing && hostPpq > -1.0e8 && sync)
    {
        // ONE HOST BLOCK IS NOT ALWAYS ONE processBlock. The CLAP wrapper splits
        // a block at every incoming MIDI event and calls processBlock once per
        // piece - but it reads the transport once, before the split, so every
        // piece is handed the SAME ppq. Taking that at face value re-renders the
        // same beats for each piece, and because expectedNextPpq has already
        // moved on by then, the jump test below fires and re-anchors the pattern
        // on every piece. The audible result is the arp restarting a few hundred
        // times a second: a dense sustained cluster instead of a sequence, which
        // is exactly what a held chord looks like. VST3 never showed it because
        // its wrapper calls processBlock once per block with a ppq that moves.
        //
        // So the host position is honoured only when it actually MOVES. Within a
        // split block we carry on from where the previous piece ended.
        const bool hostMoved = (! haveHostPpq) || std::abs (hostPpq - lastHostPpq) > 1.0e-9;

        // TWO CLOCKS MUST NOT DRIVE ONE SPAN.
        //
        // This used to start every block at hostPpq and end it at hostPpq plus
        // OUR OWN blockBeats. The host decided where the block began, we decided
        // how far it ran. Whenever the host's position advanced by even slightly
        // more or less than our estimate, consecutive blocks overlapped or left
        // a gap - and an overlap emits the same notes a second time, with their
        // note-offs landing on the wrong copies. That is the legato smear, and
        // it can only happen here: free-running has no hostPpq, so one clock
        // drives everything and the spans always meet exactly.
        //
        // So the host position is used to DETECT a jump, not to set the start.
        // Between jumps the timeline is ours and is continuous by construction.
        if (hostMoved)
        {
            haveHostPpq = true;
            lastHostPpq = hostPpq;

            // A real move: DAW loop, rewind, locate, or the very first block.
            // Drop stale note-offs and re-anchor, or the pattern carries on from
            // the old anchor and lands on an arbitrary bar.
            if (! haveRendered || std::abs (hostPpq - expectedNextPpq) > 0.25)
            {
                flushAllPendingOffs (0);
                arp.reanchor (hostPpq);
                jumped = true;

                blockStartBeat = hostPpq;      // snap, once
            }
            else
            {
                blockStartBeat = expectedNextPpq;   // carry on, do not snap
            }
        }
        else
        {
            blockStartBeat = expectedNextPpq;
        }
    }
    else
    {
        blockStartBeat = freeBeat;
    }

    // NOTHING IS EVER RENDERED TWICE.
    //
    // Around transport start and stop a host can report a position that repeats
    // or steps slightly BACKWARDS - by less than the quarter beat the jump test
    // looks for, so no re-anchor happens and the block simply overlaps the one
    // before it. That slice of the pattern is then emitted a SECOND time: the
    // same notes stacked on themselves, with note-offs landing on the wrong
    // copies. It sounds like the arp doubling or tripling in speed and smearing
    // into legato, and it is intermittent because it depends on how the host
    // happens to jitter.
    //
    // A deliberate jump - a loop, a rewind, a locate - is exempt. That is the
    // one case where going backwards is real, and it has already re-anchored.
    if (jumped || ! haveRendered)
        haveRendered = true;
    else if (blockStartBeat < lastRenderedBeat)
        blockStartBeat = lastRenderedBeat;

    lastRenderedBeat = blockStartBeat + blockBeats;
    expectedNextPpq  = lastRenderedBeat;
    freeBeat         = lastRenderedBeat;

   #if DOMINA_TRACE
    {
        double ls = 0.0, le = 0.0;
        arp.getLoopBounds (ls, le);

        const auto r = std::memory_order_relaxed;
        stBlockStart.store (blockStartBeat, r);
        stBlockBeats.store (blockBeats,     r);
        stBpm       .store (bpm,            r);
        stLoopStart .store (ls,             r);
        stLoopEnd   .store (le,             r);
        stPlayPos   .store ((double) arp.getPlayPosition(), r);
        stHostPpq   .store (hostPpq,        r);
        stPlaying   .store (playing ? 1 : 0, r);
        stSync      .store (sync    ? 1 : 0, r);
        stSamples   .store (numSamples,      r);
    }
   #endif

    // ---- MUTE ARP and HOLD --------------------------------------------------
    const bool muteArp = pMuteArp->load() > 0.5f;
    const bool holdBar = pHold->load()    > 0.5f;

    if (lastMuteArp != (int) muteArp)
    {
        const int prev = lastMuteArp;
        lastMuteArp = (int) muteArp;
        if (prev != -1)
            handleArpMuteTransition (muteArp, 0, blockStartBeat);
    }

    if (lastHold != (int) holdBar)
    {
        lastHold = (int) holdBar;
        arp.setHoldBar (holdBar, bpb, blockStartBeat);
    }

    // ---- idle restart -------------------------------------------------------
    // CONTINUOUS off: once nothing has sounded for kIdleRestartSeconds, the next
    // chord should begin the pattern rather than join whatever the free-running
    // clock has reached. RestartMode::Free anchors on the FIRST chord only, so
    // dropping that anchor is the whole mechanism - the next noteOn sets a new
    // origin by itself. Done before the incoming events are walked, so a chord
    // arriving in this very block already sees the cleared anchor.
    {
        const bool sounding = arp.isRunning() || ! pendingOffs.empty();

        // SYNC TO DAW WINS. While the transport is running and we are following
        // it, the song position decides where the pattern is - that is the whole
        // meaning of syncing. Restarting after a silence there would throw away
        // the alignment: play 1-bar chords with a gap between them and, since a
        // bar at 120bpm IS two seconds, every chord would land back on pattern
        // bar 1 and bars 3 and 4 would never be heard. So CONTINUOUS governs the
        // free-running case, which is the only case where "start again after a
        // gap" has anything to mean.
        const bool timelineOwned = playing && sync;

        if (sounding || timelineOwned)
        {
            idleSamples = 0;
        }
        else
        {
            const juce::int64 limit = (juce::int64) (kIdleRestartSeconds * curSampleRate);

            if (idleSamples < limit)
                idleSamples += numSamples;      // stops here, so it cannot overflow

            if (idleSamples >= limit && pContinuous->load() < 0.5f)
                arp.clearAnchor();
        }
    }

    // ---- look-ahead: every note-on position in this block -------------------
    const int gatherSamples = juce::jmax (1, (int) std::ceil (0.020 * curSampleRate));
    noteOnSamples.clear();
    for (const auto meta : inMidi)
        if (meta.getMessage().isNoteOn() && noteOnSamples.size() < noteOnSamples.capacity())
            noteOnSamples.push_back (juce::jlimit (0, juce::jmax (0, numSamples - 1),
                                                   (int) meta.samplePosition));

    // ---- walk incoming MIDI, splitting the block at every event -------------
    int cursor = 0;

    for (const auto meta : inMidi)
    {
        const auto msg = meta.getMessage();
        const int  at  = juce::jlimit (0, juce::jmax (0, numSamples - 1), (int) meta.samplePosition);

        renderSegment (beatAtSample (cursor), beatAtSample (at), cursor, at);
        cursor = at;

        if (msg.isNoteOn())
        {
            const int n = msg.getNoteNumber();
            keyDown[n] = true;
            keyVel[n]  = msg.getFloatVelocity();

            if (muteArp)
            {
                emitNoteOn (at, n, keyVel[n], 1);
            }
            else
            {
                arp.params.chordGatherBeats =
                    (float) gatherBeatsForChordAt (at, numSamples, gatherSamples);
                arp.noteOn (n, keyVel[n], beatAtSample (at));
            }
        }
        else if (msg.isNoteOff())
        {
            const int n = msg.getNoteNumber();
            keyDown[n] = false;

            if (muteArp) emitNoteOff (at, n, 1);
            else         arp.noteOff (n);
        }
        else if (msg.isAllNotesOff() || msg.isAllSoundOff())
        {
            for (int n = 0; n < 128; ++n) keyDown[n] = false;
            arp.reset();
            flushAllPendingOffs (at);
            silenceBruteForce (at);      // an inbound panic must not be polite
            outMidi.addEvent (msg, at);
        }
        else
        {
            if (msg.isController())
                midiLearn.handleCc (msg.getChannel(), msg.getControllerNumber(),
                                    msg.getControllerValue());

            // Anything Domina does not consume passes straight through, so it
            // stays transparent in the middle of a MIDI chain.
            outMidi.addEvent (msg, at);
        }
    }

    renderSegment (beatAtSample (cursor), blockStartBeat + blockBeats, cursor, numSamples);

    // ---- publish GUI state --------------------------------------------------
    // Checked every block, not just on note events: OCTAVES also changes the
    // resolved pitches, and the roll must never show a different note from the
    // one being played.
    {
        fanan::ChordSnapshot cs;
        arp.fillChordSnapshot (cs);
        if (cs.size != lastChord.size
            || cs.octaves != lastChord.octaves
            || cs.semitones != lastChord.semitones
            || std::memcmp (cs.notes, lastChord.notes, sizeof (cs.notes)) != 0)
        {
            lastChord = cs;
            chordPub.publish (cs);
        }
    }

    playPosition.store (arp.getPlayPosition(), std::memory_order_relaxed);
    arpActive.store (arp.isRunning() && ! muteArp, std::memory_order_relaxed);

    midiMessages.swapWith (outMidi);
}

// ---------------------------------------------------------------------------
// CLAP note output
// ---------------------------------------------------------------------------
#if DOMINA_CLAP
void DominaAudioProcessor::addOutboundEventsToQueue (const clap_output_events* outEvents,
                                                     const juce::MidiBuffer& midiBuffer,
                                                     int sampleOffset)
{
    if (outEvents == nullptr)
        return;

    // midiBuffer is whatever processBlock left behind, i.e. Domina's output for
    // the segment the wrapper just ran. sampleOffset is the segment's start
    // within the host block, so it is ADDED to every timestamp.
    for (const auto meta : midiBuffer)
    {
        const auto msg  = meta.getMessage();
        const auto time = (uint32_t) juce::jmax (0, meta.samplePosition + sampleOffset);

        if (msg.isNoteOn() || msg.isNoteOff())
        {
            const bool on  = msg.isNoteOn();
            const int  ch  = juce::jlimit (0, 15,  msg.getChannel() - 1);
            const int  key = juce::jlimit (0, 127, msg.getNoteNumber());

            // Pair the off with the id of the on it ends. Sending -1 is legal -
            // it means "unspecified", and the host is then meant to match on
            // port, channel and key - but a host that skips that fallback never
            // ends the note at all, which sounds like a chord that sustains
            // forever and stacks voices in whatever is downstream.
            int32_t id;

            if (on)
            {
                id = clapNextNoteId++;
                clapNoteIds[ch][key] = id;
            }
            else
            {
                id = clapNoteIds[ch][key];
                clapNoteIds[ch][key] = -1;
            }

            clap_event_note ev {};
            ev.header.size     = sizeof (clap_event_note);
            ev.header.time     = time;
            ev.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
            ev.header.type     = (uint16_t) (on ? CLAP_EVENT_NOTE_ON : CLAP_EVENT_NOTE_OFF);
            ev.header.flags    = 0;

            ev.note_id    = id;
            ev.port_index = 0;
            ev.channel    = (int16_t) ch;
            ev.key        = (int16_t) key;
            ev.velocity   = (double) msg.getFloatVelocity();

            outEvents->try_push (outEvents, reinterpret_cast<const clap_event_header*> (&ev));
            continue;
        }

        // Everything Domina passes through untouched - CC, pitch bend, program
        // change - has no CLAP note-event equivalent, so it goes as raw MIDI,
        // exactly as the wrapper's own path would have sent it. Hosts that
        // ignore CLAP_EVENT_MIDI on a note port will still drop these; there is
        // nothing on the plugin side that can change that.
        const int size = msg.getRawDataSize();

        if (size == 2 || size == 3)
        {
            clap_event_midi ev {};
            ev.header.size     = sizeof (clap_event_midi);
            ev.header.time     = time;
            ev.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
            ev.header.type     = (uint16_t) CLAP_EVENT_MIDI;
            ev.header.flags    = 0;
            ev.port_index      = 0;

            std::memcpy (ev.data, msg.getRawData(), (size_t) size);
            if (size == 2)
                ev.data[2] = 0;

            outEvents->try_push (outEvents, reinterpret_cast<const clap_event_header*> (&ev));
        }
    }
}
#endif

// ---------------------------------------------------------------------------
// Factory
// ---------------------------------------------------------------------------
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new DominaAudioProcessor();
}



