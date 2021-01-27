/*
  ==============================================================================

    HarmonizerMidi.cpp
    Created: 22 Jan 2021 1:50:21am
    Author:  Ben Vining

  ==============================================================================
*/

#include "HarmonizerDSP/Harmonizer.h"


template<typename SampleType>
void Harmonizer<SampleType>::turnOffAllKeyupNotes (const bool allowTailOff,
                                                   const bool includePedalPitchAndDescant)
{
    Array< HarmonizerVoice<SampleType>* > toTurnOff;
    
    toTurnOff.ensureStorageAllocated (voices.size());
    
    for (auto* voice : voices)
    {
        if (voice->isVoiceActive() && ! voice->isKeyDown())
        {
            if (includePedalPitchAndDescant)
            {
                toTurnOff.add (voice);
                continue;
            }
            
            const int note = voice->getCurrentlyPlayingNote();
            
            if (note != lastPedalPitch && note != lastDescantPitch)
                toTurnOff.add (voice);
        }
    }
    
    if (toTurnOff.isEmpty())
        return;
        
    const float velocity = allowTailOff ? 0.0f : 1.0f;
    
    for (auto* voice : toTurnOff)
    {
        aggregateMidiBuffer.addEvent (MidiMessage::noteOff (lastMidiChannel, voice->getCurrentlyPlayingNote(), velocity),
                                      ++lastMidiTimeStamp);
        
        stopVoice (voice, velocity, allowTailOff);
    }
    
    if (includePedalPitchAndDescant)
    {
        lastPedalPitch = -1;
        lastDescantPitch = -1;
    }
};


// MIDI events --------------------------------------------------------------------------------------------------------------------------------------

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// KEYBOARD / PLUGIN MIDI INPUT --------------------------------------------------------------------------------------------------------------------
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

template<typename SampleType>
void Harmonizer<SampleType>::processMidi (MidiBuffer& midiMessages, const bool returnMidiOutput)
{
    aggregateMidiBuffer.clear();
    
    auto midiIterator = midiMessages.findNextSamplePosition(0);
    
    if (midiIterator == midiMessages.cend())
    {
        lastMidiTimeStamp = -1;
        return;
    }
    
    lastMidiTimeStamp = 0;
    
    std::for_each (midiIterator,
                   midiMessages.cend(),
                   [&] (const MidiMessageMetadata& meta)
                   {
                       handleMidiEvent (meta.getMessage(), meta.samplePosition, returnMidiOutput);
                   } );
    
    pitchCollectionChanged();
    
    if (returnMidiOutput)
        midiMessages.swapWith (aggregateMidiBuffer);
    
    lastMidiTimeStamp = -1;
};


template<typename SampleType>
void Harmonizer<SampleType>::handleMidiEvent (const MidiMessage& m, const int samplePosition, const bool returnMidiOutput)
{
    // events coming from a midi keyboard, or the plugin's midi input, should be routed to this function.
    
    bool shouldAddToAggregateMidiBuffer = true;
    lastMidiChannel   = m.getChannel();
    lastMidiTimeStamp = samplePosition;
    
    if (m.isNoteOn())
        noteOn (m.getNoteNumber(), m.getFloatVelocity(), true);
    else if (m.isNoteOff())
    {
        noteOff (m.getNoteNumber(), m.getFloatVelocity(), true, true);
        shouldAddToAggregateMidiBuffer = (! latchIsOn);
    }
    else if (m.isAllNotesOff() || m.isAllSoundOff())
        allNotesOff (false);
    else if (m.isPitchWheel())
        handlePitchWheel (m.getPitchWheelValue());
    else if (m.isAftertouch())
        handleAftertouch (m.getNoteNumber(), m.getAfterTouchValue());
    else if (m.isChannelPressure())
        handleChannelPressure (m.getChannelPressureValue());
    else if (m.isController())
        handleController (m.getControllerNumber(), m.getControllerValue());
    
    if (returnMidiOutput && shouldAddToAggregateMidiBuffer)
        aggregateMidiBuffer.addEvent (m, samplePosition);
};


