#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
ImogenAudioProcessor::ImogenAudioProcessor():
    AudioProcessor(makeBusProperties()),
    tree(*this, nullptr, "PARAMETERS", createParameters()),
    floatEngine(*this),
    limiterIsOn(true), inputGainMultiplier(1.0f), outputGainMultiplier(1.0f),
    prevDryPan(64), prevideb(0.0f), prevodeb(0.0f),
    modulatorInput(ModulatorInputSource::left),
    wasBypassedLastCallback(true)
{
    // setLatencySamples(newLatency); // TOTAL plugin latency!
    
    dryPan             = dynamic_cast<AudioParameterInt*>  (tree.getParameter("dryPan"));                   jassert(dryPan);
    dryWet             = dynamic_cast<AudioParameterInt*>  (tree.getParameter("masterDryWet"));             jassert(dryWet);
    inputChan          = dynamic_cast<AudioParameterInt*>  (tree.getParameter("inputChan"));                jassert(inputChan);
    adsrAttack         = dynamic_cast<AudioParameterFloat*>(tree.getParameter("adsrAttack"));               jassert(adsrAttack);
    adsrDecay          = dynamic_cast<AudioParameterFloat*>(tree.getParameter("adsrDecay"));                jassert(adsrDecay);
    adsrSustain        = dynamic_cast<AudioParameterFloat*>(tree.getParameter("adsrSustain"));              jassert(adsrSustain);
    adsrRelease        = dynamic_cast<AudioParameterFloat*>(tree.getParameter("adsrRelease"));              jassert(adsrRelease);
    adsrToggle         = dynamic_cast<AudioParameterBool*> (tree.getParameter("adsrOnOff"));                jassert(adsrToggle);
    quickKillMs        = dynamic_cast<AudioParameterInt*>  (tree.getParameter("quickKillMs"));              jassert(quickKillMs);
    quickAttackMs      = dynamic_cast<AudioParameterInt*>  (tree.getParameter("quickAttackMs"));            jassert(quickAttackMs);
    stereoWidth        = dynamic_cast<AudioParameterInt*>  (tree.getParameter("stereoWidth"));              jassert(stereoWidth);
    lowestPanned       = dynamic_cast<AudioParameterInt*>  (tree.getParameter("lowestPan"));                jassert(lowestPanned);
    velocitySens       = dynamic_cast<AudioParameterInt*>  (tree.getParameter("midiVelocitySensitivity"));  jassert(velocitySens);
    pitchBendUp        = dynamic_cast<AudioParameterInt*>  (tree.getParameter("PitchBendUpRange"));         jassert(pitchBendUp);
    pitchBendDown      = dynamic_cast<AudioParameterInt*>  (tree.getParameter("PitchBendDownRange"));       jassert(pitchBendDown);
    pedalPitchIsOn     = dynamic_cast<AudioParameterBool*> (tree.getParameter("pedalPitchToggle"));         jassert(pedalPitchIsOn);
    pedalPitchThresh   = dynamic_cast<AudioParameterInt*>  (tree.getParameter("pedalPitchThresh"));         jassert(pedalPitchThresh);
    pedalPitchInterval = dynamic_cast<AudioParameterInt*>  (tree.getParameter("pedalPitchInterval"));       jassert(pedalPitchInterval);
    descantIsOn        = dynamic_cast<AudioParameterBool*> (tree.getParameter("descantToggle"));            jassert(descantIsOn);
    descantThresh      = dynamic_cast<AudioParameterInt*>  (tree.getParameter("descantThresh"));            jassert(descantThresh);
    descantInterval    = dynamic_cast<AudioParameterInt*>  (tree.getParameter("descantInterval"));          jassert(descantInterval);
    concertPitchHz     = dynamic_cast<AudioParameterInt*>  (tree.getParameter("concertPitch"));             jassert(concertPitchHz);
    voiceStealing      = dynamic_cast<AudioParameterBool*> (tree.getParameter("voiceStealing"));            jassert(voiceStealing);
    latchIsOn          = dynamic_cast<AudioParameterBool*> (tree.getParameter("latchIsOn"));                jassert(latchIsOn);
    inputGain          = dynamic_cast<AudioParameterFloat*>(tree.getParameter("inputGain"));                jassert(inputGain);
    outputGain         = dynamic_cast<AudioParameterFloat*>(tree.getParameter("outputGain"));               jassert(outputGain);
    limiterToggle      = dynamic_cast<AudioParameterBool*> (tree.getParameter("limiterIsOn"));              jassert(limiterToggle);
    limiterThresh      = dynamic_cast<AudioParameterFloat*>(tree.getParameter("limiterThresh"));            jassert(limiterThresh);
    limiterRelease     = dynamic_cast<AudioParameterInt*>  (tree.getParameter("limiterRelease"));           jassert(limiterRelease);
};

