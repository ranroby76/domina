
// ============================================================================
//  C:\workspace\Domina\src\SeededArpCore.h
//  Domina - musical seeded arpeggiator core
//  (c) Fanan
//
//  Pure C++17, no JUCE dependency: engine-agnostic and unit-testable.
//
//  MODEL: bake-per-cycle, not per-step streaming.
//  The whole pattern is generated up front from the seed, run through the
//  musical constraints, then played back. This is what makes constraint
//  filtering, motif repetition and the piano-roll display possible at all -
//  you cannot filter or draw a note you have already emitted.
//
//  What makes it musical (all scaled by params.complexity, 0..1):
//    1. RHYTHM BY RECURSIVE SUBDIVISION. Each beat is split in half, then in
//       half again, with probabilities derived from the 1/16-1/8-1/4 weights.
//       Every note therefore lands on a legal metric position by construction:
//       a 1/4 only on a beat, a 1/8 only on an 1/8 position. No more quarter
//       notes starting on the "a" of beat 1. A tie pass merges adjacent whole
//       beats into occasional half notes so the phrase breathes.
//    2. MOTIF REPETITION. Three source bars (A, B, C) are generated and laid
//       out as AAAA / AAAB / ABAB / AABB / ABAC. Repetition with variation is
//       what separates a riff from a random walk.
//    3. CONTOUR. Pitch is a weighted random walk over the chord ladder -
//       mostly small steps, occasional leaps, direction reversed after a big
//       leap (gap-fill), chord root anchored on downbeats - instead of a
//       uniform jump anywhere in the pool.
//    4. METRIC ACCENT. Velocity and gate follow metric strength; notes
//       leading into a rest get extended toward legato.
//    Rests are biased onto weak positions, so gaps sound intentional.
//
//  Determinism: SplitMix64 - bit-identical on MSVC / clang / gcc, Windows and
//  macOS. Three independent streams (form, rhythm, pitch) so that tweaking
//  the rhythm knobs does not scramble the melody of a seed you like.
//
//  Chord independence: notes are baked as ladder RUNGS against a nominal
//  4-note chord, and resolved to real pitches at playback against whatever is
//  held. So the rhythm never changes when you change chord, while the chord
//  still fully determines the pitches - and the piano roll stays stable.
//
//  Caller contract: if an emitted length makes a note-off coincide with the
//  next note-on of the same pitch, schedule the off before the on.
// ============================================================================

#pragma once

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

namespace fanan
{

// ---------------------------------------------------------------------------
// Deterministic, cross-platform PRNG (SplitMix64)
// ---------------------------------------------------------------------------
class SplitMix64
{
public:
    void seed (uint64_t s) noexcept { state = s; }

    uint64_t nextU64() noexcept
    {
        uint64_t z = (state += 0x9E3779B97F4A7C15ull);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31);
    }

    double nextDouble() noexcept   // uniform in [0, 1)
    {
        return double (nextU64() >> 11) * (1.0 / 9007199254740992.0);
    }

private:
    uint64_t state = 0;
};

// ---------------------------------------------------------------------------
// Public types
// ---------------------------------------------------------------------------
// What re-starts the pattern. A 4-bar pattern can only ever be heard in full
// if it is NOT restarted every time the chord changes.
enum class RestartMode : int
{
    Chord = 0,   // restart from bar 1 on every new chord (best for 1-bar patterns)
    Free,        // start on the first chord, then run continuously through all bars
    Song         // locked to the host timeline; bar 1 of the pattern = bar 1 of the song
};

// ---------------------------------------------------------------------------
// Idioms
//
// An idiom is the way a style speaks: the shape a genre keeps, held as data the
// generator is pulled toward rather than a phrase it plays back. Two parts:
//
//   ONSET   - per slot, how much that slot wants to sound, and this is where
//             nearly all the recognisability lives. The generator's own rest
//             pass biases gaps onto WEAK positions; a montuno does the opposite
//             through beats 2 and 3, sounding the "e" and the "a" and leaving
//             the beat and the "and" empty. No setting of the existing knobs
//             can ask for that, which is why the table exists.
//
//             A NEGATIVE onset means NO OPINION: the seed decides that slot on
//             its own, exactly as if there were no idiom. This is what keeps an
//             idiom a SKELETON rather than a copy. Mark only the slots that
//             carry the feel - the ones that must sound, and the ones that must
//             stay empty - and leave the rest free. The flesh is the seed's,
//             which is why two bars of the same idiom are never identical and
//             why an idiom can flavour a track without taking it over.
//             Same convention on ACCENT: negative means leave the metric
//             accent alone.
//
//   RUN     - pitch as an ascending cycle through a window of the chord ladder,
//             wrapping when it passes the top. That wrap is the octave-drop you
//             hear in a montuno. HOLD marks slots where the run repeats a rung
//             instead of advancing.
//
// ORIGINALITY (0..1) blends both against what the knobs alone would do, so an idiom
// is a bias and never a template - the seed still picks the slots the mask is
// not certain about, and no two bars come out identical.
// ---------------------------------------------------------------------------
// Longest grid an idiom may declare. 32 covers two bars of 16ths, which is as
// long as an idiom is worth having - past that it stops being a cell and starts
// being a phrase, and a phrase belongs in a sequencer.
inline constexpr int kIdiomSlots = 32;

// Ceiling on how far the loop-end locator may stretch the pattern. 16 bars is
// already a long phrase, and it keeps the worst case - a dense idiom at full
// 1/16 weight - inside PatternSnapshot's note limit.
inline constexpr int kMaxPatternBars = 16;

struct ArpIdiom
{
    const char* name;

    // The grid. SLOTS is how many steps the whole unit is divided into and BARS
    // is how many bars that unit spans, so one slot lasts (bars * beatsPerBar /
    // slots) beats and the two fields together say what kind of grid it is:
    //   16 / 1  one bar of 16ths          (salsa, funk, most straight feels)
    //   12 / 1  one bar of 8th triplets   (shuffle, swing, 6/8)
    //   24 / 1  one bar of 16th triplets
    //   32 / 2  two bars of 16ths         (bossa, clave-length cycles)
    //   24 / 2  two bars of 8th triplets
    // Anything up to kIdiomSlots works; these are just the useful ones.
    int   slots;
    int   bars;

    // The idiom's OWN beats per bar. Not the host's - that was the bug this
    // field fixes. slots used to be divided across whatever bar the project was
    // in, so every idiom here was silently written for 4/4 and a 3/4 session
    // turned a 16th grid into 3/16 of a beat. With the meter stated, a slot is
    // always a real subdivision, and an idiom is only offered when the project
    // agrees with it.
    int   meter;


    float onset [kIdiomSlots];   // 0 = keep silent, 1 = always sound
    float accent[kIdiomSlots];   // velocity multiplier
    bool  hold  [kIdiomSlots];   // repeat the previous rung here

    // Pitch. rungWindow 0 = no opinion, the seed keeps its own melody; this is
    // what a rhythm-only idiom uses. 2 or more turns on the ascending run that
    // wraps at the window edge, which is where the montuno's octave drop lives.
    int   rungWindow;            // rungs in the wrap window, 0 = rhythm only
    int   rungStep;              // rungs advanced per sounding note
};