//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// AUTOMATED MIDI EVENTS ----------------------------------------------------------------------------------------------------------------------------
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// midi latch: when active, holds note offs recieved from the keyboard instead of sending them immediately; held note offs are sent once latch is deactivated.
template<typename SampleType>
void Harmonizer<SampleType>::setMidiLatch (const bool shouldBeOn, const bool allowTailOff)
{
    if (latchIsOn == shouldBeOn)
        return;
    
    latchIsOn = shouldBeOn;
    
    if (shouldBeOn)
        return;
    
    if (! intervalLatchIsOn || intervalsLatchedTo.isEmpty())
        turnOffAllKeyupNotes (allowTailOff, false);
    else
    {
        // turn off all voices whose key is up and who aren't being held by the interval latch function
        
        const int currentMidiPitch = roundToInt (pitchConverter.ftom (currentInputFreq));
        
        Array<int> intervalLatchNotes;
        intervalLatchNotes.ensureStorageAllocated (intervalsLatchedTo.size());
        
        for (int interval : intervalsLatchedTo)
            intervalLatchNotes.add (currentMidiPitch + interval);
        
        Array< HarmonizerVoice<SampleType>* > toTurnOff;
        toTurnOff.ensureStorageAllocated (voices.size());
        
        for (auto* voice : voices)
        {
            if (voice->isVoiceActive() && ! voice->isKeyDown())
            {
                const int note = voice->getCurrentlyPlayingNote();
                
                if (note != lastPedalPitch && note != lastDescantPitch
                    && ! intervalLatchNotes.contains(note))
                {
                    toTurnOff.add (voice);
                }
            }
        }
        
        if (toTurnOff.isEmpty())
            return;
        
        const float velocity = allowTailOff ? 0.0f : 1.0f;
        
        for (auto* voice : toTurnOff)
        {
            aggregateMidiBuffer.addEvent (MidiMessage::noteOff (lastMidiChannel, voice->getCurrentlyPlayingNote(), velocity),
                                          ++lastMidiTimeStamp);
            
            stopVoice (voice, velocity, allowTailOff);
        }
    }
};


// interval latch
template<typename SampleType>
void Harmonizer<SampleType>::setIntervalLatch (const bool shouldBeOn, const bool allowTailOff)
{
    if (intervalLatchIsOn == shouldBeOn)
        return;
    
    intervalLatchIsOn = shouldBeOn;
    
    if (shouldBeOn)
        updateIntervalsLatchedTo();
    else if (! latchIsOn)
        turnOffAllKeyupNotes (allowTailOff, false);
};


template<typename SampleType>
void Harmonizer<SampleType>::updateIntervalsLatchedTo()
{
    intervalsLatchedTo.clearQuick();
    
    Array<int> currentNotes;
    currentNotes.ensureStorageAllocated (voices.size());
    
    reportActivesNoReleased (currentNotes);
    
    if (currentNotes.isEmpty())
        return;
    
    const int currentMidiPitch = roundToInt (pitchConverter.ftom (currentInputFreq));
    
    for (int note : currentNotes)
        intervalsLatchedTo.add (note - currentMidiPitch);
};


// play chord: send an array of midi pitches into this function and it will ensure that only those desired pitches are being played.
template<typename SampleType>
void Harmonizer<SampleType>::playChord (const Array<int>& desiredPitches,
                                        const float velocity,
                                        const bool allowTailOffOfOld,
                                        const bool isIntervalLatch)
{
    if (desiredPitches.isEmpty())
    {
        allNotesOff (allowTailOffOfOld);
        return;
    }
    
    // create array containing current pitches
    
    Array<int> currentNotes;
    currentNotes.ensureStorageAllocated (voices.size());
    
    reportActivesNoReleased (currentNotes);
    
    // 1. turn off the pitches that were previously on that are not included in the list of desired pitches
    
    if (! currentNotes.isEmpty())
    {
        Array<int> toTurnOff;
        toTurnOff.ensureStorageAllocated (currentNotes.size());
    
        for (int note : currentNotes)
            if (! desiredPitches.contains (note))
                toTurnOff.add (note);
    
        turnOffList (toTurnOff, !allowTailOffOfOld, allowTailOffOfOld, true);
    }

    // 2. turn on the desired pitches that aren't already on
    
    if (currentNotes.isEmpty())
    {
        turnOnList (desiredPitches, velocity, true);
    }
    else
    {
        Array<int> toTurnOn;
        toTurnOn.ensureStorageAllocated (currentNotes.size());
        
        for (int note : desiredPitches)
        {
            if (! currentNotes.contains(note))
                toTurnOn.add (note);
        }
        
        turnOnList (toTurnOn, velocity, true);
    }
    
    if (! isIntervalLatch)
        pitchCollectionChanged();
};


