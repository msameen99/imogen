/*
  ==============================================================================

    PitchDetector.cpp
    Created: 18 Jan 2021 11:31:33am
    Author:  Ben Vining

  ==============================================================================
*/

#include "PitchDetector/PitchDetector.h"


template<typename SampleType>
PitchDetector<SampleType>::PitchDetector(const int minHz, const int maxHz, const double samplerate): confidenceThresh(0.25)
{
    this->minHz = minHz;
    this->maxHz = maxHz;
    this->samplerate = samplerate;
    
    asdfBuffer.setSize (1, 500);
    
    setHzRange (minHz, maxHz, true);
    
    lastEstimatedPeriod = minPeriod;
};


template<typename SampleType>
PitchDetector<SampleType>::~PitchDetector()
{
    
};


template<typename SampleType>
void PitchDetector<SampleType>::setHzRange (const int newMinHz, const int newMaxHz, const bool allowRecalc)
{
    jassert (newMaxHz > newMinHz);
    
    if ((! allowRecalc)
        && ((minHz == newMinHz) && (maxHz == newMaxHz)))
        return;
    
    maxPeriod = roundToInt (samplerate / minHz);
    minPeriod = roundToInt (samplerate / maxHz);
    
    if (! (maxPeriod > minPeriod))
        ++maxPeriod;
    
    const int numOfLagValues = maxPeriod - minPeriod + 1;
    
    if (asdfBuffer.getNumSamples() != numOfLagValues)
        asdfBuffer.setSize (1, numOfLagValues, true, true, true);
};


template<typename SampleType>
void PitchDetector<SampleType>::setSamplerate (const double newSamplerate, const bool recalcHzRange)
{
    if (samplerate == newSamplerate)
        return;
    
    if (lastFrameWasPitched)
    {
        SampleType lastHz = samplerate / lastEstimatedPeriod;
        lastEstimatedPeriod = newSamplerate / lastHz;
    }
    
    samplerate = newSamplerate;
    
    if (recalcHzRange)
        setHzRange (minHz, maxHz, true);
};


template<typename SampleType>
float PitchDetector<SampleType>::detectPitch (const AudioBuffer<SampleType>& inputAudio)
{
    // this function should return the pitch in Hz, or -1 if the frame of audio is determined to be unpitched
    
    const int numSamples = inputAudio.getNumSamples();
    
    if (numSamples < minPeriod)
        return -1.0f;
    
    jassert (asdfBuffer.getNumSamples() > (maxPeriod - minPeriod));
    
    const SampleType* reading = inputAudio.getReadPointer(0);
    
    int minLag = samplesToFirstZeroCrossing (reading, numSamples); // little trick to avoid picking too small a period
    int maxLag = maxPeriod;
    
    if (lastFrameWasPitched) // pitch shouldn't halve or double between consecutive voiced frames...
    {
        minLag = std::max (minLag, roundToInt (lastEstimatedPeriod / 2.0));
        maxLag = std::min (maxLag, roundToInt (lastEstimatedPeriod * 2.0));
    }
    
    minLag = std::max (minLag, minPeriod);
    
    if (maxLag < minLag)
        maxLag = minLag + 1;
    else if (minLag == maxLag)
        ++maxLag;
    
    const int middleIndex = floor (numSamples / 2.0f);
    const int halfNumSamples = floor ((numSamples - 1) / 2.0f);
    
    SampleType* asdfData = asdfBuffer.getWritePointer(0);
    
    // in the ASDF buffer, the value stored at index 0 is the ASDF for lag minPeriod.
    // the value stored at the maximum index is the ASDF for lag maxPeriod.
    // always write the same datasize to the ASDF buffer (with regard to this member variables), even if the k range is being limited this frame by the minLag & maxLag local variables.
    
    // STEP 1 - COMPUTE ASDF
    
    for (int k = minPeriod; // always write the same datasize to asdfBuffer, even if k values are being limited this frame
            k <= maxPeriod; // k = delay = lag = period
            ++k)
    {
        const int index = k - minPeriod; // the actual asdfBuffer index for this k value's data
        
        if (k < minLag || k > maxLag) // range compression of k is done heres
        {
            asdfData[index] = 2.0;
            continue;
        }
        
        asdfData[index] = 0.0;
        
        const int sampleOffset = middleIndex - (floor (k / 2.0f));
        
        for (int s = sampleOffset - halfNumSamples;
                 s < sampleOffset + halfNumSamples;
               ++s)
        {
            const SampleType difference = reading[s] - reading[s + k];
            asdfData[index] += (difference * difference);
        }
        
        asdfData[index] /= numSamples; // normalize
    }
    
    const int asdfDataSize = maxPeriod - minPeriod + 1; // # of samples written to asdfBuffer
    
    const int minIndex = indexOfMinElement (asdfData, asdfDataSize);
    
    const SampleType greatestConfidence = asdfData[minIndex];
    
    if (greatestConfidence > confidenceThresh) // determine if frame is unpitched - return early
    {
        lastFrameWasPitched = false;
        return -1.0f;
    }
    
    if ((! lastFrameWasPitched) || (greatestConfidence < 0.05)) // separate confidence threshold value for this??
        return foundThePeriod (asdfData, minIndex, asdfDataSize);
    
    return chooseIdealPeriodCandidate (asdfData, asdfDataSize, minIndex);
};