ImogenAudioProcessor::~ImogenAudioProcessor()
{ };

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void ImogenAudioProcessor::prepareToPlay (const double sampleRate, const int samplesPerBlock)
{
    floatEngine.prepare(sampleRate, samplesPerBlock);
};



void ImogenAudioProcessor::reset()
{
    floatEngine.reset();
};


// audio rendering ----------------------------------------------------------------------------------------------------------------------------------

void ImogenAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    if( (host.isLogic() || host.isGarageBand()) && (getBusesLayout().getChannelSet(true, 1) == AudioChannelSet::disabled()) )
        return; // our audio input is disabled! can't do processing
    
    if (buffer.getNumSamples() == 0) // some hosts are crazy
        return;
    
    AudioBuffer<float> inBus  = AudioProcessor::getBusBuffer(buffer, true, (host.isLogic() || host.isGarageBand()));
    AudioBuffer<float> outBus = AudioProcessor::getBusBuffer(buffer, false, 0); // out bus must be configured to stereo
    
    floatEngine.process (inBus, outBus, midiMessages, wasBypassedLastCallback, false);
    
    wasBypassedLastCallback = false;
};


void ImogenAudioProcessor::processBlockBypassed (AudioBuffer<float>& buffer, MidiBuffer& midiMessages)
{
    if( (host.isLogic() || host.isGarageBand()) && (getBusesLayout().getChannelSet(true, 1) == AudioChannelSet::disabled()) )
        return;
    
    if (buffer.getNumSamples() == 0)
        return;
    
    AudioBuffer<float> inBus  = AudioProcessor::getBusBuffer(buffer, true, (host.isLogic() || host.isGarageBand()));
    AudioBuffer<float> outBus = AudioProcessor::getBusBuffer(buffer, false, 0); // out bus must be configured to stereo
    
    if (! wasBypassedLastCallback)
    {
        // this is the first callback of processBlockBypassed() after the bypass has been activated.
        // Process one more chunk and ramp the sound to 0 instead of killing the sound instantly
        
        floatEngine.process (inBus, outBus, midiMessages, false, true);
        wasBypassedLastCallback = true;
        return;
    }
    
    floatEngine.processBypassed (inBus, outBus, midiMessages);
    
    wasBypassedLastCallback = true;
};



/*===========================================================================================================================
 ============================================================================================================================*/



bool ImogenAudioProcessor::shouldWarnUserToEnableSidechain() const
{
    return getBusesLayout().getChannelSet(true, 1) == AudioChannelSet::disabled(); // only for Logic & Garageband
};


// functions for updating parameters ----------------------------------------------------------------------------------------------------------------

void ImogenAudioProcessor::updateAllParameters()
{
    updateIOgains();
    updateLimiter();
    updateDryVoxPan();
    updateDryWet();
    updateQuickKillMs();
    updateQuickAttackMs();
    updateNoteStealing();
    
    // these parameter functions have the potential to alter the pitch & other properties of currently playing harmonizer voices:
    updateConcertPitch();
    updatePitchbendSettings();
    updateStereoWidth();
    updateMidiVelocitySensitivity();
    updateAdsr();
    
    // these parameter functions have the potential to trigger or turn off midi notes / harmonizer voices:
    updateMidiLatch();
    updatePedalPitch();
    updateDescant();
};

void ImogenAudioProcessor::updateSampleRate(const double newSamplerate)
{
    floatEngine.updateSamplerate(newSamplerate);
};

void ImogenAudioProcessor::updateDryVoxPan()
{
    const int newDryPan = dryPan->get();
    
    if(newDryPan != prevDryPan)
    {
        const float Rpan = newDryPan / 127.0f;
        dryvoxpanningmults[1] = Rpan;
        dryvoxpanningmults[0] = 1.0f - Rpan;
        prevDryPan = newDryPan;
    }
};

void ImogenAudioProcessor::updateDryWet()
{
    floatEngine.updateDryWet(dryWet->get());
};

void ImogenAudioProcessor::updateAdsr()
{
    floatEngine.updateAdsr(adsrAttack->get(), adsrDecay->get(), adsrSustain->get(), adsrRelease->get(), adsrToggle->get());
};

void ImogenAudioProcessor::updateQuickKillMs()
{
    floatEngine.updateQuickKill(quickKillMs->get());
};

void ImogenAudioProcessor::updateQuickAttackMs()
{
    floatEngine.updateQuickAttack(quickAttackMs->get());
};

void ImogenAudioProcessor::updateStereoWidth()
{
    floatEngine.updateStereoWidth(stereoWidth->get(), lowestPanned->get());
};

void ImogenAudioProcessor::updateMidiVelocitySensitivity()
{
    floatEngine.updateMidiVelocitySensitivity(velocitySens->get());
};

void ImogenAudioProcessor::updatePitchbendSettings()
{
    floatEngine.updatePitchbendSettings(pitchBendUp->get(), pitchBendDown->get());
};