// turn on list: turns on a list of specified notes in quick sequence.
template<typename SampleType>
void Harmonizer<SampleType>::turnOnList (const Array<int>& toTurnOn, const float velocity, const bool partOfChord)
{
    if (toTurnOn.isEmpty())
        return;
    
    for (int note : toTurnOn)
        noteOn (note, velocity, false);
    
    if (! partOfChord)
        pitchCollectionChanged();
};


// turn off list: turns off a list of specified notes in quick sequence.
template<typename SampleType>
void Harmonizer<SampleType>::turnOffList (const Array<int>& toTurnOff, const float velocity, const bool allowTailOff, const bool partOfChord)
{
    if (toTurnOff.isEmpty())
        return;
    
    for (int note : toTurnOff)
        noteOff (note, velocity, allowTailOff, false);
    
    if (! partOfChord)
        pitchCollectionChanged();
};


// automated midi "pedal pitch": creates a polyphonic doubling of the lowest note currently being played by a keyboard key at a specified interval below that keyboard key, IF that keyboard key is below a certain pitch threshold.
template<typename SampleType>
void Harmonizer<SampleType>::applyPedalPitch()
{
    int currentLowest = 128; // find the current lowest note being played by a keyboard key
    
    for (auto* voice : voices)
    {
        if (voice->isVoiceActive() && voice->isKeyDown())
        {
            const int note = voice->getCurrentlyPlayingNote();
            
            if (note < currentLowest)
                currentLowest = voice->getCurrentlyPlayingNote();
        }
    }
    
    if ((currentLowest == 128) || (currentLowest > pedalPitchUpperThresh))
    {
        if (lastPedalPitch > -1)
            noteOff (lastPedalPitch, 1.0f, false, false);
        
        lastPedalPitch = -1;
        return;
    }
    
    const int newPedalPitch = currentLowest - pedalPitchInterval;
    
    if (newPedalPitch == lastPedalPitch)
        return;
    
    if (lastPedalPitch > -1)
        noteOff (lastPedalPitch, 1.0f, false, false);
    
    if (newPedalPitch < 0 || isPitchHeldByKeyboardKey (newPedalPitch))
    {
        lastPedalPitch = -1;
        return;
    }
    
    lastPedalPitch = newPedalPitch;
    
    auto* voiceCopying = getVoicePlayingNote (currentLowest);
    const float velocity = (voiceCopying != nullptr) ? voiceCopying->getLastRecievedVelocity() : 1.0f;
    
    noteOn (newPedalPitch, velocity, false);
};


// automated midi "descant": creates a polyphonic doubling of the highest note currently being played by a keyboard key at a specified interval above that keyboard key, IF that keyboard key is above a certain pitch threshold.
template<typename SampleType>
void Harmonizer<SampleType>::applyDescant()
{
    int currentHighest = -1;
    for (auto* voice : voices)
    {
        if (voice->isVoiceActive() && voice->isKeyDown())
        {
            const int note = voice->getCurrentlyPlayingNote();
            
            if (note > currentHighest)
                currentHighest = voice->getCurrentlyPlayingNote();
        }
    }
    
    if ((currentHighest == -1) || (currentHighest < descantLowerThresh))
    {
        if (lastDescantPitch > -1)
            noteOff (lastDescantPitch, 1.0f, false, false);
        lastDescantPitch = -1;
        return;
    }
    
    const int newDescantPitch = currentHighest + descantInterval;
    
    if (newDescantPitch == lastDescantPitch)
        return;
    
    if (lastDescantPitch > -1)
        noteOff (lastDescantPitch, 1.0f, false, false);
    
    if (newDescantPitch > 127 || isPitchHeldByKeyboardKey (newDescantPitch))
    {
        lastDescantPitch = -1;
        return;
    }
    
    lastDescantPitch = newDescantPitch;
    
    auto* voiceCopying = getVoicePlayingNote (currentHighest);
    const float velocity = (voiceCopying != nullptr) ? voiceCopying->getLastRecievedVelocity() : 1.0f;
    
    noteOn (newDescantPitch, velocity, false);
};