namespace detail
{
    inline const ArpIdiom* idiomTable()
    {
        // Salsa taken from a hand-drawn montuno: a one-bar cell, every note a
        // 16th, onsets on 1 e & a | . e . a | . e . a | 4 . & . and pitch
        // cycling E-G-A-C with a hold on beat 4.
        static const ArpIdiom table[] =
        {
            {
                // What actually makes a montuno a montuno is beats 2 and 3:
                // sound the "e" and the "a", leave the beat and the "and" empty.
                // Plus a downbeat and a landing on 4. Everything else - how busy
                // beat 1 gets, what happens after the landing - is flesh, and is
                // left to the seed, so the flavour survives without the figure
                // being copied. Beat 1's "e" ghosts if the seed puts a note there.
                "Latin",
                16, 1, 4,
                //   1      e      &      a        2      e      &      a        3      e      &      a        4      e      &      a
                {  0.92f, -1.0f, -1.0f, -1.0f,   0.10f, 0.92f, 0.12f, 0.92f,   0.10f, 0.92f, 0.12f, 0.92f,   0.85f, -1.0f, -1.0f, -1.0f },
                {  1.00f, 0.45f, -1.0f, -1.0f,   -1.0f, 0.95f, -1.0f, 0.90f,   -1.0f, 0.95f, -1.0f, 0.90f,   1.00f, -1.0f, -1.0f, -1.0f },
                {  false, false, false, false,   false, false, false, false,   false, false, false, false,   true,  false, false, false },
                4, 1
            },
            {
                // Quarter, eighth, quarter, eighth, quarter - and the move that
                // makes it folk rather than square is that BEAT 3 IS SKIPPED: it
                // anticipates with the "and" of 2, leaves 3 empty, plays the
                // "and" of 3 and lands on 4. Drawn on one pitch, so there is no
                // pitch opinion here and the melody stays entirely the seed's.
                // Every 16th between the five onsets is free - that is where the
                // interesting touches come from.
                "Folk",
                16, 1, 4,
                //   1      e      &      a        2      e      &      a        3      e      &      a        4      e      &      a
                {  0.92f, -1.0f, -1.0f, -1.0f,   0.92f, -1.0f, 0.92f, -1.0f,   0.12f, -1.0f, 0.92f, -1.0f,   0.92f, -1.0f, -1.0f, -1.0f },
                {  -1.0f, -1.0f, -1.0f, -1.0f,   -1.0f, -1.0f, -1.0f, -1.0f,   -1.0f, -1.0f, -1.0f, -1.0f,   -1.0f, -1.0f, -1.0f, -1.0f },
                {  false, false, false, false,   false, false, false, false,   false, false, false, false,   false, false, false, false },
                0, 0
            },
            {
                // Two bars, and the feel is entirely in what is NOT played.
                // Beat 4 is empty in both bars; the "and" of 4 carries a long
                // note that pushes across the bar line and swallows bar 2's
                // downbeat. So the four onsets per bar are held, beats 4 are
                // held EMPTY, and the whole of bar 2 beat 1 is kept clear or the
                // push gets cut off after an eighth and the anticipation dies.
                // A late 16th is allowed to creep into that gap - in gospel that
                // space gets a fill often enough to be part of the language.
                "Gospel",
                32, 2, 4,
                //   1      e      &      a        2      e      &      a        3      e      &      a        4      e      &      a
                {  0.92f, -1.0f, -1.0f, -1.0f,   0.92f, -1.0f, 0.92f, -1.0f,   0.92f, -1.0f, 0.92f, -1.0f,   0.08f, -1.0f, 0.92f, 0.15f,
                   0.03f, 0.10f, 0.15f, 0.20f,   0.92f, -1.0f, 0.92f, -1.0f,   0.92f, -1.0f, 0.92f, -1.0f,   0.08f, -1.0f, 0.92f, 0.20f },
                {  -1.0f, -1.0f, -1.0f, -1.0f,   -1.0f, -1.0f, -1.0f, -1.0f,   -1.0f, -1.0f, -1.0f, -1.0f,   -1.0f, -1.0f, -1.0f, -1.0f,
                   -1.0f, -1.0f, -1.0f, -1.0f,   -1.0f, -1.0f, -1.0f, -1.0f,   -1.0f, -1.0f, -1.0f, -1.0f,   -1.0f, -1.0f, -1.0f, -1.0f },
                {  false, false, false, false,   false, false, false, false,   false, false, false, false,   false, false, false, false,
                   false, false, false, false,   false, false, false, false,   false, false, false, false,   false, false, false, false },
                0, 0
            },
            {
                // Eighths through 1, 1& and 2, then a push on the "a" of 2 that
                // vaults BEAT 3 - which stays empty - and lands on a 16th pair,
                // "e" and "and" of 3, before beat 4. That gallop pair and the
                // hole at beat 3 are the riff; without the hole the push and the
                // pair just read as busy 16ths. Everything else is free, so the
                // seed can chug or leave air after the landing on 4.
                "Metal",
                16, 1, 4,
                //   1      e      &      a        2      e      &      a        3      e      &      a        4      e      &      a
                {  0.92f, -1.0f, 0.92f, -1.0f,   0.92f, -1.0f, -1.0f, 0.92f,   0.08f, 0.92f, 0.92f, -1.0f,   0.92f, -1.0f, -1.0f, -1.0f },
                {  -1.0f, -1.0f, -1.0f, -1.0f,   -1.0f, -1.0f, -1.0f, -1.0f,   -1.0f, -1.0f, -1.0f, -1.0f,   -1.0f, -1.0f, -1.0f, -1.0f },
                {  false, false, false, false,   false, false, false, false,   false, false, false, false,   false, false, false, false },
                0, 0
            },
            {
                // Three-three-two across the front half: the downbeat, the "a"
                // of 1, then the "and" of 2 - which means BEAT 2 IS EMPTY, the
                // grouping vaults it. Straight eighths through beat 3, then a
                // 16th pair on 4 and its "e" to kick back round. Beat 1's "and"
                // stays free but rarely lands, because the "a" outranks it and
                // only a fully opened beat has room for both.
                "Disco",
                16, 1, 4,
                //   1      e      &      a        2      e      &      a        3      e      &      a        4      e      &      a
                {  0.92f, -1.0f, -1.0f, 0.92f,   0.08f, -1.0f, 0.92f, -1.0f,   0.92f, -1.0f, 0.92f, -1.0f,   0.92f, 0.92f, -1.0f, -1.0f },
                {  -1.0f, -1.0f, -1.0f, -1.0f,   -1.0f, -1.0f, -1.0f, -1.0f,   -1.0f, -1.0f, -1.0f, -1.0f,   -1.0f, -1.0f, -1.0f, -1.0f },
                {  false, false, false, false,   false, false, false, false,   false, false, false, false,   false, false, false, false },
                0, 0
            },
            {
                // Four square eighths - 1, 1&, 2, 2&, 3 - and then the back half
                // breaks: the "a" of 3 pushes over BEAT 4, WHICH STAYS EMPTY,
                // and lands on the "and" of 4. Square front, anticipated back.
                // Beat 3's "and" is left free but loses to the "a" in the
                // ranking, which is what stops the second half squaring up too.
                "80s Pop",
                16, 1, 4,
                //   1      e      &      a        2      e      &      a        3      e      &      a        4      e      &      a
                {  0.92f, -1.0f, 0.92f, -1.0f,   0.92f, -1.0f, 0.92f, -1.0f,   0.92f, -1.0f, -1.0f, 0.92f,   0.08f, -1.0f, 0.92f, -1.0f },
                {  -1.0f, -1.0f, -1.0f, -1.0f,   -1.0f, -1.0f, -1.0f, -1.0f,   -1.0f, -1.0f, -1.0f, -1.0f,   -1.0f, -1.0f, -1.0f, -1.0f },
                {  false, false, false, false,   false, false, false, false,   false, false, false, false,   false, false, false, false },
                0, 0
            },
            {
                // Baladi. In eighths: 1, 1&, [skip 2], 2&, 3, [skip], 4 - the
                // doubled downbeat, then BEAT 2 EMPTY so the "and" of 2 carries
                // the push, then the two landings. Only five notes in the bar,
                // which makes this the airiest idiom in the bank: the spaces
                // after 3 and 4 are left free on purpose, because that is
                // exactly where the style puts its ornaments.
                //
                // EXCEPT the "and" of 3, which is now suppressed. It was free,
                // and free meant it landed 60% of the time - which is precisely
                // Oriental 3's shape, so the two were near-indistinguishable.
                // The bare beat 3 is what makes this the airy one of the pair,
                // so it has to be stated rather than left to chance.
                "Oriental 1",
                16, 1, 4,
                //   1      e      &      a        2      e      &      a        3      e      &      a        4      e      &      a
                {  0.92f, -1.0f, 0.92f, -1.0f,   0.08f, -1.0f, 0.92f, -1.0f,   0.92f, -1.0f, 0.08f, -1.0f,   0.92f, -1.0f, -1.0f, -1.0f },
                {  -1.0f, -1.0f, -1.0f, -1.0f,   -1.0f, -1.0f, -1.0f, -1.0f,   -1.0f, -1.0f, -1.0f, -1.0f,   -1.0f, -1.0f, -1.0f, -1.0f },
                {  false, false, false, false,   false, false, false, false,   false, false, false, false,   false, false, false, false },
                0, 0
            },
            {
                // Malfuf: three-three-two across the front half, so BEAT 2 IS
                // EMPTY and the "and" of 2 closes the grouping. Then a bare beat
                // 3 with a whole quarter of air after it, and a turnaround on 4
                // and its "and".
                //
                // Shares its opening with Disco - both are tresillo - and the
                // tail is what separates them: Disco answers with a 16th pair on
                // 4 and its "e", this answers with 4 and its "and", a beat wider.
                "Oriental 2",
                16, 1, 4,
                //   1      e      &      a        2      e      &      a        3      e      &      a        4      e      &      a
                {  0.92f, -1.0f, -1.0f, 0.92f,   0.08f, -1.0f, 0.92f, -1.0f,   0.92f, -1.0f, -1.0f, -1.0f,   0.92f, -1.0f, 0.92f, -1.0f },
                {  -1.0f, -1.0f, -1.0f, -1.0f,   -1.0f, -1.0f, -1.0f, -1.0f,   -1.0f, -1.0f, -1.0f, -1.0f,   -1.0f, -1.0f, -1.0f, -1.0f },
                {  false, false, false, false,   false, false, false, false,   false, false, false, false,   false, false, false, false },
                0, 0
            },
            {
                // A quarter pulse on all four beats, with the skank on the
                // "and" of 2 AND 4 ONLY. That asymmetry is the whole idiom: the
                // "and" of 1 and the "and" of 3 are held back, because filling
                // them turns it into straight eighths and the skank stops being
                // an answer to the backbeat. So only two slots are suppressed
                // here - everything that is not a beat, an "and" of 2 or 4, or
                // one of those two, is free.
                "Reggae 1",
                16, 1, 4,
                //   1      e      &      a        2      e      &      a        3      e      &      a        4      e      &      a
                {  0.92f, -1.0f, 0.10f, -1.0f,   0.92f, -1.0f, 0.92f, -1.0f,   0.92f, -1.0f, 0.10f, -1.0f,   0.92f, -1.0f, 0.92f, -1.0f },
                {  -1.0f, -1.0f, -1.0f, -1.0f,   -1.0f, -1.0f, -1.0f, -1.0f,   -1.0f, -1.0f, -1.0f, -1.0f,   -1.0f, -1.0f, -1.0f, -1.0f },
                {  false, false, false, false,   false, false, false, false,   false, false, false, false,   false, false, false, false },
                0, 0
            },
            {
                // The rolling gate: every beat is beat, skip the "e", "and",
                // "a". Three of four 16ths, with the hole always in the same
                // place - that regularity IS the idiom, which makes this the
                // only one in the bank whose identity is its DENSITY rather than
                // its syncopation. It needs the 1/16 knob up to read; see the
                // note where the beat resolution is drawn.
                "Trance",
                16, 1, 4,
                //   1      e      &      a        2      e      &      a        3      e      &      a        4      e      &      a
                {  0.92f, 0.08f, 0.92f, 0.92f,   0.92f, 0.08f, 0.92f, 0.92f,   0.92f, 0.08f, 0.92f, 0.92f,   0.92f, 0.08f, 0.92f, 0.92f },
                {  -1.0f, -1.0f, -1.0f, -1.0f,   -1.0f, -1.0f, -1.0f, -1.0f,   -1.0f, -1.0f, -1.0f, -1.0f,   -1.0f, -1.0f, -1.0f, -1.0f },
                {  false, false, false, false,   false, false, false, false,   false, false, false, false,   false, false, false, false },
                0, 0
            },
            {
                // Bare quarters on 1 and 2, then a three-three push - the "a" of
                // 2 into the "and" of 3 - that vaults BEAT 3, WHICH STAYS EMPTY,
                // and a straight landing on 4 and its "and". The contrast is the
                // idiom: square front, off-kilter middle, square close.
                //
                // Beat 1's "and" is left free even though the drawn bar has it
                // empty, because a bare beat 1 is sparseness rather than
                // syncopation - and house fills that slot as often as not.
                "House",
                16, 1, 4,
                //   1      e      &      a        2      e      &      a        3      e      &      a        4      e      &      a
                {  0.92f, -1.0f, -1.0f, -1.0f,   0.92f, -1.0f, -1.0f, 0.92f,   0.08f, -1.0f, 0.92f, -1.0f,   0.92f, -1.0f, 0.92f, -1.0f },
                {  -1.0f, -1.0f, -1.0f, -1.0f,   -1.0f, -1.0f, -1.0f, -1.0f,   -1.0f, -1.0f, -1.0f, -1.0f,   -1.0f, -1.0f, -1.0f, -1.0f },
                {  false, false, false, false,   false, false, false, false,   false, false, false, false,   false, false, false, false },
                0, 0
            },
            {
                // A held downbeat, then straight eighths the rest of the way -
                // 2, 2&, 3, 3&, 4, 4&. The one hole is the "AND" OF 1, and it is
                // the whole idiom: every other beat gets its offbeat, beat 1
                // does not, so the bar opens on a long note and then drives.
                //
                // Near neighbour of Reggae, which suppresses the "and" of 3 as
                // well. That single slot is the difference between the two, so
                // they should not sit next to each other in the selector.
                "Astrada",
                16, 1, 4,
                //   1      e      &      a        2      e      &      a        3      e      &      a        4      e      &      a
                {  0.92f, -1.0f, 0.08f, -1.0f,   0.92f, -1.0f, 0.92f, -1.0f,   0.92f, -1.0f, 0.92f, -1.0f,   0.92f, -1.0f, 0.92f, -1.0f },
                {  -1.0f, -1.0f, -1.0f, -1.0f,   -1.0f, -1.0f, -1.0f, -1.0f,   -1.0f, -1.0f, -1.0f, -1.0f,   -1.0f, -1.0f, -1.0f, -1.0f },
                {  false, false, false, false,   false, false, false, false,   false, false, false, false,   false, false, false, false },
                0, 0
            },
            {
                // The mirror of Reggae 1. Same six notes, same four beats, but
                // the offbeats sit on 1 AND 4 instead of 2 and 4 - so the "and"
                // of 2 and the "and" of 3 are the suppressed pair here. The bar
                // opens and closes with an eighth-note pair and the middle is
                // bare quarters, where Reggae 1 answers the backbeat instead.
                //
                // Two slots apart from its neighbour, which is the whole point
                // of shipping both: same instrument, opposite half of the bar.
                "Reggae 2",
                16, 1, 4,
                //   1      e      &      a        2      e      &      a        3      e      &      a        4      e      &      a
                {  0.92f, -1.0f, 0.92f, -1.0f,   0.92f, -1.0f, 0.08f, -1.0f,   0.92f, -1.0f, 0.08f, -1.0f,   0.92f, -1.0f, 0.92f, -1.0f },
                {  -1.0f, -1.0f, -1.0f, -1.0f,   -1.0f, -1.0f, -1.0f, -1.0f,   -1.0f, -1.0f, -1.0f, -1.0f,   -1.0f, -1.0f, -1.0f, -1.0f },
                {  false, false, false, false,   false, false, false, false,   false, false, false, false,   false, false, false, false },
                0, 0
            },
            {
                // The 16th-note strum. Ten notes, and the shape is in how they
                // CLUSTER: two on beat 1, three on beat 2, two on beat 3, three
                // on beat 4, so the bar breathes in and out twice.
                //
                // Second idiom in the bank to own its density, and for the same
                // reason as Trance: thinned to one note a beat this is just
                // quarters. Nothing needs suppressing here - with the beat given
                // exactly as many slots as the table marks essential, the empty
                // ones are never even reached, so they stay free and come back
                // as ORIGINALITY is turned down.
                "Doobie",
                16, 1, 4,
                //   1      e      &      a        2      e      &      a        3      e      &      a        4      e      &      a
                {  0.92f, -1.0f, -1.0f, 0.92f,   0.92f, -1.0f, 0.92f, 0.92f,   0.92f, -1.0f, 0.92f, -1.0f,   0.92f, 0.92f, 0.92f, -1.0f },
                {  -1.0f, -1.0f, -1.0f, -1.0f,   -1.0f, -1.0f, -1.0f, -1.0f,   -1.0f, -1.0f, -1.0f, -1.0f,   -1.0f, -1.0f, -1.0f, -1.0f },
                {  false, false, false, false,   false, false, false, false,   false, false, false, false,   false, false, false, false },
                0, 0
            },
            {
                // Oriental 1 with beat 3 filled in: doubled downbeat, BEAT 2
                // EMPTY so the "and" of 2 carries the push, then 3 and its
                // "and", then 4. One slot separates this from Oriental 1 - which
                // is why that slot is suppressed there and essential here rather
                // than left free in both.
                "Oriental 3",
                16, 1, 4,
                //   1      e      &      a        2      e      &      a        3      e      &      a        4      e      &      a
                {  0.92f, -1.0f, 0.92f, -1.0f,   0.08f, -1.0f, 0.92f, -1.0f,   0.92f, -1.0f, 0.92f, -1.0f,   0.92f, -1.0f, -1.0f, -1.0f },
                {  -1.0f, -1.0f, -1.0f, -1.0f,   -1.0f, -1.0f, -1.0f, -1.0f,   -1.0f, -1.0f, -1.0f, -1.0f,   -1.0f, -1.0f, -1.0f, -1.0f },
                {  false, false, false, false,   false, false, false, false,   false, false, false, false,   false, false, false, false },
                0, 0
            },
            {
                // First 3/4 idiom in the bank. A held downbeat, then eighths
                // through beats 2 and 3 - so the hole is the "AND" OF 1, the
                // same statement Astrada makes in 4/4: every other beat gets its
                // offbeat and the first one does not, so the bar opens long.
                //
                // 12 slots over 3 beats, which is real 16ths because the meter
                // is stated rather than inherited. Offered only in a 3/4
                // project; anywhere else it stands down and the arp free runs.
                "Georgia 3/4",
                12, 1, 3,
                //   1      e      &      a        2      e      &      a        3      e      &      a
                {  0.92f, -1.0f, 0.08f, -1.0f,   0.92f, -1.0f, 0.92f, -1.0f,   0.92f, -1.0f, 0.92f, -1.0f },
                {  -1.0f, -1.0f, -1.0f, -1.0f,   -1.0f, -1.0f, -1.0f, -1.0f,   -1.0f, -1.0f, -1.0f, -1.0f },
                {  false, false, false, false,   false, false, false, false,   false, false, false, false },
                0, 0
            },
            {
                // A waltz that leans off the beat. Beats 1 and 3 are the beat
                // plus its "a"; beat 2 is entirely offbeat - its "e" and its
                // "and" - because BEAT 2 ITSELF IS EMPTY. Two notes to a beat
                // throughout, but never the same two, which is what stops it
                // sounding like a straight 3/4 with decoration.
                //
                // Thinned to one note a beat it reads 1, 2&, 3 and still leans,
                // so the density stays the knobs' business.
                "Reggae 3/4",
                12, 1, 3,
                //   1      e      &      a        2      e      &      a        3      e      &      a
                {  0.92f, -1.0f, -1.0f, 0.92f,   0.08f, 0.92f, 0.92f, -1.0f,   0.92f, -1.0f, -1.0f, 0.92f },
                {  -1.0f, -1.0f, -1.0f, -1.0f,   -1.0f, -1.0f, -1.0f, -1.0f,   -1.0f, -1.0f, -1.0f, -1.0f },
                {  false, false, false, false,   false, false, false, false,   false, false, false, false },
                0, 0
            },
            {
                // Straight eighths, all six of them, and nothing suppressed -
                // the only idiom in the bank with no hole at all. What it states
                // is REGULARITY: two notes to every beat, the same two, no
                // syncopation anywhere. That is the whole point of it, and it is
                // the calm answer to a bank where everything else leans.
                //
                // It owns its density for exactly that reason. Left to the knobs
                // a quarter of the beats would open to 16ths and the evenness -
                // the one thing it has to say - would be gone.
                "Ballad 3/4",
                12, 1, 3,
                //   1      e      &      a        2      e      &      a        3      e      &      a
                {  0.92f, -1.0f, 0.92f, -1.0f,   0.92f, -1.0f, 0.92f, -1.0f,   0.92f, -1.0f, 0.92f, -1.0f },
                {  -1.0f, -1.0f, -1.0f, -1.0f,   -1.0f, -1.0f, -1.0f, -1.0f,   -1.0f, -1.0f, -1.0f, -1.0f },
                {  false, false, false, false,   false, false, false, false,   false, false, false, false },
                0, 0
            },
            {
                // Three-three-two twice over: 1, its "a", the "and" of 2 - then
                // exactly the same again from beat 3. BEATS 2 AND 4 ARE BOTH
                // EMPTY, each vaulted by its own tresillo, and the halves are
                // identical, which is what gives it the relentless quality.
                //
                // Shares its opening with Disco and Oriental 2, and separates
                // from both in the second half: they land on beat 4, this one
                // vaults it. That single slot is the difference, so it is
                // suppressed here and essential there rather than free anywhere.
                "Dance",
                16, 1, 4,
                //   1      e      &      a        2      e      &      a        3      e      &      a        4      e      &      a
                {  0.92f, -1.0f, -1.0f, 0.92f,   0.08f, -1.0f, 0.92f, -1.0f,   0.92f, -1.0f, -1.0f, 0.92f,   0.08f, -1.0f, 0.92f, -1.0f },
                {  -1.0f, -1.0f, -1.0f, -1.0f,   -1.0f, -1.0f, -1.0f, -1.0f,   -1.0f, -1.0f, -1.0f, -1.0f,   -1.0f, -1.0f, -1.0f, -1.0f },
                {  false, false, false, false,   false, false, false, false,   false, false, false, false,   false, false, false, false },
                0, 0
            },
            {
                // Two bars that answer each other. Bar 1 states it on the beat -
                // 1, 2, 2&, 3, 3& - then pushes off the "and" of 4 across the
                // bar line, so BAR 2'S DOWNBEAT IS EMPTY, exactly the Gospel
                // anticipation. Bar 2 then drives on offbeats, its beats 1 and 2
                // both empty and carried by their "and"s, before landing on 3, 4
                // and the "and" of 4 to hand back to the top.
                //
                // Five suppressed slots, the most in the bank, and every one of
                // them is a beat that got vaulted rather than a subdivision
                // being tidied away.
                "Rock",
                32, 2, 4,
                //   1      e      &      a        2      e      &      a        3      e      &      a        4      e      &      a
                {  0.92f, -1.0f, 0.08f, -1.0f,   0.92f, -1.0f, 0.92f, -1.0f,   0.92f, -1.0f, 0.92f, -1.0f,   0.08f, -1.0f, 0.92f, -1.0f,
                   0.08f, -1.0f, 0.92f, -1.0f,   0.08f, -1.0f, 0.92f, -1.0f,   0.92f, -1.0f, 0.08f, -1.0f,   0.92f, -1.0f, 0.92f, -1.0f },
                {  -1.0f, -1.0f, -1.0f, -1.0f,   -1.0f, -1.0f, -1.0f, -1.0f,   -1.0f, -1.0f, -1.0f, -1.0f,   -1.0f, -1.0f, -1.0f, -1.0f,
                   -1.0f, -1.0f, -1.0f, -1.0f,   -1.0f, -1.0f, -1.0f, -1.0f,   -1.0f, -1.0f, -1.0f, -1.0f,   -1.0f, -1.0f, -1.0f, -1.0f },
                {  false, false, false, false,   false, false, false, false,   false, false, false, false,   false, false, false, false,
                   false, false, false, false,   false, false, false, false,   false, false, false, false,   false, false, false, false },
                0, 0
            }
        };
        return table;
    }