template<typename SampleType>
float PitchDetector<SampleType>::foundThePeriod (const SampleType* asdfData,
                                                 const int minIndex,
                                                 const int asdfDataSize)
{
    SampleType realPeriod = quadraticPeakPosition (asdfData, minIndex, asdfDataSize);
    realPeriod += minPeriod;
    
    jassert (realPeriod <= maxPeriod);
    
    lastEstimatedPeriod = realPeriod;
    lastFrameWasPitched = true;
    return static_cast<float> (samplerate / realPeriod); // return pitch in hz
};


template<typename SampleType>
float PitchDetector<SampleType>::chooseIdealPeriodCandidate (const SampleType* asdfData,
                                                             const int asdfDataSize,
                                                             const int minIndex) // index of minimum asdf data value
{
    const int periodCandidatesSize = std::min(periodCandidatesToTest, asdfDataSize);
    
    Array<int> periodCandidates;
    periodCandidates.ensureStorageAllocated (periodCandidatesSize);
    
    periodCandidates.add (minIndex);
    
    for (int c = 1; c < periodCandidatesSize; ++c)
        getNextBestPeriodCandidate (periodCandidates, asdfData, asdfDataSize);
    
    if (periodCandidates.size() == 1)
        return foundThePeriod (asdfData, minIndex, asdfDataSize);
    
    // find the greatest & least confidences of any candidate (ie, highest & lowest asdf data values)
    
    const SampleType greatestConfidence = asdfData[minIndex];
    SampleType leastConfidence = greatestConfidence;
    
    for (int c = 1; c < periodCandidatesSize; ++c)
    {
        const SampleType confidence = asdfData[periodCandidates.getUnchecked(c)];
        
        if (confidence > leastConfidence)
            leastConfidence = confidence;
    }
    
    // if there is little variation in the confidences of our candidates, return the smallest k value that is a candidate
    if ((leastConfidence - greatestConfidence) < 2.0)
    {
        int smallestK = periodCandidates.getUnchecked(0);
        
        for (int candidate : periodCandidates)
            if (candidate < smallestK)
                smallestK = candidate;
        
        return foundThePeriod (asdfData, smallestK, asdfDataSize);
    }
    
    // candidate deltas: how far away each period candidate is from the last estimated period
    int candidateDeltas[periodCandidatesSize];
    
    for (int c = 0; c < periodCandidatesSize; ++c)
        candidateDeltas[c] = abs(periodCandidates.getUnchecked(c) + minPeriod - lastEstimatedPeriod);
    
    // find min & max delta val of any candidate we have
    int minDelta = candidateDeltas[0];
    int maxDelta = candidateDeltas[0];
    
    for (int d = 1; d < periodCandidatesSize; ++d)
    {
        const int delta = candidateDeltas[d];
        
        if (delta < minDelta)
            minDelta = delta;
        
        if (delta > maxDelta)
            maxDelta = delta;
    }
    
    const int deltaRange = maxDelta - minDelta;
    
    if (deltaRange < 4) // all deltas are very close, so return the candidate with the min asdf data value
        return foundThePeriod (asdfData, minIndex, asdfDataSize);
    
    // weight the asdf data based on each candidate's delta value
    // because higher asdf values represent a lower confidence in that period candidate, we want to artificially increase the asdf data a bit for candidates with higher deltas
    SampleType weightedCandidateConfidence[periodCandidatesSize];
    
    for (int c = 0; c < periodCandidatesSize; ++c)
    {
        const int candidate = periodCandidates.getUnchecked(c);
        const int delta = candidateDeltas[c];
        
        if (delta == 0)
            weightedCandidateConfidence[c] = asdfData[candidate];
        else
        {
            const SampleType weight = 1.0 + ((delta / deltaRange) * 0.5);
            weightedCandidateConfidence[c] = asdfData[candidate] * weight;
        }
    }
    
    // choose the estimated period based on the lowest weighted asdf data value
    int indexOfPeriod = 0;
    SampleType confidence = weightedCandidateConfidence[0];
    
    for (int c = 1; c < periodCandidatesSize; ++c)
    {
        const SampleType current = weightedCandidateConfidence[c];
        
        if (current < confidence)
        {
            indexOfPeriod = c;
            confidence = current;
        }
    }
    
    return foundThePeriod (asdfData, periodCandidates.getUnchecked(indexOfPeriod), asdfDataSize);
};


