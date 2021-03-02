/*
    Part of module: bv_ImogenEngine
    Direct parent file: bv_ImogenEngine.h
*/


#include "bv_ImogenEngine/bv_ImogenEngine.h"



namespace bav

{
    

template<typename SampleType>
ImogenEngine<SampleType>::ImogenEngine():
    internalBlocksize(512),
    limiterIsOn(false),
    resourcesReleased(true), initialized(false)
{
    modulatorInput.store(0);
    
    inputGain.store(1.0f);
    prevInputGain.store(1.0f);
    
    outputGain.store(1.0f);
    prevOutputGain.store(1.0f);
    
    dryGain.store(1.0f);
    prevDryGain.store(1.0f);
    
    wetGain.store(1.0f);
    prevWetGain.store(1.0f);
}


template<typename SampleType>
ImogenEngine<SampleType>::~ImogenEngine()
{ }

    
#undef bvie_VOID_TEMPLATE
#define bvie_VOID_TEMPLATE template<typename SampleType> void ImogenEngine<SampleType>
    

bvie_VOID_TEMPLATE::initialize (const double initSamplerate, const int initSamplesPerBlock, const int initNumVoices)
{
    jassert (initSamplerate > 0 && initSamplesPerBlock > 0 && initNumVoices > 0);
    
    harmonizer.initialize (initNumVoices, initSamplerate, initSamplesPerBlock);
    
    inputBuffer.initialize (1, internalBlocksize * 2);
    outputBuffer.initialize(2, internalBlocksize * 3);
    
    prepare (initSamplerate, initSamplesPerBlock);
    
    initialized = true;
}


bvie_VOID_TEMPLATE::prepare (double sampleRate, int samplesPerBlock)
{
    jassert (sampleRate > 0);
    jassert (samplesPerBlock > 0);
    
    midiChoppingBuffer.ensureSize (size_t(samplesPerBlock * 2));
    
    const size_t doubleBlocksize = size_t(internalBlocksize * 2);
    
    midiInputCollection .ensureSize (doubleBlocksize);
    midiOutputCollection.ensureSize (doubleBlocksize);
    chunkMidiBuffer.ensureSize (doubleBlocksize);
    
    inBuffer .setSize (1, internalBlocksize, true, true, true);
    dryBuffer.setSize (2, internalBlocksize, true, true, true);
    wetBuffer.setSize (2, internalBlocksize, true, true, true);
    
    inputBuffer.changeSize (1, internalBlocksize * 2);
    outputBuffer.changeSize(2, internalBlocksize * 3);
    
    harmonizer.setCurrentPlaybackSampleRate (sampleRate);
    harmonizer.prepare (internalBlocksize);
    
    dspSpec.sampleRate = sampleRate;
    dspSpec.numChannels = 2;
    dspSpec.maximumBlockSize = uint32(internalBlocksize);
    limiter.prepare (dspSpec);
    dryWetMixer.prepare (dspSpec);
    dryWetMixer.setWetLatency(0);
    
    resourcesReleased = false;
    
    prevOutputGain.store(outputGain.load());
    prevInputGain.store(inputGain.load());
    prevDryGain.store(dryGain.load());
    prevWetGain.store(wetGain.load());
}
    
    
bvie_VOID_TEMPLATE::latencyChanged (const int newLatency)
{
    internalBlocksize = newLatency;
    
    harmonizer.prepare (internalBlocksize);
    
    inBuffer .setSize (1, internalBlocksize, true, true, true);
    dryBuffer.setSize (2, internalBlocksize, true, true, true);
    wetBuffer.setSize (2, internalBlocksize, true, true, true);
    
    inputBuffer.changeSize (1, internalBlocksize * 2);
    outputBuffer.changeSize(2, internalBlocksize * 3);
    
    dspSpec.maximumBlockSize = uint32(internalBlocksize);
    limiter.prepare (dspSpec);
    dryWetMixer.prepare (dspSpec);
}

    
bvie_VOID_TEMPLATE::reset()
{
    harmonizer.allNotesOff(false);
    
    dryWetMixer.reset();
    limiter.reset();
    
    prevOutputGain.store(outputGain.load());
    prevInputGain.store(inputGain.load());
    prevDryGain.store(dryGain.load());
    prevWetGain.store(wetGain.load());
}
    
    
bvie_VOID_TEMPLATE::killAllMidi()
{
    harmonizer.allNotesOff(false);
}


bvie_VOID_TEMPLATE::releaseResources()
{
    harmonizer.releaseResources();
    
    wetBuffer.setSize(0, 0, false, false, false);
    dryBuffer.setSize(0, 0, false, false, false);
    inBuffer .setSize(0, 0, false, false, false);
    
    inputBuffer.releaseResources();
    outputBuffer.releaseResources();
    
    midiChoppingBuffer.clear();
    midiInputCollection.clear();
    midiOutputCollection.clear();
    chunkMidiBuffer.clear();
    
    dryWetMixer.reset();
    limiter.reset();
    
    resourcesReleased  = true;
    initialized        = false;
}



/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/*
    AUDIO RENDERING
 
    The internal algorithm always processes samples in blocks of internalBlocksize samples, regardless of the buffer sizes recieved from the host. This regulated-blocksize processing happens in the renderBlock() function.
    In order to achieve this blocksize regulation, the processing is wrapped in several layers of buffer slicing and what essentially amounts to an audio & MIDI FIFO.
 */

bvie_VOID_TEMPLATE::process (AudioBuffer<SampleType>& inBus, AudioBuffer<SampleType>& output,
                                        MidiBuffer& midiMessages,
                                        const bool applyFadeIn, const bool applyFadeOut,
                                        const bool isBypassed)
{
    // at this layer, we check to ensure that the buffer sent to us does not exceed the internal blocksize we want to process. If it does, it is broken into smaller chunks, and processWrapped() called on each of these chunks in sequence.
    
    const int totalNumSamples = inBus.getNumSamples();
    
    if (totalNumSamples == 0 || inBus.getNumChannels() == 0)
        return;
    
    jassert (output.getNumChannels() == 2);
    
    if (totalNumSamples <= internalBlocksize)
    {
        processWrapped (inBus, output, midiMessages, applyFadeIn, applyFadeOut, isBypassed);
        return;
    }
    
    int samplesLeft = totalNumSamples;
    int startSample = 0;
    
    bool actuallyFadingIn  = applyFadeIn;
    bool actuallyFadingOut = applyFadeOut;
    
    do {
        const int chunkNumSamples = std::min (internalBlocksize, samplesLeft);
        
        AudioBuffer<SampleType> inBusProxy  (inBus.getArrayOfWritePointers(),  inBus.getNumChannels(), startSample, chunkNumSamples);
        AudioBuffer<SampleType> outputProxy (output.getArrayOfWritePointers(), 2,                      startSample, chunkNumSamples);
        
        /* put just the midi messages for this time segment into the midiChoppingBuffer
           in the midiChoppingBuffer, events will start at timestamp sample 0
           the harmonizer's midi output will be returned by being copied to this same region of the midiChoppingBuffer */
        midiChoppingBuffer.clear();
        MidiUtils::copyRangeOfMidiBuffer (midiMessages, midiChoppingBuffer, startSample, 0, chunkNumSamples);
        
        processWrapped (inBusProxy, outputProxy, midiChoppingBuffer, actuallyFadingIn, actuallyFadingOut, isBypassed);
        
        // copy the harmonizer's midi output back to midiMessages (I/O), at the original startSample
        MidiUtils::copyRangeOfMidiBuffer (midiChoppingBuffer, midiMessages, 0, startSample, chunkNumSamples);
        
        startSample += chunkNumSamples;
        samplesLeft -= chunkNumSamples;
    
        actuallyFadingIn  = false;
        actuallyFadingOut = false;
    }
    while (samplesLeft > 0);
}


bvie_VOID_TEMPLATE::processWrapped (AudioBuffer<SampleType>& inBus, AudioBuffer<SampleType>& output,
                                               MidiBuffer& midiMessages,
                                               const bool applyFadeIn, const bool applyFadeOut,
                                               const bool isBypassed)
{
    // at this level, the buffer block sizes sent to us are garunteed to NEVER exceed the declared internalBlocksize, but they may still be SMALLER than this blocksize -- the individual buffers this function recieves may be as short as 1 sample long.
    // this is where the secret sauce happens that ensures the consistent block sizes fed to renderBlock()
    
    const int numNewSamples = inBus.getNumSamples();
    
    jassert (numNewSamples <= internalBlocksize);
    
    AudioBuffer<SampleType> input; // input must be a MONO buffer!
    
    switch (modulatorInput.load()) // isolate a mono input buffer from the input bus, mixing to mono if necessary
    {
        case (0): // take only the left channel
        {
            input = AudioBuffer<SampleType> (inBus.getArrayOfWritePointers(), 1, numNewSamples);
            break;
        }
            
        case (1):  // take only the right channel
        {
            input = AudioBuffer<SampleType> ((inBus.getArrayOfWritePointers() + (inBus.getNumChannels() > 1)), 1, numNewSamples);
            break;
        }
            
        case (2):  // mix all input channels to mono
        {
            const int totalNumChannels = inBus.getNumChannels();
            
            if (totalNumChannels == 1)
            {
                input = AudioBuffer<SampleType> (inBus.getArrayOfWritePointers(), 1, numNewSamples);
                break;
            }
            
            inBuffer.copyFrom (0, 0, inBus, 0, 0, numNewSamples);
            
            for (int channel = 1; channel < totalNumChannels; ++channel)
                inBuffer.addFrom (0, 0, inBus, channel, 0, numNewSamples);
            
            inBuffer.applyGain (1.0f / totalNumChannels);
            
            input = AudioBuffer<SampleType> (inBuffer.getArrayOfWritePointers(), 1, numNewSamples);
            break;
        }
    }
    
    inputBuffer.pushSamples (input, 0, 0, numNewSamples, 0);
    midiInputCollection.pushEvents (midiMessages, numNewSamples);
    
    if (inputBuffer.numStoredSamples() >= internalBlocksize)  // we have enough samples, render the new chunk
    {
        inBuffer.clear();
        inputBuffer.popSamples (inBuffer, 0, 0, internalBlocksize, 0);
        
        if (isBypassed)
        {
            for (int chan = 0; chan < 2; ++chan)
                outputBuffer.pushSamples (inBuffer, 0, 0, internalBlocksize, chan);
        }
        else
        {
            chunkMidiBuffer.clear();
            midiInputCollection.popEvents (chunkMidiBuffer, internalBlocksize);
            
            renderBlock (inBuffer, chunkMidiBuffer);
        }
    }
    
    for (int chan = 0; chan < 2; ++chan)
        outputBuffer.popSamples (output, chan, 0, numNewSamples, chan);
    
    midiOutputCollection.popEvents (midiMessages, numNewSamples);
    
    if (applyFadeIn)
        output.applyGainRamp (0, numNewSamples, 0.0f, 1.0f);
    
    if (applyFadeOut)
        output.applyGainRamp (0, numNewSamples, 1.0f, 0.0f);
}



bvie_VOID_TEMPLATE::renderBlock (const AudioBuffer<SampleType>& input,
                                                  MidiBuffer& midiMessages)       
{
    // at this stage, the blocksize is garunteed to ALWAYS be the declared internalBlocksize (2 * the max detectable period)
    
    // master input gain
    const float currentInGain = inputGain.load();
    
    if (input.getReadPointer(0) == inBuffer.getReadPointer(0))
        inBuffer.applyGainRamp (0, internalBlocksize, prevInputGain.load(), currentInGain);
    else
        inBuffer.copyFromWithRamp (0, 0, input.getReadPointer(0), internalBlocksize, prevInputGain.load(), currentInGain);
    
    prevInputGain.store(currentInGain);

    // write to dry buffer & apply panning
    for (int chan = 0; chan < 2; ++chan)
        dryBuffer.copyFromWithRamp (chan, 0, inBuffer.getReadPointer(0), internalBlocksize,
                                    dryPanner.getPrevGain(chan), dryPanner.getGainMult(chan));
    // dry gain
    const float currentDryGain = dryGain.load();
    dryBuffer.applyGainRamp (0, internalBlocksize, prevDryGain.load(), currentDryGain);
    prevDryGain.store(currentDryGain);

    dryWetMixer.pushDrySamples ( dsp::AudioBlock<SampleType>(dryBuffer) );

    // puts the harmonizer's rendered stereo output into wetBuffer & returns its midi output into midiMessages
    harmonizer.renderVoices (inBuffer, wetBuffer, midiMessages);
    
    midiOutputCollection.pushEvents (midiMessages, internalBlocksize);

    // wet gain
    const float currentWetGain = wetGain.load();
    wetBuffer.applyGainRamp (0, internalBlocksize, prevWetGain.load(), currentWetGain);
    prevWetGain.store(currentWetGain);

    dryWetMixer.mixWetSamples ( dsp::AudioBlock<SampleType>(wetBuffer) ); // puts the mixed dry & wet samples into wetBuffer

    // master output gain
    const float currentOutGain = outputGain.load();
    wetBuffer.applyGainRamp (0, internalBlocksize, prevOutputGain.load(), currentOutGain);
    prevOutputGain.store(currentOutGain);

    if (limiterIsOn)
    {
        dsp::AudioBlock<SampleType> limiterBlock (wetBuffer);
        limiter.process (dsp::ProcessContextReplacing<SampleType>(limiterBlock));
    }
    
    for (int chan = 0; chan < 2; ++chan)
        outputBuffer.pushSamples (wetBuffer, chan, 0, internalBlocksize, chan);
}


/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// functions for updating parameters ----------------------------------------------------------------------------------------------------------------

bvie_VOID_TEMPLATE::updateNumVoices (const int newNumVoices)
{
    const int currentVoices = harmonizer.getNumVoices();
    
    if (currentVoices == newNumVoices)
        return;
    
    if (newNumVoices > currentVoices)
        harmonizer.addNumVoices (newNumVoices - currentVoices);
    else
        harmonizer.removeNumVoices (currentVoices - newNumVoices);
}


bvie_VOID_TEMPLATE::updateDryVoxPan  (const int newMidiPan)
{
    dryPanner.setMidiPan (newMidiPan);
}


bvie_VOID_TEMPLATE::updateInputGain (const float newInGain)
{
    prevInputGain.store(inputGain.load());
    inputGain.store(newInGain);
}


bvie_VOID_TEMPLATE::updateOutputGain (const float newOutGain)
{
    prevOutputGain.store(outputGain.load());
    outputGain.store(newOutGain);
}


bvie_VOID_TEMPLATE::updateDryGain (const float newDryGain)
{
    prevDryGain.store(dryGain.load());
    dryGain.store(newDryGain);
}


bvie_VOID_TEMPLATE::updateWetGain (const float newWetGain)
{
    prevWetGain.store(wetGain.load());
    wetGain.store(newWetGain);
}


bvie_VOID_TEMPLATE::updateDryWet (const int percentWet)
{
    dryWetMixer.setWetMixProportion (percentWet / 100.0f);
}

    
bvie_VOID_TEMPLATE::updateAdsr(const float attack, const float decay, const float sustain, const float release, const bool isOn)
{
    harmonizer.updateADSRsettings(attack, decay, sustain, release);
    harmonizer.setADSRonOff(isOn);
}
    

bvie_VOID_TEMPLATE::updateQuickKill(const int newMs)
{
    harmonizer.updateQuickReleaseMs(newMs);
}
    

bvie_VOID_TEMPLATE::updateQuickAttack(const int newMs)
{
    harmonizer.updateQuickAttackMs(newMs);
}
    

bvie_VOID_TEMPLATE::updateStereoWidth(const int newStereoWidth, const int lowestPannedNote)
{
    harmonizer.updateLowestPannedNote(lowestPannedNote);
    harmonizer.updateStereoWidth     (newStereoWidth);
}
    

bvie_VOID_TEMPLATE::updateMidiVelocitySensitivity(const int newSensitivity)
{
    harmonizer.updateMidiVelocitySensitivity(newSensitivity);
}
    

bvie_VOID_TEMPLATE::updatePitchbendSettings(const int rangeUp, const int rangeDown)
{
    harmonizer.updatePitchbendSettings(rangeUp, rangeDown);
}
    

bvie_VOID_TEMPLATE::updatePedalPitch(const bool isOn, const int upperThresh, const int interval)
{
    harmonizer.setPedalPitch           (isOn);
    harmonizer.setPedalPitchUpperThresh(upperThresh);
    harmonizer.setPedalPitchInterval   (interval);
}
    

bvie_VOID_TEMPLATE::updateDescant(const bool isOn, const int lowerThresh, const int interval)
{
    harmonizer.setDescant           (isOn);
    harmonizer.setDescantLowerThresh(lowerThresh);
    harmonizer.setDescantInterval   (interval);
}
    

bvie_VOID_TEMPLATE::updateConcertPitch(const int newConcertPitchHz)
{
    harmonizer.setConcertPitchHz(newConcertPitchHz);
}
   
    
bvie_VOID_TEMPLATE::updateNoteStealing(const bool shouldSteal)
{
    harmonizer.setNoteStealingEnabled(shouldSteal);
}
    

bvie_VOID_TEMPLATE::updateMidiLatch(const bool isLatched)
{
    harmonizer.setMidiLatch(isLatched, true);
}
    
    
bvie_VOID_TEMPLATE::updateIntervalLock(const bool isLocked)
{
    harmonizer.setIntervalLatch (isLocked, true);
}
    

bvie_VOID_TEMPLATE::updateLimiter(const float thresh, const int release, const bool isOn)
{
    ScopedLock sl (lock);
    limiterIsOn = isOn;
    limiter.setThreshold(thresh);
    limiter.setRelease(float(release));
}


bvie_VOID_TEMPLATE::updateSoftPedalGain (const float newGain)
{
    harmonizer.setSoftPedalGainMultiplier (newGain);
}


bvie_VOID_TEMPLATE::updatePitchDetectionHzRange (const int minHz, const int maxHz)
{
    harmonizer.updatePitchDetectionHzRange (minHz, maxHz);
    
    latencyChanged (harmonizer.getLatencySamples());
}


bvie_VOID_TEMPLATE::updatePitchDetectionConfidenceThresh (const float newUpperThresh, const float newLowerThresh)
{
    harmonizer.updatePitchDetectionConfidenceThresh (newUpperThresh, newLowerThresh);
}
    
    
bvie_VOID_TEMPLATE::updateAftertouchGainOnOff (const bool shouldBeOn)
{
    harmonizer.setAftertouchGainOnOff (shouldBeOn);
}
    
    
bvie_VOID_TEMPLATE::updatePlayingButReleasedGain (const float newGainMult)
{
    harmonizer.setPlayingButReleasedGain(newGainMult);
}
    
    
#undef bvie_VOID_TEMPLATE

template class ImogenEngine<float>;
template class ImogenEngine<double>;


} // namespace