void ImogenAudioProcessor::updatePedalPitch()
{
    floatEngine.updatePedalPitch(pedalPitchIsOn->get(), pedalPitchThresh->get(), pedalPitchInterval->get());
};

void ImogenAudioProcessor::updateDescant()
{
    floatEngine.updateDescant(descantIsOn->get(), descantThresh->get(), descantInterval->get());
};

void ImogenAudioProcessor::updateConcertPitch()
{
    floatEngine.updateConcertPitch(concertPitchHz->get());
};

void ImogenAudioProcessor::updateNoteStealing()
{
    floatEngine.updateNoteStealing(voiceStealing->get());
};

void ImogenAudioProcessor::updateMidiLatch()
{
    floatEngine.updateMidiLatch(latchIsOn->get());
};

void ImogenAudioProcessor::updateIOgains()
{
    const float newIn = inputGain->get();
    if(newIn != prevideb)
        inputGainMultiplier = Decibels::decibelsToGain(newIn);
    prevideb = newIn;
    
    const float newOut = outputGain->get();
    if(newOut != prevodeb)
        outputGainMultiplier = Decibels::decibelsToGain(newOut);
    prevodeb = newOut;
};

void ImogenAudioProcessor::updateLimiter()
{
    limiterIsOn = limiterToggle->get();
    floatEngine.updateLimiter(limiterThresh->get(), limiterRelease->get());
};

void ImogenAudioProcessor::updatePitchDetectionSettings(const float newMinHz, const float newMaxHz, const float newTolerance)
{
    floatEngine.updatePitchDetectionSettings(newMinHz, newMaxHz, newTolerance);
};




/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// functions for custom preset management system ----------------------------------------------------------------------------------------------------

void ImogenAudioProcessor::savePreset(juce::String presetName) 
{
    // this function can be used both to save new preset files or to update existing ones
    
    File writingTo = getPresetsFolder().getChildFile(presetName);
    
    auto xml(tree.copyState().createXml());
   // xml->setAttribute("numberOfVoices", harmonizer.getNumVoices());
    xml->writeTo(writingTo);
    updateHostDisplay();
};


void ImogenAudioProcessor::loadPreset(juce::String presetName)
{
    File presetToLoad = getPresetsFolder().getChildFile(presetName);
    
    if(presetToLoad.existsAsFile())
    {
        auto xmlElement = juce::parseXML(presetToLoad);
        
        if(xmlElement.get() != nullptr && xmlElement->hasTagName (tree.state.getType()))
        {
            tree.replaceState(juce::ValueTree::fromXml (*xmlElement));
            updateNumVoices( xmlElement->getIntAttribute("numberOfVoices", 4) ); // TO DO : send notif to GUI to update numVoices comboBox
            updateHostDisplay();
        }
    }
};


void ImogenAudioProcessor::deletePreset(juce::String presetName) 
{
    File presetToDelete = getPresetsFolder().getChildFile(presetName);
    
    if(presetToDelete.existsAsFile())
        if(! presetToDelete.moveToTrash())
            presetToDelete.deleteFile();
    updateHostDisplay();
};


juce::File ImogenAudioProcessor::getPresetsFolder() const
{
    File rootFolder;
    
#ifdef JUCE_MAC
    rootFolder = File::getSpecialLocation(File::SpecialLocationType::userApplicationDataDirectory);
    rootFolder = rootFolder.getChildFile("Audio").getChildFile("Presets").getChildFile("Ben Vining Music Software").getChildFile("Imogen");
#endif
    
#ifdef JUCE_WINDOWS
    rootFolder = File::getSpecialLocation(File::SpecialLocationType::UserDocumentsDirectory);
    rootFolder = rootFolder.getChildFile("Ben Vining Music Software").getChildFile("Imogen");
#endif
    
#ifdef JUCE_LINUX
    rootFolder = File::getSpecialLocation(File::SpecialLocationType::userApplicationDataDirectory);
    rootFolder = rootFolder.getChildFile("Ben Vining Music Software").getChildFile("Imogen");
#endif
    
    if(! rootFolder.isDirectory() && ! rootFolder.existsAsFile())
        rootFolder.createDirectory(); // creates the presets folder if it doesn't already exist
    
    return rootFolder;
};


void ImogenAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto xml(tree.copyState().createXml());
 //   xml->setAttribute("numberOfVoices", harmonizer.getNumVoices());
    copyXmlToBinary (*xml, destData);
};


void ImogenAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xmlState(getXmlFromBinary(data, sizeInBytes));
    
    if (xmlState.get() != nullptr && xmlState->hasTagName (tree.state.getType()))
    {
        tree.replaceState(juce::ValueTree::fromXml (*xmlState));
        const int newNumOfVoices = xmlState->getIntAttribute("numberOfVoices", 4);
        updateNumVoices(newNumOfVoices); // TO DO : send notif to GUI to update numVoices comboBox
    }
};


// standard and general-purpose functions -----------------------------------------------------------------------------------------------------------