    inline constexpr int kIdiomCount = 20;
}

// Index 0 is always "Free Running" - no idiom, the seeded engine on its own.
// 1.. index the table above.
inline int idiomCount() { return 1 + detail::kIdiomCount; }

// True when this idiom can be used in a project of the given meter. The editor
// uses it to grey out what does not apply; bake() enforces it regardless.
inline bool idiomFitsMeter (int index, double beatsPerBar);

inline const ArpIdiom* idiomAt (int index)
{
    if (index <= 0 || index > detail::kIdiomCount)
        return nullptr;

    return detail::idiomTable() + (index - 1);
}

inline bool idiomFitsMeter (int index, double beatsPerBar)
{
    const ArpIdiom* a = idiomAt (index);
    if (a == nullptr)
        return true;                       // Free Running fits everything

    return std::fabs (double (a->meter) - beatsPerBar) < 0.01;
}

inline const char* idiomName (int index)
{
    const ArpIdiom* a = idiomAt (index);
    return (a != nullptr) ? a->name : "Free Running";
}

struct ArpParams
{
    uint32_t seed          = 973;
    int      patternBars   = 4;      // 1..8
    int      octaveRange   = 1;      // 1..4
    int      semitones     = 12;

    // relative weights of 1/16, 1/8, 1/4 - drive the subdivision probabilities
    float weight16 = 1.0f;
    float weight8  = 1.0f;
    float weight4  = 0.35f;

