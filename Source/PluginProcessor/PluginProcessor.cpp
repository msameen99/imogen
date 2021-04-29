
/*======================================================================================================================================================
           _             _   _                _                _                 _               _
          /\ \          /\_\/\_\ _           /\ \             /\ \              /\ \            /\ \     _
          \ \ \        / / / / //\_\        /  \ \           /  \ \            /  \ \          /  \ \   /\_\
          /\ \_\      /\ \/ \ \/ / /       / /\ \ \         / /\ \_\          / /\ \ \        / /\ \ \_/ / /
         / /\/_/     /  \____\__/ /       / / /\ \ \       / / /\/_/         / / /\ \_\      / / /\ \___/ /
        / / /       / /\/________/       / / /  \ \_\     / / / ______      / /_/_ \/_/     / / /  \/____/
       / / /       / / /\/_// / /       / / /   / / /    / / / /\_____\    / /____/\       / / /    / / /
      / / /       / / /    / / /       / / /   / / /    / / /  \/____ /   / /\____\/      / / /    / / /
  ___/ / /__     / / /    / / /       / / /___/ / /    / / /_____/ / /   / / /______     / / /    / / /
 /\__\/_/___\    \/_/    / / /       / / /____\/ /    / / /______\/ /   / / /_______\   / / /    / / /
 \/_________/            \/_/        \/_________/     \/___________/    \/__________/   \/_/     \/_/
 
 
 This file is part of the Imogen codebase.
 
 @2021 by Ben Vining. All rights reserved.
 
 PluginProcessor.cpp: This file contains the guts of Imogen's AudioProcessor code.
 
======================================================================================================================================================*/


#include "GUI/Holders/Plugin_Editor/PluginEditor.h"
#include "PluginProcessor.h"

#include "PluginProcessorParameters.cpp"
#include "PluginProcessorState.cpp"


ImogenAudioProcessor::ImogenAudioProcessor()
  : AudioProcessor (makeBusProperties()),
    translationInitializer (findAppropriateTranslationFile()),
    tree (*this, nullptr, "IMOGEN_PARAMETERS", createParameters()),
    abletonLink (120.0) // constructed with the initial BPM
{
#if BV_USE_NE10
    ne10_init();
#endif
    
    jassert (AudioProcessor::getParameters().size() == numParams);
    initializeParameterPointers();
           
    if (isUsingDoublePrecision())
        initialize (doubleEngine);
    else
        initialize (floatEngine);
    
    // lastPresetName = getActivePresetName();
    
    constexpr int timerFramerate = 30;
    Timer::startTimerHz (timerFramerate);
    
    // oscReceiver.addListener (&oscListener);
}

ImogenAudioProcessor::~ImogenAudioProcessor()
{
    Timer::stopTimer();
    
    oscSender.disconnect();
    // oscReceiver.removeListener (&oscListener);
    oscReceiver.disconnect();
}

/*===========================================================================================================
===========================================================================================================*/

void ImogenAudioProcessor::timerCallback()
{

}

/*===========================================================================================================
 ===========================================================================================================*/

template <typename SampleType>
inline void ImogenAudioProcessor::initialize (bav::ImogenEngine<SampleType>& activeEngine)
{
    auto initSamplerate = getSampleRate();
    if (initSamplerate <= 0.0) initSamplerate = 44100.0;
    
    auto initBlockSize = getBlockSize();
    if (initBlockSize <= 0) initBlockSize = 512;
    
    activeEngine.initialize (initSamplerate, initBlockSize);
    
    setLatencySamples (activeEngine.reportLatency());
}


void ImogenAudioProcessor::prepareToPlay (const double sampleRate, const int samplesPerBlock)
{
    if (isUsingDoublePrecision())
        prepareToPlayWrapped (sampleRate, doubleEngine, floatEngine);
    else
        prepareToPlayWrapped (sampleRate, floatEngine,  doubleEngine);
    
    currentMessages.ensureStorageAllocated (samplesPerBlock);
}


template <typename SampleType1, typename SampleType2>
inline void ImogenAudioProcessor::prepareToPlayWrapped (const double sampleRate,
                                                        bav::ImogenEngine<SampleType1>& activeEngine,
                                                        bav::ImogenEngine<SampleType2>& idleEngine)
{
    if (! idleEngine.hasBeenReleased())
        idleEngine.releaseResources();
    
    processQueuedNonParamEvents (activeEngine);
    
    jassert (activeEngine.getLatency() > 0);
    
    activeEngine.prepare (sampleRate);
    
    setLatencySamples (activeEngine.reportLatency());
}

/*===========================================================================================================
 ===========================================================================================================*/

void ImogenAudioProcessor::releaseResources()
{
    if (! doubleEngine.hasBeenReleased())
        doubleEngine.releaseResources();
    
    if (! floatEngine.hasBeenReleased())
        floatEngine.releaseResources();
}


void ImogenAudioProcessor::reset()
{
    if (isUsingDoublePrecision())
        doubleEngine.reset();
    else
        floatEngine.reset();
}