AudioProcessorValueTreeState::ParameterLayout ImogenAudioProcessor::createParameters() const
{
    std::vector<std::unique_ptr<RangedAudioParameter>> params;
    
    // general
    params.push_back(std::make_unique<AudioParameterInt>	("dryPan", "Dry vox pan", 0, 127, 64));
    
    params.push_back(std::make_unique<AudioParameterInt>	("masterDryWet", "% wet", 0, 100, 100));
    
    params.push_back(std::make_unique<AudioParameterInt>	("inputChan", "Input channel", 0, 16, 0));
    
    // ADSR
    params.push_back(std::make_unique<AudioParameterFloat> 	("adsrAttack", "ADSR Attack", NormalisableRange<float> (0.001f, 1.0f, 0.001f), 0.035f));
    params.push_back(std::make_unique<AudioParameterFloat> 	("adsrDecay", "ADSR Decay", NormalisableRange<float> (0.001f, 1.0f, 0.001f), 0.06f));
    params.push_back(std::make_unique<AudioParameterFloat> 	("adsrSustain", "ADSR Sustain", NormalisableRange<float> (0.01f, 1.0f, 0.01f), 0.8f));
    params.push_back(std::make_unique<AudioParameterFloat> 	("adsrRelease", "ADSR Release", NormalisableRange<float> (0.001f, 1.0f, 0.001f), 0.1f));
    params.push_back(std::make_unique<AudioParameterBool>	("adsrOnOff", "ADSR on/off", true));
    params.push_back(std::make_unique<AudioParameterInt>	("quickKillMs", "Quick kill ms", 1, 250, 15));
    params.push_back(std::make_unique<AudioParameterInt>	("quickAttackMs", "Quick attack ms", 1, 250, 15));
    
    // stereo width
    params.push_back(std::make_unique<AudioParameterInt> 	("stereoWidth", "Stereo Width", 0, 100, 100));
    params.push_back(std::make_unique<AudioParameterInt>	("lowestPan", "Lowest panned midiPitch", 0, 127, 0));
    
    // midi settings
    params.push_back(std::make_unique<AudioParameterInt> 	("midiVelocitySensitivity", "MIDI Velocity Sensitivity", 0, 100, 100));
    params.push_back(std::make_unique<AudioParameterInt> 	("PitchBendUpRange", "Pitch bend range (up)", 0, 12, 2));
    params.push_back(std::make_unique<AudioParameterInt>	("PitchBendDownRange", "Pitch bend range (down)", 0, 12, 2));
    // pedal pitch
    params.push_back(std::make_unique<AudioParameterBool>	("pedalPitchToggle", "Pedal pitch on/off", false));
    params.push_back(std::make_unique<AudioParameterInt>	("pedalPitchThresh", "Pedal pitch upper threshold", 0, 127, 0));
    params.push_back(std::make_unique<AudioParameterInt>	("pedalPitchInterval", "Pedal pitch interval", 1, 12, 12));
    // descant
    params.push_back(std::make_unique<AudioParameterBool>	("descantToggle", "Descant on/off", false));
    params.push_back(std::make_unique<AudioParameterInt>	("descantThresh", "Descant lower threshold", 0, 127, 127));
    params.push_back(std::make_unique<AudioParameterInt>	("descantInterval", "Descant interval", 1, 12, 12));
    // concert pitch Hz
    params.push_back(std::make_unique<AudioParameterInt>	("concertPitch", "Concert pitch (Hz)", 392, 494, 440));
    // voice stealing
    params.push_back(std::make_unique<AudioParameterBool>	("voiceStealing", "Voice stealing", false));
    // midi latch
    params.push_back(std::make_unique<AudioParameterBool>	("latchIsOn", "MIDI latch on/off", false));
    
    // input & output gain
    params.push_back(std::make_unique<AudioParameterFloat>	("inputGain", "Input Gain", NormalisableRange<float>(-60.0f, 0.0f, 0.01f), 0.0f));
    params.push_back(std::make_unique<AudioParameterFloat>	("outputGain", "Output Gain", NormalisableRange<float>(-60.0f, 0.0f, 0.01f), -4.0f));
    
    // output limiter
    params.push_back(std::make_unique<AudioParameterBool>	("limiterIsOn", "Limiter on/off", true));
    params.push_back(std::make_unique<AudioParameterFloat>	("limiterThresh", "Limiter threshold", NormalisableRange<float>(-60.0f, 0.0f, 0.01f), -2.0f));
    params.push_back(std::make_unique<AudioParameterInt>	("limiterRelease", "limiter release (ms)", 1, 250, 10));
    
    return { params.begin(), params.end() };
};


double ImogenAudioProcessor::getTailLengthSeconds() const
{
//    if(harmonizer.isADSRon())
//        return double(adsrRelease->get()); // ADSR release time in seconds
//
    return double(quickKillMs->get() * 1000.0f); // "quick kill" time in seconds
};