    float restProb   = 0.12f;
    float gateMin    = 0.45f;
    float gateMax    = 0.90f;
    float complexity = 0.80f;        // 0 = chaotic, 1 = maximally shaped

    // Idiom: 0 = free running, 1.. = idiomAt().
    //
    // ORIGINALITY is whose skeleton the bar is built on, 0..1. At 0 the idiom is
    // not consulted at all and the knobs and seed decide everything. At 1 the
    // pattern is reduced to the idiom's own skeleton - its positions, its note
    // count per beat, its accents - whatever the other controls say.
    //
    // COMPLEXITY is a separate axis: how much the generator elaborates whatever
    // skeleton it was handed. The two are orthogonal, and the interesting
    // territory is high ORIGINALITY with COMPLEXITY up, which is the fusion.
    int   idiom       = 0;
    float originality = 0.75f;

    float beatsPerBar = 4.0f;

    // Chord-gather window: how long to wait after the first key of a chord
    // before the pattern starts, so the first note is resolved against the
    // whole chord instead of whichever key happened to arrive first.
    // The host layer sets this PER CHORD from look-ahead, and it is zero when
    // the chord is known to be complete already (programmed MIDI, where every
    // note lands on the same sample) - so a drawn part starts dead on the grid.
    float chordGatherBeats = 0.0f;

    bool        sortNotes = true;
    RestartMode restart   = RestartMode::Free;

    // locators, in beats from the pattern start; loop = [start, end)
    float loopStartBeats = 0.0f;
    float loopEndBeats   = 16.0f;
};

// HOW MANY BARS TO BAKE. BARS sets the floor, but the loop-end locator is
// allowed to open past it, so the pattern grows to cover wherever that locator
// sits, rounded up to a whole bar. Without this the locator clamps back to BARS
// and dragging it out does nothing at all.
//
// bake() and hashParams() live in different classes and must agree on this, or
// a locator that lengthens the pattern would never trigger a re-bake - hence a
// free function rather than a member of either. Hashing the RESULT rather than
// the locator also means dragging within a bar does not churn the audio thread.
inline int bakeBarsFor (const ArpParams& p) noexcept
{
    const double bpb  = std::max (1.0, double (p.beatsPerBar));
    const int    bars = std::min (8, std::max (1, p.patternBars));

    double total = double (bars) * bpb;

    const double wanted = std::min (double (p.loopEndBeats), double (kMaxPatternBars) * bpb);
    if (wanted > total + 1.0e-6)
        total = std::ceil (wanted / bpb - 1.0e-6) * bpb;

    const int out = (int) std::lround (total / bpb);
    return std::min (kMaxPatternBars, std::max (1, out));
}

struct PatternNote
{
    float   startBeat   = 0.0f;   // offset from pattern start
    float   lengthBeats = 0.0f;   // already gated
    int16_t rung        = 0;      // index into the NOMINAL ladder
    float   accent      = 1.0f;   // velocity multiplier, 0..1
};

struct ArpEvent
{
    int    note;          // 0..127, already resolved and clamped
    float  velocity;      // 0..1
    double startBeat;     // absolute beat position
    double lengthBeats;
};

// ---------------------------------------------------------------------------
// Baked pattern container (also the GUI snapshot payload)
// ---------------------------------------------------------------------------
struct PatternSnapshot
{
    static constexpr int kMaxNotes    = 512;
    static constexpr int kNominalPool = 4;    // ladder baked against a 4-note chord

    int         count         = 0;
    float       totalBeats    = 16.0f;
    float       beatsPerBar   = 4.0f;
    int         bars          = 4;
    int         nominalLadder = 4;
    PatternNote notes[kMaxNotes] {};
};

