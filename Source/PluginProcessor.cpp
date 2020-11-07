#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
ImogenAudioProcessor::ImogenAudioProcessor()
#ifndef JucePlugin_PreferredChannelConfigurations
     : AudioProcessor (BusesProperties()
                     #if ! JucePlugin_IsMidiEffect
                      #if ! JucePlugin_IsSynth
                       .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                      #endif
                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                     #endif
                       ),
tree (*this, nullptr)
#endif
{
	
	tree.createAndAddParameter("adsrAttack", "ADSR Attack", "ADSR Attack", NormalisableRange<float> (0.01f, 1.0f), 0.035f, nullptr, nullptr);
	tree.createAndAddParameter("adsrDecay", "ADSR Decay", "ADSR Decay", NormalisableRange<float> (0.01f, 1.0f), 0.06f, nullptr, nullptr);
	tree.createAndAddParameter("adsrSustain", "ADSR Sustain", "ADSR Sustain", NormalisableRange<float> (0.01f, 1.0f), 0.8f, nullptr, nullptr);
	tree.createAndAddParameter("adsrRelease", "ADSR Release", "ADSR Release", NormalisableRange<float> (0.01f, 1.0f), 0.1f, nullptr, nullptr);
	tree.createAndAddParameter("stereoWidth", "Stereo Width", "Stereo Width", NormalisableRange<float> (0.0, 100.0), 100, nullptr, nullptr);
	tree.createAndAddParameter("midiVelocitySensitivity", "MIDI Velocity Sensitivity", "MIDI Velocity Sensitivity", NormalisableRange<float> (0.0, 100.0), 100, nullptr, nullptr);
	
	// initializes each instance of the HarmonyVoice class inside the harmEngine array:
	for (int i = 0; i < numVoices; ++i) {
		harmEngine.add(new HarmonyVoice(i));
	}
}

ImogenAudioProcessor::~ImogenAudioProcessor() {
	for (int i = 0; i < numVoices; ++i) {
		delete harmEngine[i];
	}
}

//==============================================================================
const juce::String ImogenAudioProcessor::getName() const {
    return JucePlugin_Name;
}

bool ImogenAudioProcessor::acceptsMidi() const {
   #if JucePlugin_WantsMidiInput
    return true;
   #else
    return false;
   #endif
}

bool ImogenAudioProcessor::producesMidi() const {
   #if JucePlugin_ProducesMidiOutput
    return true;
   #else
    return false;
   #endif
}

bool ImogenAudioProcessor::isMidiEffect() const {
   #if JucePlugin_IsMidiEffect
    return true;
   #else
    return false;
   #endif
}

double ImogenAudioProcessor::getTailLengthSeconds() const {
    return 0.0;
}

int ImogenAudioProcessor::getNumPrograms() {
    return 1;   // NB: some hosts don't cope very well if you tell them there are 0 programs,
                // so this should be at least 1, even if you're not really implementing programs.
}

int ImogenAudioProcessor::getCurrentProgram() {
    return 0;
}

void ImogenAudioProcessor::setCurrentProgram (int index) {
}

const juce::String ImogenAudioProcessor::getProgramName (int index) {
    return {};
}

void ImogenAudioProcessor::changeProgramName (int index, const juce::String& newName) {
}

//==============================================================================
void ImogenAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock) {
	
	lastSampleRate = sampleRate;
	lastBlockSize = samplesPerBlock;
	
	for (int i = 0; i < numVoices; ++i) {
		harmEngine[i]->updateDSPsettings(lastSampleRate, lastBlockSize);
		harmEngine[i]->adsrSettingsListener(adsrAttackListener, adsrDecayListener, adsrSustainListener, adsrReleaseListener, midiVelocitySensListener);
	}
}

void ImogenAudioProcessor::releaseResources() {
    // When playback stops, you can use this as an opportunity to free up any
    // spare memory, etc.
	
	for (int i = 0; i < numVoices; ++i) {
		harmEngine[i]->voiceIsOn = false;
	}
}

#ifndef JucePlugin_PreferredChannelConfigurations
bool ImogenAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
  #if JucePlugin_IsMidiEffect
    juce::ignoreUnused (layouts);
    return true;
  #else
    // This is the place where you check if the layout is supported.
    // In this template code we only support mono or stereo.
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
     && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    // This checks if the input layout matches the output layout
   #if ! JucePlugin_IsSynth
    if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
        return false;
   #endif

    return true;
  #endif
}
#endif



////==============================================================================////
////==============================================================================////


void ImogenAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    auto totalNumInputChannels  = getTotalNumInputChannels();
    auto totalNumOutputChannels = getTotalNumOutputChannels();
	
	for (auto i = totalNumInputChannels; i < totalNumOutputChannels; ++i) {
        buffer.clear (i, 0, buffer.getNumSamples());
	}
	
	if(previousStereoWidth != *stereoWidthListener) {  // update stereo width, if the value has changed
		midiProcessor.updateStereoWidth(stereoWidthListener);
	}
	previousStereoWidth = *stereoWidthListener;
	
	midiProcessor.processIncomingMidi(midiMessages, harmEngine);
	
	// need to update the voxCurrentPitch variable!!
	// identify grain lengths & peak locations ONCE based on input signal, then pass info to individual instances of shifter ?
	
	// this for loop steps through each of the 12 instances of HarmonyVoice to render their audio:
	for (int i = 0; i < numVoices; i++) {  // i = the harmony voice # currently being processed
		if (harmEngine[i]->voiceIsOn == true) {  // only do audio processing on active voices:
			
			// update ADSR parameters
			
			if(prevAttack != *adsrAttackListener || prevDecay != *adsrDecayListener || prevSustain != *adsrSustainListener || prevRelease != *adsrReleaseListener || prevVelocitySens != *midiVelocitySensListener) {
			
			harmEngine[i]->adsrSettingsListener(adsrAttackListener, adsrDecayListener, adsrSustainListener, adsrReleaseListener, midiVelocitySensListener);
			}
	
			// render next audio vector
			harmEngine[i]->renderNextBlock(buffer, 0, buffer.getNumSamples(), voxCurrentPitch);
		}
	}
	// goal is to add all 12 voices together into a master audio signal for harmEngine, which can then be mixed with the original input signal (dry/wet)
	
	prevAttack = *adsrAttackListener;
	prevDecay = *adsrDecayListener;
	prevSustain = *adsrSustainListener;
	prevRelease = *adsrReleaseListener;
	prevVelocitySens = *midiVelocitySensListener;
}


//==============================================================================
//==============================================================================



HarmonyVoice& returnVoice(int voiceNumber) {
//	harmEngine[voiceNumber]
}




bool ImogenAudioProcessor::hasEditor() const {
    return true; // (change this to false if you choose to not supply an editor)
}

juce::AudioProcessorEditor* ImogenAudioProcessor::createEditor() {
    return new ImogenAudioProcessorEditor (*this);
}

//==============================================================================
void ImogenAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    // You should use this method to store your parameters in the memory block.
    // You could do that either as raw data, or use the XML or ValueTree classes
    // as intermediaries to make it easy to save and load complex data.
}

void ImogenAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    // You should use this method to restore your parameters from this memory block,
    // whose contents will have been created by the getStateInformation() call.
}

//==============================================================================
// This creates new instances of the plugin..
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ImogenAudioProcessor();
}