int ImogenAudioProcessor::getNumPrograms() {
    return 1;   // NB: some hosts don't cope very well if you tell them there are 0 programs, so this should be at least 1, even if you're not really implementing programs.
};

int ImogenAudioProcessor::getCurrentProgram() {
    return 0;
};

void ImogenAudioProcessor::setCurrentProgram (int index)
{ };

const juce::String ImogenAudioProcessor::getProgramName (int index) {
    return {};
};

void ImogenAudioProcessor::changeProgramName (int index, const juce::String& newName)
{ };


AudioProcessor::BusesProperties ImogenAudioProcessor::makeBusProperties() const
{
    if (host.isLogic() || host.isGarageBand())
        return BusesProperties().withInput ("Input",     AudioChannelSet::stereo(), true)
                                .withInput ("Sidechain", AudioChannelSet::mono(),   true)
                                .withOutput("Output",    AudioChannelSet::stereo(), true);
    
    return     BusesProperties().withInput ("Input",     AudioChannelSet::stereo(), true)
                                .withOutput("Output",    AudioChannelSet::stereo(), true);
};


bool ImogenAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    if ( (layouts.getMainInputChannelSet()  == juce::AudioChannelSet::disabled()) && (! (host.isLogic() || host.isGarageBand())) )
        return false;
    
    if ( layouts.getMainOutputChannelSet() == juce::AudioChannelSet::disabled() )
        return false;
    
    if ( layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;
    
    return true;
};


bool ImogenAudioProcessor::canAddBus(bool isInput) const
{
    if (! host.isLogic() || host.isGarageBand())
        return false;
    
    return isInput;
};


void ImogenAudioProcessor::updateTrackProperties (const TrackProperties& properties)
{
    String trackName   = properties.name;   // The name   of the track - this will be empty if the track name is not known
    Colour trackColour = properties.colour; // The colour of the track - this will be transparentBlack if the colour is not known
    
    if (trackName != "")
    {
        // do something cool with the name of the mixer track the plugin is loaded on
    }
    
    if (trackColour != Colours::transparentBlack)
    {
        // do something cool with the colour of the mixer track the plugin is loaded on
    }
};


juce::AudioProcessorEditor* ImogenAudioProcessor::createEditor()
{
    return new ImogenAudioProcessorEditor(*this);
};


// This creates new instances of the plugin..
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ImogenAudioProcessor();
};




template<typename SampleType>
ImogenEngine<SampleType>::ImogenEngine(ImogenAudioProcessor& p):
    processor(p), inBuffer(1, MAX_BUFFERSIZE), wetBuffer(2, MAX_BUFFERSIZE), dryBuffer(2, MAX_BUFFERSIZE), monoSummingBuffer(1, MAX_BUFFERSIZE * 2)
{
    initialize (44100.0, MAX_BUFFERSIZE, 12);
};


template<typename SampleType>
ImogenEngine<SampleType>::~ImogenEngine()
{
    
};


template<typename SampleType>
void ImogenEngine<SampleType>::initialize (const double initSamplerate, const int initSamplesPerBlock, const int initNumVoices)
{
    for (int i = 0; i < initNumVoices; ++i)
        harmonizer.addVoice(new HarmonizerVoice<SampleType>(&harmonizer));
    
    harmonizer.newMaxNumVoices(std::max(initNumVoices, MAX_POSSIBLE_NUMBER_OF_VOICES));
    harmonizer.setPitchDetectionRange(40.0, 2000.0);
    harmonizer.setPitchDetectionTolerance(0.15);
    
    // setLatencySamples(newLatency); // TOTAL plugin latency!
    
    dryWetMixer.setMixingRule(dsp::DryWetMixingRule::linear);
    
    prepare (initSamplerate, std::max(initSamplesPerBlock, MAX_BUFFERSIZE));
};


template<typename SampleType>
void ImogenEngine<SampleType>::prepare (double sampleRate, int samplesPerBlock)
{
    // setLatencySamples(newLatency); // TOTAL plugin latency!
    
    updateSamplerate(sampleRate);
    
    wetBuffer.setSize(2, samplesPerBlock, true, true, true);
    dryBuffer.setSize(2, samplesPerBlock, true, true, true);
    inBuffer .setSize(1, samplesPerBlock, true, true, true);
    harmonizer.increaseBufferSizes(samplesPerBlock);
    
    monoSummingBuffer.setSize(1, samplesPerBlock);
    
    dspSpec.maximumBlockSize = samplesPerBlock;
    dspSpec.sampleRate = sampleRate;
    dspSpec.maximumBlockSize = samplesPerBlock;
    dspSpec.numChannels = 2;
    
    limiter.prepare(dspSpec);
    
    dryWetMixer.prepare(dspSpec);
    dryWetMixer.setWetLatency(2); // latency in samples of the ESOLA algorithm
    
    bypassDelay.prepare(dspSpec);
    bypassDelay.setDelay(2); // latency in samples of the ESOLA algorithm
    
    harmonizer.resetNoteOnCounter(); // ??
    
    clearBuffers();
};