struct ChordSnapshot
{
    int size      = 0;
    int octaves   = 1;
    int semitones = 12;
    int notes[16] {};
};

// ---------------------------------------------------------------------------
// Lock-free single-writer / single-reader publisher (seqlock)
// Writer = audio thread, reader = GUI thread. Reader retries on a torn read.
// ---------------------------------------------------------------------------
template <typename T>
class SeqLockPublisher
{
public:
    void publish (const T& v) noexcept
    {
        version.fetch_add (1, std::memory_order_release);
        std::atomic_thread_fence (std::memory_order_release);
        payload = v;
        std::atomic_thread_fence (std::memory_order_release);
        version.fetch_add (1, std::memory_order_release);
    }

    bool read (T& out) const noexcept
    {
        for (int attempt = 0; attempt < 8; ++attempt)
        {
            const uint32_t v1 = version.load (std::memory_order_acquire);
            if ((v1 & 1u) != 0u)
                continue;

            std::atomic_thread_fence (std::memory_order_acquire);
            out = payload;
            std::atomic_thread_fence (std::memory_order_acquire);

            if (version.load (std::memory_order_acquire) == v1)
                return true;
        }
        return false;
    }

private:
    mutable std::atomic<uint32_t> version { 0 };
    T payload {};
};

// ---------------------------------------------------------------------------
// Musical pattern baker
// ---------------------------------------------------------------------------
class PatternBaker
{
public:
    static int bake (const ArpParams& p, PatternSnapshot& dest)
    {
        const double bpb   = clampD (p.beatsPerBar, 1.0, 16.0);
        const int    bars  = bakeBarsFor (p);
        const double m     = clampD (p.complexity, 0.0, 1.0);
        const int    beats = std::max (1, (int) std::lround (bpb));

        dest.count         = 0;
        dest.beatsPerBar   = (float) bpb;
        dest.bars          = bars;
        dest.totalBeats    = float (bars * bpb);
        dest.nominalLadder = std::max (2, PatternSnapshot::kNominalPool * clampI (p.octaveRange, 1, 4));

        // three independent streams: knob-tweak isolation
        SplitMix64 rForm, rRhythm, rPitch;
        rForm  .seed (uint64_t (p.seed) * 0x9E3779B1ull + 0x1234567ull);
        rRhythm.seed (uint64_t (p.seed) * 0xC2B2AE3Dull + 0x9E3779Bull);
        rPitch .seed (uint64_t (p.seed) * 0x27D4EB2Full + 0x165667B1ull);

        // The chosen idiom, if there is one and ORIGINALITY is off zero. When there
        // is not, every call below takes the path it always took.
        const ArpIdiom* idm = idiomAt (p.idiom);
        const double    c   = clampD (p.originality, 0.0, 1.0);

        // An idiom is written in a meter and only makes sense in it, so one that
        // disagrees with the project is not applied at all - the generator free
        // runs instead. Better an idiom that politely stands down than a 3/4
        // figure smeared across a 4/4 bar.
        const bool meterFits = (idm != nullptr)
                            && std::fabs (double (idm->meter) - bpb) < 0.01;

        const bool useIdiom = (idm != nullptr && c > 1.0e-6 && meterFits);

        // pitch walks pre-drawn, so rhythm tweaks do not move the melody
        Walk walk;
        int16_t rungs[kSources][kWalkLen];
        for (int s = 0; s < kSources; ++s)
            buildWalk (rPitch, dest.nominalLadder, m, walk, rungs[s],
                       useIdiom ? idm : nullptr, c);

        // An idiom may be two bars long, so the pattern is laid out in UNITS.
        // Without an idiom a unit is one bar and everything below is unchanged.
        const int unitBars = useIdiom ? clampI (idm->bars, 1, 2) : 1;

        SourceBar src[kSources];
        for (int s = 0; s < kSources; ++s)
        {
            if (useIdiom) buildIdiomUnit (rRhythm, p, m, bpb, rungs[s], *idm, c, src[s]);
            else          buildBar       (rRhythm, p, m, bpb, beats, rungs[s], src[s]);
        }

        static const int forms[5][8] = {
            { 0,0,0,0, 0,0,0,0 },   // AAAA
            { 0,0,0,1, 0,0,0,1 },   // AAAB
            { 0,1,0,1, 0,1,0,1 },   // ABAB
            { 0,0,1,1, 0,0,1,1 },   // AABB
            { 0,1,0,2, 0,1,0,2 }    // ABAC
        };
        const int form = clampI ((int) (rForm.nextDouble() * 5.0), 0, 4);

        for (int bar = 0, unit = 0; bar < bars; bar += unitBars, ++unit)
        {
            const double unitStart = bar * bpb;
            const bool   useMotif  = (rForm.nextDouble() < m);

            SourceBar fresh;
            const SourceBar* chosen = nullptr;

            if (useMotif)
            {
                chosen = &src[forms[form][unit & 7]];
            }
            else
            {
                int16_t freshRungs[kWalkLen];
                buildWalk (rPitch, dest.nominalLadder, m, walk, freshRungs,
                           useIdiom ? idm : nullptr, c);

                if (useIdiom) buildIdiomUnit (rRhythm, p, m, bpb, freshRungs, *idm, c, fresh);
                else          buildBar       (rRhythm, p, m, bpb, beats, freshRungs, fresh);

                chosen = &fresh;
            }

            for (int i = 0; i < chosen->count && dest.count < PatternSnapshot::kMaxNotes; ++i)
            {
                PatternNote n = chosen->notes[i];
                n.startBeat += (float) unitStart;

                // A two-bar idiom asked for inside a one-bar pattern overhangs
                // the end; keep what fits rather than refusing to play at all.
                if (n.startBeat >= dest.totalBeats - 1.0e-4f)
                    continue;

                n.lengthBeats = std::min (n.lengthBeats, dest.totalBeats - n.startBeat);
                dest.notes[dest.count++] = n;
            }
        }

        std::stable_sort (dest.notes, dest.notes + dest.count,
                          [] (const PatternNote& a, const PatternNote& b)
                          { return a.startBeat < b.startBeat; });

        return dest.count;
    }

private:
    static constexpr int kSources = 3;
    static constexpr int kWalkLen = 64;
    static constexpr int kMaxLeaf = 128;

    struct SourceBar
    {
        int         count = 0;
        PatternNote notes[kMaxLeaf] {};
    };

    struct Walk { int prev = 0; int dir = 1; bool bigLeap = false; };

    static int    clampI (int v, int lo, int hi)          { return v < lo ? lo : (v > hi ? hi : v); }
    static double clampD (double v, double lo, double hi) { return v < lo ? lo : (v > hi ? hi : v); }
    static double lerpD  (double a, double b, double t)   { return a + (b - a) * t; }

    // Metric strength: downbeat 1.0 ... 16th 0.35
    static double strengthAt (double offsetInBar)
    {
        const double eps = 1.0e-6;
        if (std::fabs (offsetInBar) < eps)                            return 1.00;
        if (std::fabs (offsetInBar - std::round (offsetInBar)) < eps) return 0.80;
        if (std::fabs (std::fmod (offsetInBar, 0.5)) < eps)           return 0.55;
        return 0.35;
    }

    // ---- pitch: weighted walk with gap-fill and edge reflection -----------
    static void buildWalk (SplitMix64& rng, int ladder, double m, Walk& w, int16_t* out,
                           const ArpIdiom* a = nullptr, double c = 0.0)
    {
        // The idiom's run ascends through a window of the ladder and wraps
        // when it passes the top - that wrap IS the octave drop. The window is
        // centred so it has ladder above and below it whenever OCTAVES gives it
        // the room; at OCTAVES 1 the ladder is only one chord wide and the run
        // degrades to a plain cycle with no drop.
        // rungWindow 0 or 1 means the idiom has no PITCH opinion: it shapes the
        // rhythm and leaves the melody entirely to the seed. That is the normal
        // case for an idiom drawn as a single repeated note, where the file says
        // everything about where the notes fall and nothing about what they are.
        const bool hasRun = (a != nullptr && c > 1.0e-6 && ladder >= 2 && a->rungWindow >= 2);
        const int  win    = hasRun ? clampI (a->rungWindow, 2, ladder) : 0;
        const int  base   = hasRun ? clampI ((ladder - win) / 2, 0, ladder - win) : 0;

        for (int i = 0; i < kWalkLen; ++i)
        {
            int next;

            // Drawn only when there is an idiom, so a seed with none set
            // produces the exact walk it always did.
            // Capped below 1 on purpose: even at full ORIGINALITY roughly one step
            // in ten is the ordinary walk, so the run stays a spine rather than
            // a loop the ear can count.
            const bool useRun = hasRun && (rng.nextDouble() < c * 0.9);

            if (useRun)
            {
                next = w.prev + clampI (a->rungStep, 1, std::max (1, win - 1));

                if (next >= base + win) next -= win;
                if (next <  base)       next  = base;

                next      = clampI (next, 0, ladder - 1);
                w.dir     = 1;
                w.bigLeap = false;
            }
            else if (rng.nextDouble() < (1.0 - m))
            {
                next      = clampI ((int) (rng.nextDouble() * ladder), 0, ladder - 1);
                w.dir     = (next >= w.prev) ? 1 : -1;
                w.bigLeap = std::abs (next - w.prev) >= 3;
            }
            else
            {
                const double r = rng.nextDouble();
                const int stepSize = (r < 0.55) ? 1 : (r < 0.82 ? 2 : (r < 0.95 ? 3 : 4));

                int dir = w.dir;
                if (w.bigLeap)                    dir = -dir;   // gap-fill after a leap
                else if (rng.nextDouble() < 0.28) dir = -dir;   // occasional turn

                next = w.prev + dir * stepSize;

                for (int guard = 0; guard < 8 && (next < 0 || next > ladder - 1); ++guard)
                {
                    if (next < 0)          next = -next;
                    if (next > ladder - 1) next = 2 * (ladder - 1) - next;
                    dir = -dir;
                }

                next      = clampI (next, 0, ladder - 1);
                w.dir     = dir;
                w.bigLeap = (stepSize >= 3);
            }

            w.prev = next;
            out[i] = (int16_t) next;
        }
    }