// this function should be called once after each time the harmonizer's overall pitch collection has changed - so, after a midi buffer of keyboard inout events has been processed, or after a chord has been triggered, etc.
template<typename SampleType>
void Harmonizer<SampleType>::pitchCollectionChanged()
{
    if (pedalPitchIsOn)
        applyPedalPitch();
    
    if (descantIsOn)
        applyDescant();
    
    if (intervalLatchIsOn)
        updateIntervalsLatchedTo();
};


// functions for propogating midi events to HarmonizerVoices ----------------------------------------------------------------------------------------

template<typename SampleType>
void Harmonizer<SampleType>::noteOn (const int midiPitch, const float velocity, const bool isKeyboard)
{
    // N.B. the `isKeyboard` flag should be true if this note on event was triggered directly from the midi keyboard input; this flag is false if this note on event was triggered automatically by pedal pitch or descant.
    
    if (isPitchActive (midiPitch, false))
        return;
    
    bool isStealing = isKeyboard ? shouldStealNotes : false; // never steal voices for automated note events, only for keyboard triggered events
    
    startVoice (findFreeVoice (midiPitch, isStealing), midiPitch, velocity, isKeyboard);
};


template<typename SampleType>
void Harmonizer<SampleType>::startVoice (HarmonizerVoice<SampleType>* voice, const int midiPitch, const float velocity, const bool isKeyboard)
{
    if (voice == nullptr)
        return;
    
    voice->setNoteOnTime (++lastNoteOnCounter);
    
    if (! voice->isKeyDown()) // if the key wasn't already marked as down...
        voice->setKeyDown (isKeyboard); // then mark it as down IF this start command is because of a keyboard event
    
    const bool wasStolen = voice->isVoiceActive(); // we know the voice is being "stolen" from another note if it was already on before getting this start command
    
    if (midiPitch < lowestPannedNote)
        voice->setPan(64);
    else if (! wasStolen) // don't change pan if voice was stolen
        voice->setPan(panner.getNextPanVal());
    
    voice->startNote (midiPitch, velocity, wasStolen);
    
    if (! isKeyboard)
        aggregateMidiBuffer.addEvent (MidiMessage::noteOn (lastMidiChannel, midiPitch, velocity),
                                      ++lastMidiTimeStamp);
};

template<typename SampleType>
void Harmonizer<SampleType>::noteOff (const int midiNoteNumber, const float velocity,
                                      const bool allowTailOff,
                                      const bool isKeyboard)
{
    // N.B. the `isKeyboard` flag should be true if this note off event was triggered directly from the midi keyboard input; this flag is false if this note off event was triggered automatically by pedal pitch or descant.
    // N.B. the `partofList` flag should be false if this is a singular note off event; this flag should be true if this is a note off event in a known sequence of many quick note offs, so that pedal pitch & descant will not be updated after every single one of these events.
    
    auto* voice = getVoicePlayingNote (midiNoteNumber);
    
    if (voice == nullptr)
        return;
    
    bool stoppedVoice = false;
    
    if (isKeyboard)
    {
        voice->setKeyDown (false);
        
        if (latchIsOn)
            return;
        
        if (! (sustainPedalDown || sostenutoPedalDown) )
            stoppedVoice = true;
    }
    else if (! voice->isKeyDown())
    {
        stoppedVoice = true;
        aggregateMidiBuffer.addEvent (MidiMessage::noteOff (lastMidiChannel, midiNoteNumber, velocity),
                                      ++lastMidiTimeStamp);
    }
    
    if (! stoppedVoice)
        return;
    
    stopVoice (voice, velocity, allowTailOff);
    
    if (midiNoteNumber == lastDescantPitch)
        lastDescantPitch = -1;
    
    if (midiNoteNumber == lastPedalPitch)
        lastPedalPitch = -1;
};

template<typename SampleType>
void Harmonizer<SampleType>::stopVoice (HarmonizerVoice<SampleType>* voice, const float velocity, const bool allowTailOff)
{
    if (voice == nullptr)
        return;
    
    voice->stopNote (velocity, allowTailOff);
};

