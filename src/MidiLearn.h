
// ============================================================================
//  C:\workspace\Domina\src\MidiLearn.h
//  Domina - MIDI learn registry  (Fanan)
//
//  TWO WAYS TO BIND, ONE TABLE BEHIND BOTH.
//
//    Drag and drop  - move a control on your controller, its CC appears in the
//                     monitor chip, drag that chip onto anything in Domina.
//    Right-click    - Learn on the control itself, then move the controller.
//
//  Drag and drop is the better of the two because you can see what you are
//  about to bind before you bind it; arming is there for controls that are
//  awkward to reach with a drag.
//
//  THREADING. The audio thread never touches a juce::String. It reads
//  ccToSlot[] - a plain array of 128 atomics - writes a pending value into the
//  slot it finds, and stops. Everything about WHAT a slot means (which
//  parameter, which action) is owned by the GUI thread, which polls the pending
//  values on a timer and applies them with setValueNotifyingHost. That keeps
//  parameter changes on the message thread where the host expects them, and
//  keeps the callback free of locks and allocation.
//
//  MATCHING IS OMNI. The channel a CC arrived on is recorded for display but
//  not used to match, because controllers get moved between channels and a
//  binding that silently stops working is worse than one that is slightly too
//  eager.
// ============================================================================

#pragma once

#include <juce_core/juce_core.h>
#include <atomic>

namespace fanan
{

class MidiLearn
{
public:
    static constexpr int kMaxSlots = 64;

    // Pseudo-ids for things that are not APVTS parameters. The '@' prefix can
    // never collide with a real parameter id.
    static constexpr const char* kSeedRandom = "@seedRandom";

    MidiLearn()
    {
        for (int i = 0; i < 128; ++i)
            ccToSlot[i].store (-1);

        for (int i = 0; i < kMaxSlots; ++i)
        {
            slotCc[i].store (-1);
            pendingDirty[i].store (false);
            pendingValue[i].store (0.0f);
        }
    }

    // ---- audio thread -------------------------------------------------------
    void handleCc (int channel, int ccNum, int value) noexcept
    {
        if (ccNum < 0 || ccNum > 127)
            return;

        lastCcNum.store (ccNum, std::memory_order_relaxed);
        lastChan.store  (channel, std::memory_order_relaxed);
        lastVal.store   (value, std::memory_order_relaxed);
        lastStamp.fetch_add (1, std::memory_order_release);

        const int armed = armedSlot.load (std::memory_order_acquire);
        if (armed >= 0 && armed < kMaxSlots)
        {
            const int previousOwner = ccToSlot[ccNum].exchange (armed);
            if (previousOwner >= 0 && previousOwner != armed)
                slotCc[previousOwner].store (-1);

            const int oldCc = slotCc[armed].exchange (ccNum);
            if (oldCc >= 0 && oldCc != ccNum)
                ccToSlot[oldCc].store (-1);

            armedSlot.store (-1, std::memory_order_release);
            armDone.store (true, std::memory_order_release);
            return;
        }

        const int slot = ccToSlot[ccNum].load (std::memory_order_acquire);
        if (slot < 0 || slot >= kMaxSlots)
            return;

        pendingValue[slot].store ((float) value / 127.0f, std::memory_order_relaxed);
        pendingDirty[slot].store (true, std::memory_order_release);
    }

    // ---- GUI thread: binding -----------------------------------------------
    int slotFor (const juce::String& id) const
    {
        for (int i = 0; i < used; ++i)
            if (slotId[i] == id)
                return i;
        return -1;
    }

    int ccFor (const juce::String& id) const
    {
        const int s = slotFor (id);
        return s < 0 ? -1 : slotCc[s].load();
    }

    bool assign (const juce::String& id, int ccNum)
    {
        if (ccNum < 0 || ccNum > 127)
            return false;

        const int slot = allocSlot (id);
        if (slot < 0)
            return false;

        const int previousOwner = ccToSlot[ccNum].exchange (slot);
        if (previousOwner >= 0 && previousOwner != slot)
            slotCc[previousOwner].store (-1);

        const int oldCc = slotCc[slot].exchange (ccNum);
        if (oldCc >= 0 && oldCc != ccNum)
            ccToSlot[oldCc].store (-1);

        return true;
    }