    // ---- rhythm: subdivision + tie pass, then rests / accent / gate -------
    static void buildBar (SplitMix64& rng, const ArpParams& p, double m,
                          double bpb, int beats, const int16_t* rungs, SourceBar& bar)
    {
        bar.count = 0;

        const double w16 = std::max (0.0f, p.weight16);
        const double w8  = std::max (0.0f, p.weight8);
        const double w4  = std::max (0.0f, p.weight4);
        const double tot = w16 + w8 + w4;

        // The three knobs set the RATIO OF NOTE COUNTS in the bar, not split
        // probabilities. Splitting produces two notes where there was one, so
        // equal split odds would give twice as many short notes as long ones.
        // Convert counts to a share of TIME first: a 1/4 fills a beat, a 1/8
        // half a beat, a 1/16 a quarter. The share of time each type must
        // occupy is (count weight x its duration), normalised - and those
        // shares are exactly the split probabilities the tree needs.
        double pSplitBeat   = 0.6;
        double pSplitEighth = 0.5;

        if (tot > 1.0e-9)
        {
            const double timeTotal = w4 * 1.0 + w8 * 0.5 + w16 * 0.25;
            pSplitBeat = (timeTotal > 1.0e-9) ? clampD (1.0 - w4 / timeTotal, 0.0, 1.0) : 0.0;

            const double denom8 = 2.0 * w8 + w16;
            pSplitEighth = (denom8 > 1.0e-9) ? clampD (w16 / denom8, 0.0, 1.0) : 0.0;
        }

        double leafStart[kMaxLeaf];
        double leafLen  [kMaxLeaf];
        int    leafCount = 0;

        for (int b = 0; b < beats && leafCount < kMaxLeaf - 8; ++b)
            subdivide (rng, double (b), 1.0, pSplitBeat, pSplitEighth,
                       leafStart, leafLen, leafCount);

        const double covered = double (beats);
        if (bpb - covered > 1.0e-6 && leafCount < kMaxLeaf)
        {
            leafStart[leafCount] = covered;
            leafLen  [leafCount] = bpb - covered;
            ++leafCount;
        }

        // tie pass: adjacent whole beats become occasional half notes
        const double pTie = clampD (0.35 * (w4 / std::max (1.0e-9, tot)) * m, 0.0, 0.5);
        for (int i = 0; i + 1 < leafCount; ++i)
        {
            const bool bothWhole    = std::fabs (leafLen[i]     - 1.0) < 1.0e-6
                                   && std::fabs (leafLen[i + 1] - 1.0) < 1.0e-6;
            const bool onStrongBeat = std::fabs (std::fmod (leafStart[i], 2.0)) < 1.0e-6;

            if (bothWhole && onStrongBeat && rng.nextDouble() < pTie)
            {
                leafLen[i] = 2.0;
                for (int k = i + 1; k + 1 < leafCount; ++k)
                {
                    leafStart[k] = leafStart[k + 1];
                    leafLen  [k] = leafLen  [k + 1];
                }
                --leafCount;
            }
        }

        bool isRest[kMaxLeaf];
        for (int i = 0; i < leafCount; ++i)
        {
            const double s = strengthAt (leafStart[i]);
            double pRest = double (p.restProb) * lerpD (1.0, clampD (1.25 - s, 0.0, 2.0), m);

            if (i == 0)
                pRest *= (1.0 - 0.9 * m);          // keep the downbeat sounding

            isRest[i] = (rng.nextDouble() < pRest);
        }

        int onsetIdx = 0;
        for (int i = 0; i < leafCount; ++i)
        {
            if (isRest[i])
                continue;

            const double s = strengthAt (leafStart[i]);

            const double gLo = std::min (p.gateMin, p.gateMax);
            const double gHi = std::max (p.gateMin, p.gateMax);
            double gate = gLo + rng.nextDouble() * (gHi - gLo);
            gate = lerpD (gate, gate * (0.75 + 0.35 * s), m);

            if ((i + 1 < leafCount) && isRest[i + 1])
                gate = lerpD (gate, std::min (1.0, gate * 1.45), m);   // legato into the gap

            gate = clampD (gate, 0.05, 1.0);

            PatternNote n;
            n.startBeat   = (float) leafStart[i];
            n.lengthBeats = (float) std::max (0.03, leafLen[i] * gate);
            n.accent      = (float) clampD (lerpD (0.85, 0.55 + 0.45 * s, m), 0.2, 1.0);

            const bool anchor = (i == 0) && (rng.nextDouble() < 0.85 * m);
            n.rung = anchor ? int16_t (0) : rungs[onsetIdx & (kWalkLen - 1)];

            ++onsetIdx;

            if (bar.count < kMaxLeaf)
                bar.notes[bar.count++] = n;
        }
    }

