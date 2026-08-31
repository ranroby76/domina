
// ============================================================================
//  C:\workspace\Domina\src\DebugTrace.h
//  Domina - seeded MIDI arpeggiator (Fanan)
//
//  TEMPORARY DIAGNOSTIC. Set DOMINA_TRACE to 0 before shipping.
//
//  Writes a line per lifecycle event to
//
//      Windows   %APPDATA%\Fanan\Domina_trace.txt
//      macOS     ~/Library/Application Support/Fanan/Domina_trace.txt
//
//  and, if the host process faults, appends a stack backtrace to the same file.
//  When a host reports a plugin as misbehaving it will not say WHERE, so the
//  point of this is simply that the last line written is the last thing Domina
//  managed to do.
//
//  Nothing here is called from the audio thread. File I/O from processBlock
//  would itself cause dropouts and be reported as misbehaviour, which would be
//  a memorable way to waste an afternoon.
// ============================================================================
#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

#ifndef DOMINA_TRACE
 #define DOMINA_TRACE 1
#endif

namespace DominaTrace
{
   #if DOMINA_TRACE

    inline juce::File traceFile()
    {
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                 .getChildFile ("Fanan")
                 .getChildFile ("Domina_trace.txt");
    }

    // Hosts keep more than one instance alive at a time - a scan, a second
    // track, the same plugin in two formats - and without an id per instance
    // the log reads as one confused timeline. Cheap, and it settles questions
    // like "did that editor outlive its processor or belong to the other one".
    inline int nextInstanceId()
    {
        static std::atomic<int> counter { 0 };
        return ++counter;
    }

    inline void log (const juce::String& what)
    {
        const auto f = traceFile();
        f.getParentDirectory().createDirectory();

        // Opened and closed per line on purpose: a held stream loses whatever
        // is buffered at exactly the moment we care about, the crash.
        f.appendText (juce::Time::getCurrentTime().toString (true, true, true, true)
                        + "  [" + juce::String::toHexString ((juce::pointer_sized_int)
                              juce::Thread::getCurrentThreadId()) + "]  "
                        + what + juce::newLine);
    }

    inline void installCrashHandler()
    {
        static bool done = false;          // plain bool: no destructor to run at unload
        if (done)
            return;

        done = true;

        juce::SystemStats::setApplicationCrashHandler ([] (void*)
        {
            const auto f = traceFile();
            f.getParentDirectory().createDirectory();
            f.appendText (juce::newLine + "*** CRASH ***" + juce::newLine
                            + juce::SystemStats::getStackBacktrace() + juce::newLine);
        });

        log (juce::String ("--- trace started, Domina ") + JucePlugin_VersionString
               + ", note trace ACTIVE, built " __DATE__ " " __TIME__ " ---");
    }

   #else
    inline void log (const juce::String&) {}
    inline void installCrashHandler()      {}
   #endif
}

#if DOMINA_TRACE
 #define DOMINA_LOG(x)  DominaTrace::log (x)
#else
 #define DOMINA_LOG(x)
#endif
