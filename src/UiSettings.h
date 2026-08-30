
// ============================================================================
//  C:\workspace\Domina\src\UiSettings.h
//  Domina - seeded MIDI arpeggiator (Fanan)
//
//  Settings that belong to the INSTALLATION rather than to a project. The
//  accent colour is the first of them: pick a colour once and every future
//  instance opens that way, in any project, until it is changed again.
//
//  DELIBERATELY NOT juce::PropertiesFile, and definitely not a static one.
//  PropertiesFile privately inherits Timer and auto-saves on a delay, so a
//  function-local static would outlive the plugin instance and be destroyed at
//  DLL unload - after the MessageManager may already be gone. A host that loads
//  and unloads plugins repeatedly, which is every host, would fault repeatedly.
//
//  So: nothing persists between calls. Two file operations on the message
//  thread, on a file measured in bytes, only when the user clicks an LED.
//
//      Windows   %APPDATA%\Fanan\Domina.settings
//      macOS     ~/Library/Application Support/Fanan/Domina.settings
// ============================================================================
#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

namespace DominaSettings
{
    inline juce::File settingsFile()
    {
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                 .getChildFile ("Fanan")
                 .getChildFile ("Domina.settings");
    }

    inline int getAccent()
    {
        if (auto xml = juce::XmlDocument::parse (settingsFile()))
            return juce::jlimit (0, 15, xml->getIntAttribute ("uiAccent", 0));

        return 0;
    }

    inline void setAccent (int index)
    {
        const auto f = settingsFile();
        f.getParentDirectory().createDirectory();

        // Read-modify-write, so a future setting sharing this file is not wiped
        // by whichever one happens to be saved second.
        auto xml = juce::XmlDocument::parse (f);
        if (xml == nullptr)
            xml = std::make_unique<juce::XmlElement> ("DominaSettings");

        xml->setAttribute ("uiAccent", index);
        xml->writeTo (f);
    }
}