    // ---- rhythm from an idiom ---------------------------------------------
    // A flat grid rather than the subdivision tree, because the whole point is
    // choosing WHERE, which the tree cannot express. The idiom declares its own
    // resolution, so this walks 16ths, triplets or two bars of either without
    // caring which. Each slot sounds with odds blended between the idiom's
    // preference and the density the 1/16-1/8-1/4 knobs alone would give, so
    // ORIGINALITY slides between the two and the knobs never stop meaning anything.
    // Lengths run to the next onset and are then gated, so GATE MIN / GATE MAX
    // still shape the feel.
    //
    // The unit can be two bars long, so it is not "a bar" any more - bake()
    // steps the pattern in units, not bars.
    static void buildIdiomUnit (SplitMix64& rng, const ArpParams& p, double m,
                                double bpb, const int16_t* rungs,
                                const ArpIdiom& a, double c, SourceBar& bar)
    {
        bar.count = 0;

        const int    slots     = clampI (a.slots, 1, kIdiomSlots);
        const int    unitBars  = clampI (a.bars,  1, 2);
        const double unitBeats = unitBars * clampD (double (a.meter), 1.0, 16.0);
        const double slotBeats = unitBeats / double (slots);


        const double w16 = std::max (0.0f, p.weight16);
        const double w8  = std::max (0.0f, p.weight8);
        const double w4  = std::max (0.0f, p.weight4);
        const double wt  = std::max (1.0e-9, w16 + w8 + w4);

        // HOW MANY notes is the knobs' business; WHERE they go is the idiom's.
        // Keeping those separate is the whole reason this sounds like music: if
        // every slot were decided on its own the density would be flat across
        // the bar and every note would run only as far as the next slot, which
        // is a machine gun of 16ths. The subdivision tree never did that - some
        // beats carried one long note, others ran short ones - so that is what
        // gets rebuilt here, one beat at a time.
        //
        // Each beat draws a resolution from the SAME 1/16-1/8-1/4 conversion the
        // tree uses, giving it one, two or all of its slots. The idiom then says
        // WHICH of the beat's slots those are, strongest preference first. So a
        // montuno beat allowed a single note puts it on the "e" rather than the
        // downbeat - a syncopated quarter, still unmistakably salsa - and the
        // long note falls out of the gap that leaves behind.
        double pSplitBeat   = 0.6;
        double pSplitEighth = 0.5;

        if (wt > 1.0e-9)
        {
            const double timeTotal = w4 * 1.0 + w8 * 0.5 + w16 * 0.25;
            pSplitBeat = (timeTotal > 1.0e-9) ? clampD (1.0 - w4 / timeTotal, 0.0, 1.0) : 0.0;

            const double denom8 = 2.0 * w8 + w16;
            pSplitEighth = (denom8 > 1.0e-9) ? clampD (w16 / denom8, 0.0, 1.0) : 0.0;
        }

        const int beatsInUnit = std::max (1, (int) std::lround (unitBeats));
        const int perBeat     = std::max (1, slots / beatsInUnit);

        bool allowed[kIdiomSlots] {};

        for (int b = 0; b < beatsInUnit; ++b)
        {
            const int base = b * perBeat;
            if (base >= slots)
                break;

            const int n = std::min (perBeat, slots - base);

            int d = 1;                                   // one note: a whole beat long
            if (rng.nextDouble() < pSplitBeat)
                d = (rng.nextDouble() < pSplitEighth) ? n           // every slot
                                                      : std::max (1, n / 2);
            d = clampI (d, 1, n);

            // HOW MANY notes this beat gets is itself blended by ORIGINALITY.
            // The idiom states its own count in its table - the number of slots
            // it marks essential - and at full ORIGINALITY that count wins, which
            // is what "reduced to the original idiom" has to mean. Turn it down
            // and the beat is handed back to the 1/16-1/8-1/4 weights.
            //
            // This used to be an opt-in flag on the few idioms whose identity was
            // their density rather than their holes. It does not need to be: the
            // knob covers every idiom, continuously.
            {
                int want = 0;
                for (int k = 0; k < n; ++k)
                    if (a.onset[base + k] >= 0.5f)
                        ++want;

                if (want > 0)
                    d = clampI ((int) std::lround (lerpD (double (d), double (want), c)), 1, n);
            }

            // Rank the beat's slots: idiom preference first, and where it has no
            // opinion fall back to metric order - downbeat, then the midpoint,
            // then the rest - so a blank idiom degrades to what the tree did.
            int order[kIdiomSlots];
            for (int k = 0; k < n; ++k)
                order[k] = k;

            // Which slots a beat may use is most of an idiom's power, so this
            // ranking has to answer to ORIGINALITY - and it has to CROSSFADE, not
            // scale. Pulling every score toward one constant leaves the ordering
            // untouched, so the character arrived at full strength the moment
            // ORIGINALITY left zero. Crossfading against the METRIC order fixes it:
            // at 0 the beat, then its midpoint, then the rest - the same
            // placement the subdivision tree makes - and at 1 the idiom's own
            // preference. In between, slots change rank as the knob passes them.
            // Beat first, then its midpoint, then the REMAINING SLOTS FROM THE
            // BACK. That last part matters whenever an idiom suppresses the
            // midpoint: the beat's second note then falls to whatever is left,
            // and the slot before the next beat is a pickup while the slot right
            // after this one is a flam. Ranking late offsets higher makes the
            // fallback musical instead of merely metric.
            auto metric = [&] (int off)
            {
                if (off == 0)                        return 0;
                if ((n % 2) == 0 && off == n / 2)    return 1;
                return 2 + (n - off);
            };
            auto metricScore = [&] (int off)
            {
                const int r = metric (off);
                return (r == 0) ? 1.0 : (r == 1) ? 0.75 : 0.5 - 0.01 * double (r);
            };
            auto pref = [&] (int off)
            {
                const double w    = double (a.onset[base + off]);
                const double want = (w < 0.0) ? 0.5 : w;
                return lerpD (metricScore (off), want, c);
            };

            for (int x = 1; x < n; ++x)                  // insertion sort, no allocation
            {
                const int key = order[x];
                int y = x - 1;
                while (y >= 0 && (pref (order[y]) < pref (key)
                                  || (pref (order[y]) == pref (key)
                                      && metric (order[y]) > metric (key))))
                {
                    order[y + 1] = order[y];
                    --y;
                }
                order[y + 1] = key;
            }

            for (int k = 0; k < d; ++k)
                allowed[base + order[k]] = true;
        }

        // Density is the beat's job now, so this only decides whether an allowed
        // slot rests - which is what RESTS has always meant.
        const double pNeutral = clampD (1.0 - double (p.restProb), 0.05, 1.0);

        bool sound[kIdiomSlots] {};
        int  sounding = 0;

        for (int i = 0; i < slots; ++i)
        {
            if (! allowed[i])
                continue;

            // negative = the idiom has no opinion here, so the seed decides
            const double want = double (a.onset[i]);
            const double pOn  = (want < 0.0) ? pNeutral : lerpD (pNeutral, want, c);

            sound[i] = (rng.nextDouble() < pOn);
            if (sound[i])
                ++sounding;
        }

        if (sounding == 0)                 // never hand back a silent unit
            sound[0] = true;

        const double gLo = std::min (p.gateMin, p.gateMax);
        const double gHi = std::max (p.gateMin, p.gateMax);

        int     onsetIdx = 0;
        int16_t lastRung = 0;

        for (int i = 0; i < slots; ++i)
        {
            if (! sound[i])
                continue;

            int next = i + 1;
            while (next < slots && ! sound[next])
                ++next;

            const double start = i * slotBeats;
            const double span  = (next - i) * slotBeats;

            // metric strength is a property of the position WITHIN ITS BAR, so
            // bar 2 of a two-bar idiom gets a downbeat of its own
            const double s = strengthAt (std::fmod (start, bpb));
            const int    k = i;

            double gate = gLo + rng.nextDouble() * (gHi - gLo);
            gate = clampD (lerpD (gate, gate * (0.75 + 0.35 * s), m), 0.05, 1.0);

            const double metric = clampD (lerpD (0.85, 0.55 + 0.45 * s, m), 0.2, 1.0);

            PatternNote n;
            n.startBeat   = (float) start;
            n.lengthBeats = (float) std::max (0.03, span * gate);
            const double wantAcc = double (a.accent[k]);
            n.accent = (float) clampD (wantAcc < 0.0 ? metric : lerpD (metric, wantAcc, c), 0.05, 1.0);

            // HOLD repeats the previous rung instead of advancing the run - the
            // montuno's landing on beat 4. It is positional, so it has to be
            // decided here rather than in the pre-drawn walk; that means this
            // one pitch decision rides the rhythm stream.
            const bool hold = a.hold[k] && onsetIdx > 0 && rng.nextDouble() < c;

            if (hold)
            {
                n.rung = lastRung;
            }
            else
            {
                n.rung   = rungs[onsetIdx & (kWalkLen - 1)];
                lastRung = n.rung;
                ++onsetIdx;
            }

            if (bar.count < kMaxLeaf)
                bar.notes[bar.count++] = n;
        }
    }

    static void subdivide (SplitMix64& rng, double start, double len,
                           double pBeat, double pEighth,
                           double* outStart, double* outLen, int& count)
    {
        if (count >= kMaxLeaf - 8)
            return;

        double pSplit = 0.0;
        if (len > 0.75)       pSplit = pBeat;      // beat -> two 1/8
        else if (len > 0.375) pSplit = pEighth;    // 1/8  -> two 1/16
        else                  pSplit = 0.0;        // 1/16 is the floor

        if (rng.nextDouble() < pSplit)
        {
            subdivide (rng, start,             len * 0.5, pBeat, pEighth, outStart, outLen, count);
            subdivide (rng, start + len * 0.5, len * 0.5, pBeat, pEighth, outStart, outLen, count);
        }
        else
        {
            outStart[count] = start;
            outLen  [count] = len;
            ++count;
        }
    }
};

// ---------------------------------------------------------------------------
// SeededArpCore - holds the baked pattern and plays it through the locators
// ---------------------------------------------------------------------------
class SeededArpCore
{
public:
    static constexpr int kMaxHeldNotes = 128;

    SeededArpCore()
    {
        held.reserve (kMaxHeldNotes);
        pool.reserve (kMaxHeldNotes);
        PatternBaker::bake (params, pattern);
        bakeHash = hashParams (params);
    }

    ArpParams params;

    void reset() noexcept
    {
        held.clear();
        pool.clear();
        running        = false;
        anchored       = false;
        holdActive     = false;
        origin         = 0.0;
        startBeat      = 0.0;
        chordStartBeat = 0.0;
        playPos    = 0.0f;
    }

    void noteOn (int note, float velocity, double atBeat)
    {
        note = clampInt (note, 0, 127);

        for (auto& h : held)
            if (h.note == note) { h.velocity = velocity; rebuildPool(); return; }

        const bool wasEmpty = pool.empty();
        if ((int) held.size() >= kMaxHeldNotes)
            return;                                   // keeps the no-allocation guarantee

        held.push_back ({ note, velocity });
        rebuildPool();

        if (! wasEmpty && running && std::abs (atBeat - chordStartBeat) < 1.0e-9)
        {
            // another key on the very same sample: the chord is simultaneous,
            // so the pool is already complete and there is nothing to wait for
            startBeat = chordStartBeat;
        }

        if (wasEmpty)
        {
            running        = true;
            chordStartBeat = atBeat;
            startBeat      = atBeat + std::max (0.0, double (params.chordGatherBeats));

            switch (params.restart)
            {
                case RestartMode::Chord:
                    origin = startBeat;
                    break;

                case RestartMode::Free:
                    if (! anchored)                 // only the first chord anchors it
                    {
                        origin   = startBeat;
                        anchored = true;
                    }
                    break;

                case RestartMode::Song:
                default:
                    origin = 0.0;                   // pattern bar 1 == song bar 1
                    break;
            }
        }
    }

    void noteOff (int note) noexcept
    {
        note = clampInt (note, 0, 127);
        for (size_t i = 0; i < held.size(); ++i)
            if (held[i].note == note) { held.erase (held.begin() + (long) i); break; }

        rebuildPool();
        if (pool.empty())
            running = false;
    }

    bool isRunning() const noexcept { return running && ! pool.empty(); }

    // Drops the free-run anchor without disturbing held notes, the pool or the
    // baked pattern. RestartMode::Free anchors on the FIRST chord and every
    // chord after that inherits its phase; clearing the anchor makes the next
    // chord set a new origin, so the pattern starts from its beginning again.
    // The host layer uses this for the idle restart - see CONTINUOUS.
    void clearAnchor() noexcept { anchored = false; }