template<typename SampleType>
void ImogenEngine<SampleType>::reset()
{
    harmonizer.allNotesOff(false);
    harmonizer.resetNoteOnCounter();
    clearBuffers();
    dryWetMixer.reset();
    limiter.reset();
    bypassDelay.reset();
};


template<typename SampleType>
void ImogenEngine<SampleType>::process (AudioBuffer<SampleType>& inBus, AudioBuffer<SampleType>& output, MidiBuffer& midiMessages,
                                               const bool applyFadeIn, const bool applyFadeOut)
{
    updateSamplerate(processor.getSampleRate());
    
    AudioBuffer<SampleType> input; // input needs to be a MONO buffer!
    
    const int totalNumSamples = inBus.getNumSamples();
    
    switch (processor.modulatorInput)
    {
        case ImogenAudioProcessor::ModulatorInputSource::left:
            input = AudioBuffer<SampleType> (inBus.getArrayOfWritePointers(), 1, totalNumSamples);
            break;
            
        case ImogenAudioProcessor::ModulatorInputSource::right:
            input = AudioBuffer<SampleType> (inBus.getArrayOfWritePointers() + (inBus.getNumChannels() > 1), 1, totalNumSamples);
            break;
            
        case ImogenAudioProcessor::ModulatorInputSource::mixToMono:
        {
            if (inBus.getNumChannels() == 1)
            {
                input = AudioBuffer<SampleType> (inBus.getArrayOfWritePointers(), 1, totalNumSamples);
                break;
            }
            
            if(processor.isNonRealtime() && monoSummingBuffer.getNumSamples() < totalNumSamples)
                monoSummingBuffer.setSize(1, totalNumSamples);
            
            monoSummingBuffer.copyFrom(0, 0, inBus, 0, 0, totalNumSamples);
            
            const int totalNumChannels = inBus.getNumChannels();
            
            for(int channel = 1; channel < totalNumChannels; ++channel)
                monoSummingBuffer.addFrom(0, 0, inBus, channel, 0, totalNumSamples);
            
            monoSummingBuffer.applyGain(0, totalNumSamples, 1.0f / totalNumChannels);
            
            input = AudioBuffer<SampleType> (monoSummingBuffer.getArrayOfWritePointers(), 1, totalNumSamples);
            break;
        }
    }
    
    auto midiIterator = midiMessages.findNextSamplePosition(0);
    
    int  numSamples  = totalNumSamples;
    int  startSample = 0;
    bool firstEvent  = true;
    
    harmonizer.clearMidiBuffer();
    
    for (; numSamples > 0; ++midiIterator)
    {
        if (midiIterator == midiMessages.cend())
        {
            renderBlock (input, output, startSample, numSamples);
            break;
        }
        
        const auto metadata = *midiIterator;
        const int  samplePosition = metadata.samplePosition;
        const int  samplesToNextMidiMessage = samplePosition - startSample;
        
        if (samplesToNextMidiMessage >= numSamples)
        {
            renderBlock (input, output, startSample, numSamples);
            harmonizer.handleMidiEvent(metadata.getMessage(), samplePosition);
            break;
        }
        
        if (firstEvent && samplesToNextMidiMessage == 0)
        {
            harmonizer.handleMidiEvent(metadata.getMessage(), samplePosition);
            continue;
        }
        
        firstEvent = false;
        
        renderBlock (input, output, startSample, samplesToNextMidiMessage);
        harmonizer.handleMidiEvent(metadata.getMessage(), samplePosition);
        
        startSample += samplesToNextMidiMessage;
        numSamples  -= samplesToNextMidiMessage;
    }
    
    std::for_each (midiIterator,
                   midiMessages.cend(),
                   [&] (const MidiMessageMetadata& meta) { harmonizer.handleMidiEvent (meta.getMessage(), meta.samplePosition); } );
    
    midiMessages.swapWith(harmonizer.returnMidiBuffer());
    
    if (applyFadeOut)
        output.applyGainRamp(0, totalNumSamples, 1.0f, 0.0f);
    
    if (applyFadeIn)
        output.applyGainRamp(0, totalNumSamples, 0.0f, 1.0f);
};


template<typename SampleType>
void ImogenEngine<SampleType>::processBypassed (AudioBuffer<SampleType>& inBus, AudioBuffer<SampleType>& output, MidiBuffer& midiMessages)
{
    updateSamplerate(processor.getSampleRate());
    
    if (output.getNumChannels() > inBus.getNumChannels())
        for (int chan = inBus.getNumChannels(); chan < output.getNumChannels(); ++chan)
            output.clear(chan, 0, output.getNumSamples());
    
    dsp::AudioBlock<SampleType> inBlock  (inBus);
    dsp::AudioBlock<SampleType> outBlock (output);
    
    // delay line for latency compensation, so that DAW track's total latency will not change whether or not plugin bypass is active
    if (inBlock == outBlock)
        bypassDelay.process (dsp::ProcessContextReplacing   <SampleType> (inBlock) );
    else
        bypassDelay.process (dsp::ProcessContextNonReplacing<SampleType> (inBlock, outBlock));

    ignoreUnused(midiMessages); // midi passes through unaffected
};