template<typename SampleType>
void Harmonizer<SampleType>::allNotesOff (const bool allowTailOff)
{
    for (auto* voice : voices)
        if (voice->isVoiceActive())
            voice->stopNote (1.0f, allowTailOff);
    
    panner.reset(false);
    lastDescantPitch = -1;
    lastPedalPitch   = -1;
};


template<typename SampleType>
void Harmonizer<SampleType>::handlePitchWheel (const int wheelValue)
{
    if (lastPitchWheelValue == wheelValue)
        return;
    
    lastPitchWheelValue = wheelValue;
    bendTracker.newPitchbendRecieved(wheelValue);
    
    for (auto* voice : voices)
        if (voice->isVoiceActive())
            voice->setCurrentOutputFreq (getOutputFrequency (voice->getCurrentlyPlayingNote()));
};


template<typename SampleType>
void Harmonizer<SampleType>::handleAftertouch (const int midiNoteNumber, const int aftertouchValue)
{
    for (auto* voice : voices)
        if (voice->getCurrentlyPlayingNote() == midiNoteNumber)
            voice->aftertouchChanged (aftertouchValue);
};

template<typename SampleType>
void Harmonizer<SampleType>::handleChannelPressure (const int channelPressureValue)
{
    for (auto* voice : voices)
        voice->aftertouchChanged(channelPressureValue);
};

template<typename SampleType>
void Harmonizer<SampleType>::handleController (const int controllerNumber, const int controllerValue)
{
    switch (controllerNumber)
    {
        case 0x1:   handleModWheel        (controllerValue);        return;
        case 0x2:   handleBreathController(controllerValue);        return;
        case 0x4:   handleFootController  (controllerValue);        return;
        case 0x5:   handlePortamentoTime  (controllerValue);        return;
        case 0x7:   handleMainVolume      (controllerValue);        return;
        case 0x8:   handleBalance         (controllerValue);        return;
        case 0x40:  handleSustainPedal    (controllerValue >= 64);  return;
        case 0x42:  handleSostenutoPedal  (controllerValue >= 64);  return;
        case 0x43:  handleSoftPedal       (controllerValue >= 64);  return;
        case 0x44:  handleLegato          (controllerValue >= 64);  return;
        default:    return;
    }
};

template<typename SampleType>
void Harmonizer<SampleType>::handleSustainPedal (const bool isDown)
{
    if (sustainPedalDown == isDown)
        return;
    
    sustainPedalDown = isDown;
    
    if (isDown || latchIsOn || intervalLatchIsOn)
        return;
    
    turnOffAllKeyupNotes (false, false);
};

template<typename SampleType>
void Harmonizer<SampleType>::handleSostenutoPedal (const bool isDown)
{
    if (sostenutoPedalDown == isDown)
        return;
    
    sostenutoPedalDown = isDown;
    
    if (isDown || latchIsOn || intervalLatchIsOn)
        return;
    
    turnOffAllKeyupNotes (false, false);
};

template<typename SampleType>
void Harmonizer<SampleType>::handleSoftPedal (const bool isDown)
{
    if (softPedalDown == isDown)
        return;
    
    softPedalDown = isDown;
};

template<typename SampleType>
void Harmonizer<SampleType>::handleModWheel (const int wheelValue)
{
    ignoreUnused(wheelValue);
};

template<typename SampleType>
void Harmonizer<SampleType>::handleBreathController (const int controlValue)
{
    ignoreUnused(controlValue);
};

template<typename SampleType>
void Harmonizer<SampleType>::handleFootController (const int controlValue)
{
    ignoreUnused(controlValue);
};

template<typename SampleType>
void Harmonizer<SampleType>::handlePortamentoTime (const int controlValue)
{
    ignoreUnused(controlValue);
};

template<typename SampleType>
void Harmonizer<SampleType>::handleMainVolume (const int controlValue)
{
    ignoreUnused(controlValue);
};

template<typename SampleType>
void Harmonizer<SampleType>::handleBalance (const int controlValue)
{
    ignoreUnused(controlValue);
};

template<typename SampleType>
void Harmonizer<SampleType>::handleLegato (const bool isOn)
{
    ignoreUnused(isOn);
};


template class Harmonizer<float>;
template class Harmonizer<double>;