    void unassign (const juce::String& id)
    {
        const int slot = slotFor (id);
        if (slot < 0)
            return;

        const int cc = slotCc[slot].exchange (-1);
        if (cc >= 0)
            ccToSlot[cc].store (-1);
    }

    // ---- GUI thread: arm-and-wait ------------------------------------------
    void arm (const juce::String& id)
    {
        const int slot = allocSlot (id);
        if (slot < 0)
            return;

        armedId_ = id;
        armDone.store (false);
        armedSlot.store (slot, std::memory_order_release);
    }

    void cancelArm()
    {
        armedSlot.store (-1, std::memory_order_release);
        armedId_ = {};
    }

    bool isArmed (const juce::String& id) const
    {
        return armedSlot.load() >= 0 && armedId_ == id;
    }

    bool isArmedAny() const { return armedSlot.load() >= 0; }

    // True once, when an armed target has just caught a CC.
    bool consumeArmDone()
    {
        if (! armDone.exchange (false))
            return false;

        armedId_ = {};
        return true;
    }

    // ---- GUI thread: pending values ----------------------------------------
    int numSlots() const { return used; }
    const juce::String& idAt (int slot) const { return slotId[slot]; }

    bool consume (int slot, float& value01)
    {
        if (slot < 0 || slot >= used)
            return false;

        if (! pendingDirty[slot].exchange (false))
            return false;

        value01 = pendingValue[slot].load();
        return true;
    }

    // ---- monitor ------------------------------------------------------------
    int lastCc()      const { return lastCcNum.load (std::memory_order_relaxed); }
    int lastChannel() const { return lastChan.load  (std::memory_order_relaxed); }
    int lastValue()   const { return lastVal.load   (std::memory_order_relaxed); }
    uint32_t lastStampValue() const { return lastStamp.load (std::memory_order_acquire); }

    // ---- state --------------------------------------------------------------
    juce::String toString() const
    {
        juce::String s;
        for (int i = 0; i < used; ++i)
        {
            const int cc = slotCc[i].load();
            if (cc < 0)
                continue;

            if (s.isNotEmpty())
                s << ";";

            s << slotId[i] << ":" << juce::String (cc);
        }
        return s;
    }

    void fromString (const juce::String& s)
    {
        for (int i = 0; i < 128; ++i)
            ccToSlot[i].store (-1);
        for (int i = 0; i < kMaxSlots; ++i)
            slotCc[i].store (-1);

        used = 0;

        juce::StringArray entries;
        entries.addTokens (s, ";", "");

        for (const auto& e : entries)
        {
            if (e.isEmpty())
                continue;

            const auto id = e.upToFirstOccurrenceOf (":", false, false);
            const auto cc = e.fromFirstOccurrenceOf (":", false, false).getIntValue();

            if (id.isNotEmpty())
                assign (id, cc);
        }
    }

private:
    // Slots are never freed: a control that was bound once is likely to be bound
    // again, and reusing its slot keeps the pending-value indices stable.
    int allocSlot (const juce::String& id)
    {
        const int existing = slotFor (id);
        if (existing >= 0)
            return existing;

        if (used >= kMaxSlots)
            return -1;

        slotId[used] = id;
        return used++;
    }

    juce::String     slotId[kMaxSlots];
    int              used = 0;

    std::atomic<int>   ccToSlot[128];
    std::atomic<int>   slotCc[kMaxSlots];
    std::atomic<float> pendingValue[kMaxSlots];
    std::atomic<bool>  pendingDirty[kMaxSlots];

    std::atomic<int>  armedSlot { -1 };
    std::atomic<bool> armDone   { false };
    juce::String      armedId_;

    std::atomic<int>      lastCcNum { -1 };
    std::atomic<int>      lastChan  { 0 };
    std::atomic<int>      lastVal   { 0 };
    std::atomic<uint32_t> lastStamp { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MidiLearn)
};

} // namespace fanan



