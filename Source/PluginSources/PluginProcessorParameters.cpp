
#include "PluginProcessor.h"


juce::AudioProcessorValueTreeState::ParameterLayout ImogenAudioProcessor::createParameters()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;
    
    juce::NormalisableRange<float> gainRange (-60.0f, 0.0f, 0.01f);
    
    // main bypass
    params.push_back(std::make_unique<juce::AudioParameterBool> ("mainBypass", "Bypass", false));
    
    // lead vox pan
    params.push_back(std::make_unique<juce::AudioParameterInt>  ("dryPan", "Dry vox pan", 0, 127, 64));
    
    // ADSR
    juce::NormalisableRange<float> msRange (0.001f, 1.0f, 0.001f);
    params.push_back(std::make_unique<juce::AudioParameterFloat>("adsrAttack", "ADSR Attack", msRange, 0.035f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("adsrDecay", "ADSR Decay", msRange, 0.06f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("adsrSustain", "ADSR Sustain",
                                                                 juce::NormalisableRange<float> (0.01f, 1.0f, 0.01f), 0.8f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("adsrRelease", "ADSR Release", msRange, 0.1f));
    params.push_back(std::make_unique<juce::AudioParameterBool> ("adsrOnOff", "ADSR on/off", true));
    
    // stereo width
    params.push_back(std::make_unique<juce::AudioParameterInt>  ("stereoWidth", "Stereo Width", 0, 100, 100));
    params.push_back(std::make_unique<juce::AudioParameterInt>  ("lowestPan", "Lowest panned midiPitch", 0, 127, 0));
    
    // midi settings
    params.push_back(std::make_unique<juce::AudioParameterInt>  ("midiVelocitySensitivity", "MIDI Velocity Sensitivity", 0, 100, 100));
    params.push_back(std::make_unique<juce::AudioParameterInt>  ("PitchBendUpRange", "Pitch bend range (up)", 0, 12, 2));
    params.push_back(std::make_unique<juce::AudioParameterInt>  ("PitchBendDownRange", "Pitch bend range (down)", 0, 12, 2));
    params.push_back(std::make_unique<juce::AudioParameterInt>  ("concertPitch", "Concert pitch (Hz)", 392, 494, 440));
    params.push_back(std::make_unique<juce::AudioParameterBool> ("voiceStealing", "Voice stealing", false));
    params.push_back(std::make_unique<juce::AudioParameterBool> ("aftertouchGainToggle", "Aftertouch gain on/off", true));
    // midi pedal pitch
    params.push_back(std::make_unique<juce::AudioParameterBool> ("pedalPitchToggle", "Pedal pitch on/off", false));
    params.push_back(std::make_unique<juce::AudioParameterInt>  ("pedalPitchThresh", "Pedal pitch upper threshold", 0, 127, 0));
    params.push_back(std::make_unique<juce::AudioParameterInt>  ("pedalPitchInterval", "Pedal pitch interval", 1, 12, 12));
    // midi descant
    params.push_back(std::make_unique<juce::AudioParameterBool> ("descantToggle", "Descant on/off", false));
    params.push_back(std::make_unique<juce::AudioParameterInt>  ("descantThresh", "Descant lower threshold", 0, 127, 127));
    params.push_back(std::make_unique<juce::AudioParameterInt>  ("descantInterval", "Descant interval", 1, 12, 12));
    
    // mixer settings
    params.push_back(std::make_unique<juce::AudioParameterInt>  ("masterDryWet", "% wet", 0, 100, 100));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("inputGain", "Input gain",   gainRange, 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("outputGain", "Output gain", gainRange, -4.0f));
    
    // limiter toggle
    params.push_back(std::make_unique<juce::AudioParameterBool> ("limiterIsOn", "Limiter on/off", true));
    
    // pitch detection vocal range
#define imogen_DEFAULT_VOCAL_RANGE_TYPE 0
    params.push_back(std::make_unique<juce::AudioParameterChoice>("vocalRangeType", "Input vocal range",
                                                                  imgn_VOCAL_RANGE_TYPES,
                                                                  imogen_DEFAULT_VOCAL_RANGE_TYPE, "Input vocal range",
                                                                  [this] (int index, int maximumStringLength)
                                                                         {
                                                                             juce::String name = vocalRangeTypes[index];
                                                                             if (name.length() > maximumStringLength)
                                                                                 name = name.substring (0, maximumStringLength);
                                                                             return name;
                                                                         },
                                                                  [this] (const juce::String& text)
                                                                         {
                                                                             for (int i = 0; i < 4; ++i)
                                                                                 if (text.equalsIgnoreCase(vocalRangeTypes[i]))
                                                                                     return i;
                                                                             return imogen_DEFAULT_VOCAL_RANGE_TYPE;
                                                                         } ));
    return { params.begin(), params.end() };
}

#undef imogen_DEFAULT_VOCAL_RANGE_TYPE
#undef imgn_VOCAL_RANGE_TYPES


void ImogenAudioProcessor::initializeParameterPointers()
{
    mainBypass         = dynamic_cast<juce::AudioParameterBool*> (tree.getParameter("mainBypass"));                 jassert (mainBypass);
    dryPan             = dynamic_cast<juce::AudioParameterInt*>  (tree.getParameter("dryPan"));                     jassert(dryPan);
    dryWet             = dynamic_cast<juce::AudioParameterInt*>  (tree.getParameter("masterDryWet"));               jassert(dryWet);
    adsrAttack         = dynamic_cast<juce::AudioParameterFloat*>(tree.getParameter("adsrAttack"));                 jassert(adsrAttack);
    adsrDecay          = dynamic_cast<juce::AudioParameterFloat*>(tree.getParameter("adsrDecay"));                  jassert(adsrDecay);
    adsrSustain        = dynamic_cast<juce::AudioParameterFloat*>(tree.getParameter("adsrSustain"));                jassert(adsrSustain);
    adsrRelease        = dynamic_cast<juce::AudioParameterFloat*>(tree.getParameter("adsrRelease"));                jassert(adsrRelease);
    adsrToggle         = dynamic_cast<juce::AudioParameterBool*> (tree.getParameter("adsrOnOff"));                  jassert(adsrToggle);
    stereoWidth        = dynamic_cast<juce::AudioParameterInt*>  (tree.getParameter("stereoWidth"));                jassert(stereoWidth);
    lowestPanned       = dynamic_cast<juce::AudioParameterInt*>  (tree.getParameter("lowestPan"));                  jassert(lowestPanned);
    velocitySens       = dynamic_cast<juce::AudioParameterInt*>  (tree.getParameter("midiVelocitySensitivity"));    jassert(velocitySens);
    pitchBendUp        = dynamic_cast<juce::AudioParameterInt*>  (tree.getParameter("PitchBendUpRange"));           jassert(pitchBendUp);
    pitchBendDown      = dynamic_cast<juce::AudioParameterInt*>  (tree.getParameter("PitchBendDownRange"));         jassert(pitchBendDown);
    pedalPitchIsOn     = dynamic_cast<juce::AudioParameterBool*> (tree.getParameter("pedalPitchToggle"));           jassert(pedalPitchIsOn);
    pedalPitchThresh   = dynamic_cast<juce::AudioParameterInt*>  (tree.getParameter("pedalPitchThresh"));           jassert(pedalPitchThresh);
    pedalPitchInterval = dynamic_cast<juce::AudioParameterInt*>  (tree.getParameter("pedalPitchInterval"));         jassert(pedalPitchInterval);
    descantIsOn        = dynamic_cast<juce::AudioParameterBool*> (tree.getParameter("descantToggle"));              jassert(descantIsOn);
    descantThresh      = dynamic_cast<juce::AudioParameterInt*>  (tree.getParameter("descantThresh"));              jassert(descantThresh);
    descantInterval    = dynamic_cast<juce::AudioParameterInt*>  (tree.getParameter("descantInterval"));            jassert(descantInterval);
    concertPitchHz     = dynamic_cast<juce::AudioParameterInt*>  (tree.getParameter("concertPitch"));               jassert(concertPitchHz);
    voiceStealing      = dynamic_cast<juce::AudioParameterBool*> (tree.getParameter("voiceStealing"));              jassert(voiceStealing);
    inputGain          = dynamic_cast<juce::AudioParameterFloat*>(tree.getParameter("inputGain"));                  jassert(inputGain);
    outputGain         = dynamic_cast<juce::AudioParameterFloat*>(tree.getParameter("outputGain"));                 jassert(outputGain);
    limiterToggle      = dynamic_cast<juce::AudioParameterBool*> (tree.getParameter("limiterIsOn"));                jassert(limiterToggle);
    vocalRangeType     = dynamic_cast<juce::AudioParameterChoice*>(tree.getParameter("vocalRangeType"));            jassert (vocalRangeType);
    aftertouchGainToggle = dynamic_cast<juce::AudioParameterBool*> (tree.getParameter("aftertouchGainToggle"));     jassert(aftertouchGainToggle);
}


void ImogenAudioProcessor::updateParameterDefaults()
{
    defaultDryPan.store (dryPan->get());
    defaultDryWet.store (dryWet->get());
    defaultStereoWidth.store (stereoWidth->get());
    defaultLowestPannedNote.store (lowestPanned->get());
    defaultVelocitySensitivity.store (velocitySens->get());
    defaultPitchbendUp.store (pitchBendUp->get());
    defaultPitchbendDown.store (pitchBendDown->get());
    defaultPedalPitchThresh.store (pedalPitchThresh->get());
    defaultPedalPitchInterval.store (pedalPitchInterval->get());
    defaultDescantThresh.store (descantThresh->get());
    defaultDescantInterval.store (descantInterval->get());
    defaultConcertPitchHz.store (concertPitchHz->get());
    defaultAdsrAttack.store (adsrAttack->get());
    defaultAdsrDecay.store (adsrDecay->get());
    defaultAdsrSustain.store (adsrSustain->get());
    defaultAdsrRelease.store (adsrRelease->get());
    defaultInputGain.store (inputGain->get());
    defaultOutputGain.store (outputGain->get());
    
    parameterDefaultsAreDirty.store (true);
}


juce::AudioProcessorParameter* ImogenAudioProcessor::getBypassParameter() const
{
    return tree.getParameter ("mainBypass");
}



// functions for updating parameters ----------------------------------------------------------------------------------------------------------------

template<typename SampleType>
void ImogenAudioProcessor::updateAllParameters (bav::ImogenEngine<SampleType>& activeEngine)
{
    updateVocalRangeType (vocalRangeType->getIndex());
    
    activeEngine.updateInputGain    (juce::Decibels::decibelsToGain (inputGain->get()));
    activeEngine.updateOutputGain   (juce::Decibels::decibelsToGain (outputGain->get()));
    activeEngine.updateDryVoxPan (dryPan->get());
    activeEngine.updateDryWet (dryWet->get());
    activeEngine.updateAdsr (adsrAttack->get(), adsrDecay->get(), adsrSustain->get(), adsrRelease->get(), adsrToggle->get());
    activeEngine.updateStereoWidth (stereoWidth->get(), lowestPanned->get());
    activeEngine.updateMidiVelocitySensitivity (velocitySens->get());
    activeEngine.updatePitchbendSettings (pitchBendUp->get(), pitchBendDown->get());
    activeEngine.updatePedalPitch (pedalPitchIsOn->get(), pedalPitchThresh->get(), pedalPitchInterval->get());
    activeEngine.updateDescant (descantIsOn->get(), descantThresh->get(), descantInterval->get());
    activeEngine.updateConcertPitch (concertPitchHz->get());
    activeEngine.updateNoteStealing (voiceStealing->get());
    activeEngine.updateLimiter (limiterToggle->get());
    activeEngine.updateAftertouchGainOnOff (aftertouchGainToggle->get());
}


void ImogenAudioProcessor::updateVocalRangeType (int rangeTypeIndex)
{
    if (prevRangeTypeIndex == rangeTypeIndex)
        return;
    
    const juce::String rangeType = vocalRangeTypes[rangeTypeIndex];
    
    int minHz = 80;
    int maxHz = 2400;
    
    if (rangeType.equalsIgnoreCase("Soprano"))
    {
        minHz = bav::dsp::midiToFreq (57);
        maxHz = bav::dsp::midiToFreq (88);
    }
    else if (rangeType.equalsIgnoreCase("Alto"))
    {
        minHz = bav::dsp::midiToFreq (50);
        maxHz = bav::dsp::midiToFreq (81);
    }
    else if (rangeType.equalsIgnoreCase("Tenor"))
    {
        minHz = bav::dsp::midiToFreq (43);
        maxHz = bav::dsp::midiToFreq (76);
    }
    else if (rangeType.equalsIgnoreCase("Bass"))
    {
        minHz = bav::dsp::midiToFreq (36);
        maxHz = bav::dsp::midiToFreq (67);
    }
    
    updatePitchDetectionHzRange (minHz, maxHz);
    prevRangeTypeIndex = rangeTypeIndex;
}


void ImogenAudioProcessor::updatePitchDetectionHzRange (int minHz, int maxHz)
{
    suspendProcessing (true);
    
    if (isUsingDoublePrecision())
    {
        doubleEngine.updatePitchDetectionHzRange (minHz, maxHz);
        setLatencySamples (doubleEngine.reportLatency());
    }
    else
    {
        floatEngine.updatePitchDetectionHzRange (minHz, maxHz);
        setLatencySamples (floatEngine.reportLatency());
    }
    
    suspendProcessing (false);
}


void ImogenAudioProcessor::updateNumVoices (const int newNumVoices)
{
    if (isUsingDoublePrecision())
    {
        if (doubleEngine.getCurrentNumVoices() == newNumVoices)
            return;
        
        suspendProcessing (true);
        doubleEngine.updateNumVoices (newNumVoices);
    }
    else
    {
        if (floatEngine.getCurrentNumVoices() == newNumVoices)
            return;
        
        suspendProcessing (true);
        floatEngine.updateNumVoices (newNumVoices);
    }
    
    suspendProcessing (false);
}


void ImogenAudioProcessor::updateModulatorInputSource (const int newSource)
{
    if (isUsingDoublePrecision())
        doubleEngine.setModulatorSource (newSource);
    else
        floatEngine.setModulatorSource (newSource);
}


// functions for preset & state management system ---------------------------------------------------------------------------------------------------

inline juce::File ImogenAudioProcessor::getPresetsFolder() const
{
    juce::File rootFolder;
    
#if JUCE_MAC
    rootFolder = juce::File::getSpecialLocation (juce::File::SpecialLocationType::userApplicationDataDirectory)
                    .getChildFile ("Audio")
                    .getChildFile ("Presets")
                    .getChildFile ("Ben Vining Music Software")
                    .getChildFile ("Imogen");
#elif JUCE_LINUX
    rootFolder = juce::File::getSpecialLocation (juce::File::SpecialLocationType::userApplicationDataDirectory)
                    .getChildFile ("Ben Vining Music Software")
                    .getChildFile ("Imogen");
#elif JUCE_WINDOWS
    rootFolder = juce::File::getSpecialLocation (juce::File::SpecialLocationType::userDocumentsDirectory)
                    .getChildFile ("Ben Vining Music Software")
                    .getChildFile ("Imogen");
#else
  #error Unsupported operating system!
#endif
    
    if (! rootFolder.isDirectory())
        rootFolder.createDirectory(); // creates the presets folder if it doesn't already exist
    
    return rootFolder;
}


void ImogenAudioProcessor::deletePreset (juce::String presetName)
{
    juce::File presetToDelete = getPresetsFolder().getChildFile(presetName);
    
    if (presetToDelete.existsAsFile())
        if (! presetToDelete.moveToTrash())
            presetToDelete.deleteFile();
    
    updateHostDisplay();
}


// functions for saving state info.....................................

void ImogenAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    std::unique_ptr<juce::XmlElement> xml;
    
    if (isUsingDoublePrecision())
        xml = pluginStateToXml(doubleEngine);
    else
        xml = pluginStateToXml(floatEngine);
    
    copyXmlToBinary (*xml, destData);
}


void ImogenAudioProcessor::savePreset (juce::String presetName)
{
    // this function can be used both to save new preset files or to update existing ones
    
    std::unique_ptr<juce::XmlElement> xml;
    
    if (isUsingDoublePrecision())
        xml = pluginStateToXml(doubleEngine);
    else
        xml = pluginStateToXml(floatEngine);
    
    xml->setAttribute ("presetName", presetName);
    
    presetName += ".xml";
    
    xml->writeTo (getPresetsFolder().getChildFile(presetName));
    updateHostDisplay();
}


template<typename SampleType>
inline std::unique_ptr<juce::XmlElement> ImogenAudioProcessor::pluginStateToXml (bav::ImogenEngine<SampleType>& activeEngine)
{
    std::unique_ptr<juce::XmlElement> xml = tree.copyState().createXml();
    
    const int numVoices = activeEngine.getCurrentNumVoices();
    const int inputSource = activeEngine.getModulatorSource();
    
    xml->setAttribute ("numberOfVoices", numVoices);
    xml->setAttribute ("modulatorInputSource", inputSource);
    
    return xml;
}


// functions for loading state info.................................

void ImogenAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xmlElement (getXmlFromBinary (data, sizeInBytes));
    
    if (isUsingDoublePrecision())
        updatePluginInternalState (*xmlElement, doubleEngine);
    else
        updatePluginInternalState (*xmlElement, floatEngine);
}


bool ImogenAudioProcessor::loadPreset (juce::String presetName)
{
    presetName += ".xml";
    
    juce::File presetToLoad = getPresetsFolder().getChildFile(presetName);
    
    if (! presetToLoad.existsAsFile())
        return false;
    
    auto xmlElement = juce::parseXML (presetToLoad);
    
    if (isUsingDoublePrecision())
        return updatePluginInternalState (*xmlElement, doubleEngine);
    
    return updatePluginInternalState (*xmlElement, floatEngine);
}


template<typename SampleType>
inline bool ImogenAudioProcessor::updatePluginInternalState (juce::XmlElement& newState, bav::ImogenEngine<SampleType>& activeEngine)
{
    if (! newState.hasTagName (tree.state.getType()))
        return false;
    
    suspendProcessing (true);
    
    tree.replaceState (juce::ValueTree::fromXml (newState));
    
    updateNumVoices (newState.getIntAttribute ("numberOfVoices", 4));
    activeEngine.setModulatorSource (newState.getIntAttribute ("modulatorInputSource", 0));
    
    updateAllParameters (activeEngine);
    
    updateParameterDefaults();
    
    suspendProcessing (false);
    
    updateHostDisplay();
    return true;  // TODO: how to check if replacing tree state was successful...?
}