    // HOLD locks the sequence to one bar, exactly as if the locators had been
    // closed around it. Origin is rewritten so the pattern does not jump at the
    // moment of the press or the release - it simply stops advancing past the
    // end of that bar, and carries on from where it stood when let go.
    void setHoldBar (bool active, double beatsPerBar, double atBeat) noexcept
    {
        if (active == holdActive)
            return;

        double ls = 0.0, le = 0.0;
        getLoop (ls, le);
        const double pos = clampD (double (playPos), ls, le);

        if (active)
        {
            const double bar = std::max (1.0, beatsPerBar);
            holdStartBeats = ls + std::floor ((pos - ls) / bar) * bar;
        }

        holdActive = active;

        // keep the phase across the change of loop
        double ns = 0.0, ne = 0.0;
        getLoop (ns, ne);
        const double len = ne - ns;

        if (len > 1.0e-6)
            origin = atBeat - clampD (pos - ns, 0.0, len);
    }

    // Called by the host layer when the transport jumps (DAW loop, rewind,
    // locate). Without this the pattern keeps counting from the old anchor and
    // lands on an arbitrary position after the jump - bars appear to be
    // skipped. Restart-on-chord restarts the pattern at the jump point;
    // song-aligned mode needs no anchor at all.
    void reanchor (double atBeat) noexcept
    {
        if (! running)
            return;

        startBeat = std::min (startBeat, atBeat);   // never re-gather on a jump

        if (params.restart == RestartMode::Song)
            return;                                  // already timeline-locked

        // Carry the pattern's phase across the jump. Restarting here is what
        // made a 4-bar pattern unable to get past a 2-bar DAW loop.
        double loopStart = 0.0, loopEnd = 0.0;
        getLoop (loopStart, loopEnd);
        const double loopLen = loopEnd - loopStart;

        if (loopLen <= 1.0e-6)
        {
            origin = atBeat;
            return;
        }

        const double phase = std::max (0.0, double (playPos) - loopStart);
        origin = atBeat - phase;
    }

    // Re-bakes when a bake-relevant parameter changed. Returns true if it did.
    bool rebakeIfNeeded()
    {
        const uint64_t h = hashParams (params);
        if (h == bakeHash)
            return false;

        PatternBaker::bake (params, pattern);
        bakeHash = h;
        return true;
    }

    const PatternSnapshot& getPattern() const noexcept { return pattern; }
    float getPlayPosition() const noexcept             { return playPos; }

    void fillChordSnapshot (ChordSnapshot& cs) const noexcept
    {
        cs.size      = std::min (16, (int) pool.size());
        cs.octaves   = clampInt (params.octaveRange, 1, 4);
        cs.semitones = params.semitones;
        for (int i = 0; i < cs.size; ++i)
            cs.notes[i] = pool[(size_t) i].note;
    }

    // Rung -> MIDI note. Shared by the player and the piano-roll view so both
    // always agree. Squashes the nominal ladder onto the actual chord ladder,
    // which is why changing chord never changes the rhythm.
    static int resolveRung (int rung, int nominalLadder,
                            const int* poolNotes, int poolSize,
                            int octaves, int semitones)
    {
        if (poolSize <= 0)
            return -1;

        const int actual = std::max (1, poolSize * std::max (1, octaves));
        int mapped = 0;

        if (nominalLadder > 1 && actual > 1)
            mapped = (int) std::lround (double (rung) * double (actual - 1) / double (nominalLadder - 1));

        mapped = clampInt (mapped, 0, actual - 1);
        return clampInt (poolNotes[mapped % poolSize] + (mapped / poolSize) * semitones, 0, 127);
    }

    void process (double fromBeat, double toBeat, std::vector<ArpEvent>& out)
    {
        if (! running || pool.empty() || pattern.count <= 0)
            return;

        double loopStart = 0.0, loopEnd = 0.0;
        getLoop (loopStart, loopEnd);
        const double loopLen = loopEnd - loopStart;
        if (loopLen <= 1.0e-6)
            return;

        // Nothing plays before the anchor: this covers the chord-gather window
        // and any window that starts earlier than the press (a backward
        // transport jump), which used to wrap round to the END of the pattern.
        double b = std::max (fromBeat, startBeat);
        if (b >= toBeat - 1.0e-9)
            return;

        for (int guard = 0; guard < 64 && b < toBeat - 1.0e-9; ++guard)
        {
            double pos  = mapToPattern (b, loopStart, loopLen);
            double room = loopEnd - pos;

            if (room <= 1.0e-9)          // landed exactly on the loop end: wrap now
            {
                pos  = loopStart;
                room = loopLen;
            }

            const double segLen = std::min (toBeat - b, room);

            if (segLen <= 1.0e-9)
                break;

            for (int i = 0; i < pattern.count; ++i)
            {
                const PatternNote& n = pattern.notes[i];
                if (n.startBeat < pos - 1.0e-9 || n.startBeat >= pos + segLen - 1.0e-9)
                    continue;

                const int note = nextNoteFor (n);
                if (note < 0)
                    continue;

                ArpEvent ev;
                ev.note        = note;
                ev.velocity    = clampF (poolVelocity() * n.accent, 0.02f, 1.0f);
                ev.startBeat   = b + (double (n.startBeat) - pos);
                ev.lengthBeats = std::max (0.02, std::min (double (n.lengthBeats),
                                                           loopEnd - double (n.startBeat)));
                out.push_back (ev);
            }

            b += segLen;
        }

        playPos = (float) mapToPattern (std::max (startBeat, toBeat - 1.0e-6), loopStart, loopLen);
    }

private:
    struct HeldNote { int note; float velocity; };

    std::vector<HeldNote> held;
    std::vector<HeldNote> pool;

    PatternSnapshot pattern;
    uint64_t bakeHash   = 0;
    bool     running    = false;
    bool     anchored    = false;   // Free mode: has the first chord anchored it yet
    double   origin     = 0.0;
    double   startBeat  = 0.0;   // earliest beat this chord may sound
    double   chordStartBeat = 0.0;
    float    playPos    = 0.0f;

    bool     holdActive     = false;   // HOLD: loop just the bar that was playing
    double   holdStartBeats = 0.0;

    static int    clampInt (int v, int lo, int hi) noexcept       { return v < lo ? lo : (v > hi ? hi : v); }
    static float  clampF   (float v, float lo, float hi) noexcept { return v < lo ? lo : (v > hi ? hi : v); }
    static double clampD   (double v, double lo, double hi) noexcept { return v < lo ? lo : (v > hi ? hi : v); }

    float poolVelocity() const noexcept { return pool.empty() ? 0.8f : pool.front().velocity; }

    void rebuildPool()
    {
        pool = held;
        if (params.sortNotes)
            std::sort (pool.begin(), pool.end(),
                       [] (const HeldNote& a, const HeldNote& b) { return a.note < b.note; });
    }

    void getLoop (double& startOut, double& endOut) const noexcept
    {
        const double total = std::max (1.0, double (pattern.totalBeats));

        if (holdActive)
        {
            const double bar = std::max (1.0, double (params.beatsPerBar));
            startOut = clampD (holdStartBeats, 0.0, std::max (0.0, total - 0.25));
            endOut   = std::min (total, startOut + bar);
            return;
        }

        double s = clampD (double (params.loopStartBeats), 0.0, total);
        double e = clampD (double (params.loopEndBeats),   0.0, total);

        if (e - s < 0.25)                       // never smaller than a 1/4 note
            e = std::min (total, s + 0.25);
        if (e - s < 0.25)
            s = std::max (0.0, e - 0.25);

        startOut = s;
        endOut   = e;
    }

    double mapToPattern (double beat, double loopStart, double loopLen) const noexcept
    {
        const double delta = beat - origin;
        if (delta <= 0.0)
            return loopStart;               // never wrap backwards into the tail

        double d = std::fmod (delta, loopLen);
        if (d < 0.0)
            d = 0.0;
        return loopStart + d;
    }

    // Seeded mode uses the baked rung; ordered modes walk the ladder instead,
    // so Up / Down / Up-Down inherit the musical rhythm too.
    int nextNoteFor (const PatternNote& n)
    {
        const int poolSize = (int) pool.size();
        if (poolSize <= 0)
            return -1;

        int poolNotes[kMaxHeldNotes];
        const int usable = std::min (poolSize, kMaxHeldNotes);
        for (int i = 0; i < usable; ++i)
            poolNotes[i] = pool[(size_t) i].note;

        const int octaves = clampInt (params.octaveRange, 1, 4);

        return resolveRung (n.rung, pattern.nominalLadder, poolNotes, usable,
                            octaves, params.semitones);
    }

    static uint64_t hashParams (const ArpParams& p)
    {
        auto mix = [] (uint64_t h, uint64_t v)
        {
            h ^= v + 0x9E3779B97F4A7C15ull + (h << 6) + (h >> 2);
            return h;
        };
        auto bits = [] (float f)
        {
            uint32_t u = 0;
            std::memcpy (&u, &f, sizeof (u));
            return uint64_t (u);
        };

        uint64_t h = 0xCBF29CE484222325ull;
        h = mix (h, uint64_t (p.seed));
        h = mix (h, uint64_t (bakeBarsFor (p)));
        h = mix (h, uint64_t (p.octaveRange));
        h = mix (h, uint64_t (p.semitones));
        h = mix (h, bits (p.weight16));
        h = mix (h, bits (p.weight8));
        h = mix (h, bits (p.weight4));
        h = mix (h, bits (p.restProb));
        h = mix (h, bits (p.gateMin));
        h = mix (h, bits (p.gateMax));
        h = mix (h, bits (p.complexity));
        h = mix (h, uint64_t (p.idiom));
        h = mix (h, bits (p.originality));
        h = mix (h, bits (p.beatsPerBar));
        return h;
    }
};

} // namespace fanan



