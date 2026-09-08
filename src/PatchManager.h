
// ============================================================================
//  C:\workspace\Domina\src\PatchManager.h
//  Domina - seeded MIDI arpeggiator (Fanan)
//
//  The .dompatch format. XML, because a patch anyone can open in a text editor
//  is a patch anyone can post on a forum, diff, or fix by hand.
//
//  A patch and the state the DAW saves in its project are THE SAME TREE. They
//  are produced by one function and consumed by one function - see
//  DominaAudioProcessor::captureState / applyState. Two serialisers for one
//  thing is how a format ends up loading patches that were saved by an older
//  build of itself and quietly dropping half of them.
// ============================================================================
#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

namespace DominaPatch
{
    inline juce::String extension()   { return ".dompatch"; }
    inline juce::String wildcard()    { return "*.dompatch"; }
    inline juce::String rootTag()     { return "DominaPatch"; }

    // Documents, not Application Support: patches are the user's work, so they
    // belong somewhere they can find them, back them up and mail them.
    inline juce::File folder()
    {
        auto f = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                   .getChildFile ("Fanan")
                   .getChildFile ("Domina")
                   .getChildFile ("Patches");

        f.createDirectory();
        return f;
    }

    inline juce::File withExtension (const juce::File& f)
    {
        return f.hasFileExtension (extension()) ? f : f.withFileExtension (extension());
    }
}