/*===========================================================================================================
 ===========================================================================================================*/

void ImogenAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    processBlockWrapped (buffer, midiMessages, floatEngine,
                         getParameterPntr (mainBypassID)->getCurrentNormalizedValue() >= 0.5f);
}


void ImogenAudioProcessor::processBlock (juce::AudioBuffer<double>& buffer, juce::MidiBuffer& midiMessages)
{
    processBlockWrapped (buffer, midiMessages, doubleEngine,
                         getParameterPntr (mainBypassID)->getCurrentNormalizedValue() >= 0.5f);
}


void ImogenAudioProcessor::processBlockBypassed (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    auto mainBypass = getParameterPntr (mainBypassID);
    
    if (! (mainBypass->getCurrentNormalizedValue() >= 0.5f))
        mainBypass->orig()->setValueNotifyingHost (1.0f);
    
    processBlockWrapped (buffer, midiMessages, floatEngine, true);
}


void ImogenAudioProcessor::processBlockBypassed (juce::AudioBuffer<double>& buffer, juce::MidiBuffer& midiMessages)
{
    auto mainBypass = getParameterPntr (mainBypassID);
    
    if (! (mainBypass->getCurrentNormalizedValue() >= 0.5f))
        mainBypass->orig()->setValueNotifyingHost (1.0f);
    
    processBlockWrapped (buffer, midiMessages, doubleEngine, true);
}


template <typename SampleType>
inline void ImogenAudioProcessor::processBlockWrapped (juce::AudioBuffer<SampleType>& buffer,
                                                       juce::MidiBuffer& midiMessages,
                                                       bav::ImogenEngine<SampleType>& engine,
                                                       const bool isBypassedThisCallback)
{
    jassert (! engine.hasBeenReleased() && engine.hasBeenInitialized());
    
    juce::ScopedNoDenormals nodenorms;
    
    processQueuedNonParamEvents (engine);
    
    /* update all parameters... */
    engine.updateBypassStates (getParameterPntr(leadBypassID)->getCurrentNormalizedValue() >= 0.5f,
                               getParameterPntr(harmonyBypassID)->getCurrentNormalizedValue() >= 0.5f);

    engine.updateInputGain               (juce::Decibels::decibelsToGain (getParameterPntr(inputGainID)->getCurrentDenormalizedValue()));
    engine.updateOutputGain              (juce::Decibels::decibelsToGain (getParameterPntr(outputGainID)->getCurrentDenormalizedValue()));
    engine.updateDryVoxPan               (getParameterPntr(dryPanID)->getCurrentDenormalizedValue());
    engine.updateDryWet                  (getParameterPntr(dryWetID)->getCurrentDenormalizedValue());
    engine.updateNoteStealing            (getParameterPntr(voiceStealingID)->getCurrentNormalizedValue() >= 0.5f);
    engine.updateAftertouchGainOnOff     (getParameterPntr(aftertouchGainToggleID)->getCurrentNormalizedValue() >= 0.5f);
    engine.setModulatorSource            (getParameterPntr(inputSourceID)->getCurrentDenormalizedValue());
    engine.updateMidiVelocitySensitivity (getParameterPntr(velocitySensID)->getCurrentDenormalizedValue());
    engine.updatePitchbendRange          (getParameterPntr(pitchBendRangeID)->getCurrentDenormalizedValue());
    engine.updateAdsr                    (getParameterPntr(adsrAttackID)->getCurrentDenormalizedValue(),
                                          getParameterPntr(adsrDecayID)->getCurrentDenormalizedValue(),
                                          getParameterPntr(adsrSustainID)->getCurrentDenormalizedValue(),
                                          getParameterPntr(adsrReleaseID)->getCurrentDenormalizedValue());
    engine.updateStereoWidth             (getParameterPntr(stereoWidthID)->getCurrentDenormalizedValue(),
                                          getParameterPntr(lowestPannedID)->getCurrentDenormalizedValue());
    engine.updatePedalPitch              (getParameterPntr(pedalPitchIsOnID)->getCurrentNormalizedValue() >= 0.5f,
                                          getParameterPntr(pedalPitchThreshID)->getCurrentDenormalizedValue(),
                                          getParameterPntr(pedalPitchIntervalID)->getCurrentDenormalizedValue());
    engine.updateDescant                 (getParameterPntr(descantIsOnID)->getCurrentNormalizedValue() >= 0.5f,
                                          getParameterPntr(descantThreshID)->getCurrentDenormalizedValue(),
                                          getParameterPntr(descantIntervalID)->getCurrentDenormalizedValue());
    engine.updateLimiter                 (getParameterPntr(limiterToggleID)->getCurrentNormalizedValue() >= 0.5f);
    engine.updateNoiseGate               (getParameterPntr(noiseGateThresholdID)->getCurrentDenormalizedValue(),
                                          getParameterPntr(noiseGateToggleID)->getCurrentNormalizedValue() >= 0.5f);
    engine.updateDeEsser                 (getParameterPntr(deEsserAmountID)->getCurrentDenormalizedValue(),
                                          getParameterPntr(deEsserThreshID)->getCurrentDenormalizedValue(),
                                          getParameterPntr(deEsserToggleID)->getCurrentNormalizedValue() >= 0.5f);
    engine.updateDelay                   (getParameterPntr(delayDryWetID)->getCurrentDenormalizedValue(),
                                          2400,
                                          getParameterPntr(delayToggleID)->getCurrentNormalizedValue() >= 0.5f);
    engine.updateReverb                  (getParameterPntr(reverbDryWetID)->getCurrentDenormalizedValue(),
                                          getParameterPntr(reverbDecayID)->getCurrentDenormalizedValue(),
                                          getParameterPntr(reverbDuckID)->getCurrentDenormalizedValue(),
                                          getParameterPntr(reverbLoCutID)->getCurrentDenormalizedValue(),
                                          getParameterPntr(reverbHiCutID)->getCurrentDenormalizedValue(),
                                          getParameterPntr(reverbToggleID)->getCurrentNormalizedValue() >= 0.5f);

    const auto compressorKnobValue = getParameterPntr(compressorAmountID)->getCurrentDenormalizedValue();
    
    engine.updateCompressor (juce::jmap (compressorKnobValue, 0.0f, -60.0f),  // threshold (dB)
                             juce::jmap (compressorKnobValue, 1.0f, 10.0f),  // ratio
                             getParameterPntr(compressorToggleID)->getCurrentNormalizedValue() >= 0.5f);

    /* program change messages need to be handled at this top level... */
    std::for_each (midiMessages.cbegin(), midiMessages.cend(),
                   [&] (const juce::MidiMessageMetadata& meta)
                   {
                       const auto msg = meta.getMessage();
        
                       if (msg.isProgramChange())
                           setCurrentProgram (msg.getProgramChangeNumber());
                   });
    
    if (buffer.getNumSamples() == 0 || buffer.getNumChannels() == 0)
        return;
    
    using Buffer = juce::AudioBuffer<SampleType>;
    
    Buffer inBus  = getBusBuffer (buffer, true, getBusesLayout().getMainInputChannelSet() == juce::AudioChannelSet::disabled());
    Buffer outBus = getBusBuffer (buffer, false, 0);
    
    engine.process (inBus, outBus, midiMessages, isBypassedThisCallback);
}