template<typename SampleType>
void PitchDetector<SampleType>::getNextBestPeriodCandidate (Array<int>& candidates,
                                                            const SampleType* asdfData,
                                                            const int dataSize)
{
    int initIndex;
    
    if (! candidates.contains(0))
        initIndex = 0;
    else
    {
        initIndex = -1;
        
        for (int i = 1; i < dataSize; ++i)
        {
            if (! candidates.contains(i))
            {
                initIndex = i;
                break;
            }
        }
        
        if (initIndex == -1)
            return;
    }
    
    SampleType min = asdfData[initIndex];
    int minIndex = initIndex;
    
    for (int i = 0; i < dataSize; ++i)
    {
        if (i == initIndex)
            continue;
        
        if (candidates.contains(i))
            continue;
        
        const SampleType current = asdfData[i];
        
        if (current == 0.0)
        {
            candidates.add (i);
            return;
        }
        
        if (current < min)
        {
            min = current;
            minIndex = i;
        }
    }
    
    candidates.add (minIndex);
};


template<typename SampleType>
unsigned int PitchDetector<SampleType>::samplesToFirstZeroCrossing (const SampleType* inputAudio, const int numInputSamples)
{
    if (inputAudio[0] == 0.0)
        return 0;
    
    const bool startedPositive = inputAudio[0] > 0.0;
    
    for (int s = 1; s < floor (numInputSamples / 2.0f); ++s)
    {
        const auto currentSample = inputAudio[s];
        
        if (currentSample == 0.0)
            return s;
        
        const bool isNowPositive = currentSample > 0.0;
        
        if (startedPositive != isNowPositive)
            return s;
    }
    
    return 0;
};



template<typename SampleType>
int PitchDetector<SampleType>::indexOfMinElement (const SampleType* data, const int dataSize)
{
    // find minimum of ASDF output data - indicating that index (lag value) to be the period
    
    SampleType min = data[0];
    
    if (min == 0.0)
        return 0;
    
    int minIndex = 0;
    
    for (int n = 1; n < dataSize; ++n)
    {
        const SampleType current = data[n];
        
        if (current == 0.0)
            return n;
        
        if (current < min)
        {
            min = current;
            minIndex = n;
        }
    }
    
    return minIndex;
};


template<typename SampleType>
SampleType PitchDetector<SampleType>::quadraticPeakPosition (const SampleType* data, unsigned int pos, const int dataSize) noexcept
{
    if ((pos == 0) || ((pos + 1) >= dataSize)) // edge of data, can't interpolate
        return static_cast<SampleType> (pos);
    
    const auto posData = data[pos];
    
    if (posData == 0)
        return static_cast<SampleType> (pos);
    
    const auto s0 = data[pos - 1];
    const auto s2 = data[pos + 1];
    
    return pos + 0.5 * (s2 - s0) / (2.0 * posData - s2 - s0);
};


template class PitchDetector<float>;
template class PitchDetector<double>;