template<typename SampleType>
void ImogenEngine<SampleType>::renderBlock (AudioBuffer<SampleType>& input, AudioBuffer<SampleType>& output,
                                       const int startSample, const int numSamples)
{
    if (processor.isNonRealtime())
    {
        AudioBuffer<SampleType> inProxy  (input .getArrayOfWritePointers(), 1, startSample, numSamples);
        AudioBuffer<SampleType> outProxy (output.getArrayOfWritePointers(), 2, startSample, numSamples);
        
        if (wetBuffer.getNumSamples() < numSamples)
            resizeBuffers(numSamples);
        
        renderChunk (inProxy, outProxy);
    }
    else
    {
        int chunkStartSample = startSample;
        int samplesLeft      = numSamples;
        
        while(samplesLeft > 0)
        {
            const int chunkNumSamples = std::min(samplesLeft, wetBuffer.getNumSamples());
            
            AudioBuffer<SampleType> inProxy  (input .getArrayOfWritePointers(), 1, chunkStartSample, chunkNumSamples);
            AudioBuffer<SampleType> outProxy (output.getArrayOfWritePointers(), 2, chunkStartSample, chunkNumSamples);
            
            renderChunk (inProxy, outProxy);
            
            chunkStartSample += chunkNumSamples;
            samplesLeft      -= chunkNumSamples;
        }
    }
};


template<typename SampleType>
void ImogenEngine<SampleType>::renderChunk (const AudioBuffer<SampleType>& input, AudioBuffer<SampleType>& output)
{
    // regardless of the input channel(s) setup, the inBuffer fed to this function should be a mono buffer with its audio content in channel 0
    // outBuffer should be a stereo buffer with the same length in samples as inBuffer
    // # of samples in the I/O buffers must be less than or equal to MAX_BUFFERSIZE
    
    updateSamplerate(processor.getSampleRate());
    
    const int numSamples = input.getNumSamples();
    
    AudioBuffer<SampleType> inBufferProxy (inBuffer.getArrayOfWritePointers(), 1, 0, numSamples);
    
    inBufferProxy.copyFrom(0, 0, input, 0, 0, numSamples); // copy to input storage buffer so that input gain can be applied
    
    inBufferProxy.applyGain(processor.inputGainMultiplier); // apply input gain
    
    writeToDryBuffer(inBufferProxy); // puts input samples into dryBuffer w/ proper panning applied
    
    AudioBuffer<SampleType> dryProxy (dryBuffer.getArrayOfWritePointers(), 2, 0, numSamples);
    AudioBuffer<SampleType> wetProxy (wetBuffer.getArrayOfWritePointers(), 2, 0, numSamples);
    
    dryWetMixer.pushDrySamples( dsp::AudioBlock<SampleType>(dryProxy) );
    
    harmonizer.renderVoices (inBufferProxy, wetProxy); // puts the harmonizer's rendered stereo output into "wetProxy" (= "wetBuffer")
    
    dryWetMixer.mixWetSamples( dsp::AudioBlock<SampleType>(wetProxy) ); // puts the mixed dry & wet samples into "wetProxy" (= "wetBuffer")
    
    output.makeCopyOf(wetProxy, true); // transfer from wetBuffer to output buffer
    
    output.applyGain(processor.outputGainMultiplier); // apply master output gain
    
    // output limiter
    if(processor.limiterIsOn)
    {
        dsp::AudioBlock<SampleType> limiterBlock (output);
        limiter.process(dsp::ProcessContextReplacing<SampleType>(limiterBlock));
    }
};


template<typename SampleType>
void ImogenEngine<SampleType>::writeToDryBuffer (const AudioBuffer<SampleType>& input)
{
    const int numSamples = input.getNumSamples();
    
    dryBuffer.copyFrom (0, 0, input, 0, 0, numSamples);
    dryBuffer.copyFrom (1, 0, input, 0, 0, numSamples);
    dryBuffer.applyGain(0, 0, numSamples, processor.dryvoxpanningmults[0]);
    dryBuffer.applyGain(1, 0, numSamples, processor.dryvoxpanningmults[1]);
};


template<typename SampleType>
void ImogenEngine<SampleType>::resizeBuffers(const int newBlocksize)
{
    wetBuffer.setSize(2, newBlocksize, true, true, true);
    dryBuffer.setSize(2, newBlocksize, true, true, true);
    inBuffer .setSize(1, newBlocksize, true, true, true);
    harmonizer.increaseBufferSizes(newBlocksize);
    
    monoSummingBuffer.setSize(1, newBlocksize);
    
    dspSpec.maximumBlockSize = newBlocksize;
    limiter.prepare(dspSpec);
    dryWetMixer.prepare(dspSpec);
    bypassDelay.prepare(dspSpec);
};