/*===========================================================================================================================
 ============================================================================================================================*/

void ImogenAudioProcessor::changeMidiLatchState (bool isNowLatched)
{
    if (isUsingDoublePrecision())
        doubleEngine.updateMidiLatch (isNowLatched);
    else
        floatEngine.updateMidiLatch (isNowLatched);
    
    oscSender.sendMidiLatch (isNowLatched);
}

bool ImogenAudioProcessor::isMidiLatched() const
{
    return isUsingDoublePrecision() ? doubleEngine.isMidiLatched() : floatEngine.isMidiLatched();
}


int ImogenAudioProcessor::getNumAbletonLinkSessionPeers() const
{
    return abletonLink.isEnabled() ? (int)abletonLink.numPeers() : 0;
}


bool ImogenAudioProcessor::isConnectedToMtsEsp() const noexcept
{
    return isUsingDoublePrecision() ? doubleEngine.isConnectedToMtsEsp() : floatEngine.isConnectedToMtsEsp();
}

juce::String ImogenAudioProcessor::getScaleName() const
{
    return isUsingDoublePrecision() ? doubleEngine.getScaleName() : floatEngine.getScaleName();
}

/*===========================================================================================================================
 ============================================================================================================================*/

double ImogenAudioProcessor::getTailLengthSeconds() const
{
    return double(getParameterPntr(adsrReleaseID)->getCurrentNormalizedValue());
}


inline juce::AudioProcessor::BusesProperties ImogenAudioProcessor::makeBusProperties() const
{
    const auto stereo = juce::AudioChannelSet::stereo();
    const auto mono   = juce::AudioChannelSet::mono();

    return BusesProperties().withInput (TRANS ("Input"),     stereo, true)
                            .withInput (TRANS ("Sidechain"), mono,   false)
                            .withOutput(TRANS ("Output"),    stereo, true);
}


bool ImogenAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto disabled = juce::AudioChannelSet::disabled();
    
    if (layouts.getMainInputChannelSet() == disabled && layouts.getChannelSet(true, 1) == disabled)
        return false;
    
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

/*===========================================================================================================================
 ============================================================================================================================*/

juce::AudioProcessorEditor* ImogenAudioProcessor::createEditor()
{
    return new ImogenAudioProcessorEditor(*this);
}

ImogenGuiHolder* ImogenAudioProcessor::getActiveGui() const
{
    if (auto* editor = getActiveEditor())
        return dynamic_cast<ImogenGuiHolder*> (editor);
    
    return nullptr;
}

void ImogenAudioProcessor::saveEditorSize (int width, int height)
{
    savedEditorSize.setX (width);
    savedEditorSize.setY (height);
}

/*===========================================================================================================================
 ============================================================================================================================*/

// This creates new instances of the plugin..
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ImogenAudioProcessor();
}
