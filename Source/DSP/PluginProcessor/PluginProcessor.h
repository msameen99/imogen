
#pragma once

#include "ImogenEngine/ImogenEngine.h"

#include "ImogenCommon/ImogenCommon.h"

#ifndef IMOGEN_HEADLESS
#define IMOGEN_HEADLESS 0
#endif

#if !IMOGEN_HEADLESS
#include <../../third-party/ableton-link/include/ableton/Link.hpp>
#endif


using namespace Imogen;


/*
*/

class ImogenAudioProcessor : public bav::dsp::ProcessorBase
{
    using Parameter = bav::Parameter;
    using RAP = juce::RangedAudioParameter;
    using ChangeDetails = juce::AudioProcessorListener::ChangeDetails;


public:
    ImogenAudioProcessor();
    ~ImogenAudioProcessor() override;

    juce::String getScaleName() const;

    juce::Point< int > getSavedEditorSize() const;
    void saveEditorSize (int width, int height);

    /*=========================================================================================*/

private:
    /*=========================================================================================*/
    /* juce::AudioProcessor functions */
    
    void prepareToPlay (double sampleRate, int samplesPerBlock) override final;
    
    void releaseResources() override final;
    
    void processBlock (juce::AudioBuffer< float >& buffer, juce::MidiBuffer& midiMessages) override final;
    void processBlock (juce::AudioBuffer< double >& buffer, juce::MidiBuffer& midiMessages) override final;
    
    void processBlockBypassed (juce::AudioBuffer< float >& buffer, juce::MidiBuffer& midiMessages) override final;
    void processBlockBypassed (juce::AudioBuffer< double >& buffer, juce::MidiBuffer& midiMessages) override final;
    
    bool canAddBus (bool isInput) const override final { return isInput; }
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override final;
    
    double getTailLengthSeconds() const override final;
    
    void getStateInformation (juce::MemoryBlock& destData) override final;
    void setStateInformation (const void* data, int sizeInBytes) override final;
    
    juce::AudioProcessorParameter* getBypassParameter() const override final;
    
    bool acceptsMidi() const override final { return true; }
    bool producesMidi() const override final { return true; }
    bool supportsMPE() const override final { return false; }
    bool isMidiEffect() const override final { return false; }
    
    const juce::String getName() const override final { return JucePlugin_Name; }
    juce::StringArray  getAlternateDisplayNames() const override final { return {"Imgn"}; }
    
    bool hasEditor() const override final;
    
    juce::AudioProcessorEditor* createEditor() override final;
    
    bool supportsDoublePrecisionProcessing() const override final { return true; }
    
    /*=========================================================================================*/
    /* Initialization functions */

    BusesProperties createBusProperties() const override final;
    
    template < typename SampleType >
    void initialize (bav::ImogenEngine< SampleType >& activeEngine);

    template < typename SampleType >
    void initializeParameterFunctionPointers (bav::ImogenEngine< SampleType >& engine);

    /*=========================================================================================*/

    template < typename SampleType1, typename SampleType2 >
    void prepareToPlayWrapped (const double sampleRate, bav::ImogenEngine< SampleType1 >& activeEngine, bav::ImogenEngine< SampleType2 >& idleEngine);

    template < typename SampleType >
    inline void processBlockWrapped (juce::AudioBuffer< SampleType >& buffer,
                                     juce::MidiBuffer&                midiMessages,
                                     bav::ImogenEngine< SampleType >& engine,
                                     const bool                       isBypassedThisCallback);

    void updateMeters (ImogenMeterData meterData);

    /*=========================================================================================*/

    // one engine of each type. The idle one isn't destroyed, but takes up few resources.
    bav::ImogenEngine< float >  floatEngine;
    bav::ImogenEngine< double > doubleEngine;
    
    Imogen::State state;
    Imogen::Parameters& parameters {state.parameters};
    Imogen::Meters& meters {state.meters};
    
#if !IMOGEN_HEADLESS
    ableton::Link abletonLink; // this object represents the plugin as a participant in an Ableton Link session
#endif

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ImogenAudioProcessor)
};