template<typename SampleType>
void ImogenEngine<SampleType>::clearBuffers()
{
    harmonizer.clearBuffers();
    harmonizer.clearMidiBuffer();
    wetBuffer.clear();
    dryBuffer.clear();
    inBuffer.clear();
    monoSummingBuffer.clear();
};

template<typename SampleType>
void ImogenEngine<SampleType>::updateNumVoices(const int newNumVoices)
{
    const int currentVoices = harmonizer.getNumVoices();
    
    if(currentVoices != newNumVoices)
    {
        if(newNumVoices > currentVoices)
        {
            processor.suspendProcessing (true);
            
            for(int i = 0; i < newNumVoices - currentVoices; ++i)
                harmonizer.addVoice(new HarmonizerVoice<SampleType>(&harmonizer));
            
            harmonizer.newMaxNumVoices(std::max(newNumVoices, MAX_POSSIBLE_NUMBER_OF_VOICES));
            // increases storage overheads for internal harmonizer functions dealing with arrays of notes, etc
            
            processor.suspendProcessing (false);
        }
        else
            harmonizer.removeNumVoices(currentVoices - newNumVoices);
        
        // update GUI numVoices ComboBox
    }
};

template<typename SampleType>
void ImogenEngine<SampleType>::updateSamplerate(const int newSamplerate)
{
    if(harmonizer.getSamplerate() != newSamplerate)
        harmonizer.setCurrentPlaybackSampleRate(newSamplerate);
};

template<typename SampleType>
void ImogenEngine<SampleType>::updateDryWet(const float newWetMixProportion)
{
    dryWetMixer.setWetMixProportion(newWetMixProportion);
    
    // need to set latency!!!
};

template<typename SampleType>
void ImogenEngine<SampleType>::updateAdsr(const float attack, const float decay, const float sustain, const float release, const bool isOn)
{
    harmonizer.updateADSRsettings(attack, decay, sustain, release);
    harmonizer.setADSRonOff(isOn);
};

template<typename SampleType>
void ImogenEngine<SampleType>::updateQuickKill(const int newMs)
{
    harmonizer.updateQuickReleaseMs(newMs);
};

template<typename SampleType>
void ImogenEngine<SampleType>::updateQuickAttack(const int newMs)
{
    harmonizer.updateQuickAttackMs(newMs);
};

template<typename SampleType>
void ImogenEngine<SampleType>::updateStereoWidth(const int newStereoWidth, const int lowestPannedNote)
{
    harmonizer.updateLowestPannedNote(lowestPannedNote);
    harmonizer.updateStereoWidth     (newStereoWidth);
};

template<typename SampleType>
void ImogenEngine<SampleType>::updateMidiVelocitySensitivity(const int newSensitivity)
{
    harmonizer.updateMidiVelocitySensitivity(newSensitivity);
};

template<typename SampleType>
void ImogenEngine<SampleType>::updatePitchbendSettings(const int rangeUp, const int rangeDown)
{
    harmonizer.updatePitchbendSettings(rangeUp, rangeDown);
};

template<typename SampleType>
void ImogenEngine<SampleType>::updatePedalPitch(const bool isOn, const int upperThresh, const int interval)
{
    harmonizer.setPedalPitch           (isOn);
    harmonizer.setPedalPitchUpperThresh(upperThresh);
    harmonizer.setPedalPitchInterval   (interval);
};

template<typename SampleType>
void ImogenEngine<SampleType>::updateDescant(const bool isOn, const int lowerThresh, const int interval)
{
    harmonizer.setDescant           (isOn);
    harmonizer.setDescantLowerThresh(lowerThresh);
    harmonizer.setDescantInterval   (interval);
};

template<typename SampleType>
void ImogenEngine<SampleType>::updateConcertPitch(const int newConcertPitchHz)
{
    harmonizer.setConcertPitchHz(newConcertPitchHz);
};

template<typename SampleType>
void ImogenEngine<SampleType>::updateNoteStealing(const bool shouldSteal)
{
    harmonizer.setNoteStealingEnabled(shouldSteal);
};

template<typename SampleType>
void ImogenEngine<SampleType>::updateMidiLatch(const bool isLatched)
{
    harmonizer.setMidiLatch(isLatched, true);
};

template<typename SampleType>
void ImogenEngine<SampleType>::updateLimiter(const float thresh, const float release)
{
    limiter.setThreshold(thresh);
    limiter.setRelease(release);
};

template<typename SampleType>
void ImogenEngine<SampleType>::updatePitchDetectionSettings(const float newMinHz, const float newMaxHz, const float newTolerance)
{
    harmonizer.setPitchDetectionRange(newMinHz, newMaxHz);
    harmonizer.setPitchDetectionTolerance(newTolerance);
};



template class ImogenEngine<float>;
template class ImogenEngine<double>;
