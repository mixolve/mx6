#include "MultibandProcessor.h"

#include <algorithm>
#include <cmath>

namespace mxe::dsp
{
namespace
{
constexpr double epsilon = 1.0e-9;
}

void MultibandProcessor::SmoothedValue::snapTo(const double value) noexcept
{
    current = value;
    target = value;
    step = 0.0;
    remainingSteps = 0;
}

void MultibandProcessor::SmoothedValue::setTarget(const double value) noexcept
{
    target = value;
}

void MultibandProcessor::SmoothedValue::beginBlock(const int numSamples) noexcept
{
    if (numSamples <= 1 || std::abs(target - current) <= epsilon)
    {
        current = target;
        step = 0.0;
        remainingSteps = 0;
        return;
    }

    remainingSteps = numSamples - 1;
    step = (target - current) / static_cast<double>(remainingSteps);
}

void MultibandProcessor::SmoothedValue::advance() noexcept
{
    if (remainingSteps <= 0)
    {
        current = target;
        return;
    }

    current += step;
    --remainingSteps;

    if (remainingSteps == 0)
        current = target;
}

double MultibandProcessor::roundToJsfxStep(const double value)
{
    return std::floor((value * 10.0) + 0.5) * 0.1;
}

double MultibandProcessor::dbToAmp(const double decibels)
{
    return std::pow(10.0, decibels / 20.0);
}

int MultibandProcessor::wrapIndex(int index, const int size)
{
    jassert(size > 0);

    index %= size;

    if (index < 0)
        index += size;

    return index;
}

void MultibandProcessor::prepare(const double sampleRate, const int maxBlockSize, const int numChannels)
{
    crossover.prepare(sampleRate);

    alignmentBufferSize = std::max(1, DspCore::getMaximumLatencySamples(sampleRate) + 1);

    for (auto& buffer : alignmentLeft)
        buffer.assign(static_cast<size_t>(alignmentBufferSize), 0.0);

    for (auto& buffer : alignmentRight)
        buffer.assign(static_cast<size_t>(alignmentBufferSize), 0.0);

    for (auto& bandProcessor : bandProcessors)
        bandProcessor.prepare(sampleRate, maxBlockSize, numChannels);

    crossover.setActiveSplitCount(activeSplitCount);
    crossover.setSplitFrequencies(crossoverFrequencies);
    setBandParameters(parameters);
    setFullbandParameters(fullbandParameters);
    reset();
}

void MultibandProcessor::reset()
{
    crossover.reset();

    for (auto& bandProcessor : bandProcessors)
        bandProcessor.reset();

    snapFullbandParameters();
    clearAlignmentBuffers();
}

void MultibandProcessor::setBandParameters(const BandParameters& newParameters)
{
    parameters = newParameters;

    for (size_t bandIndex = 0; bandIndex < bandProcessors.size(); ++bandIndex)
    {
        bandProcessors[bandIndex].setParameters(parameters[bandIndex]);
        bandLatencies[bandIndex] = bandProcessors[bandIndex].getLatencySamples();
    }

    updateLatencyCompensation();
}

void MultibandProcessor::setActiveSplitCount(const size_t newActiveSplitCount)
{
    const auto constrainedSplitCount = std::min(newActiveSplitCount, numSplits);

    if (activeSplitCount == constrainedSplitCount)
        return;

    activeSplitCount = constrainedSplitCount;
    crossover.setActiveSplitCount(activeSplitCount);
    updateLatencyCompensation();
    clearAlignmentBuffers();
}

void MultibandProcessor::setCrossoverFrequencies(const CrossoverFrequencies& newFrequencies)
{
    if (crossoverFrequencies == newFrequencies)
        return;

    crossoverFrequencies = newFrequencies;
    crossover.setSplitFrequencies(crossoverFrequencies);
    clearAlignmentBuffers();
}

void MultibandProcessor::setFullbandParameters(const FullbandParameters& newParameters)
{
    fullbandParameters = newParameters;
    fullbandInGain.setTarget(dbToAmp(roundToJsfxStep(fullbandParameters.inGn + fullbandParameters.autoInGn)));
    fullbandOutGain.setTarget(dbToAmp(roundToJsfxStep(fullbandParameters.outGn)));
    fullbandRightGain.setTarget(dbToAmp(roundToJsfxStep(fullbandParameters.autoInRight)));
    fullbandLeftGain.setTarget(dbToAmp(roundToJsfxStep(fullbandParameters.autoInLeft)));
}

void MultibandProcessor::setSoloMask(const SoloMask& newSoloMask)
{
    soloMask = newSoloMask;
    anySoloActive = std::any_of(soloMask.begin(), soloMask.end(), [] (const bool isSoloed) { return isSoloed; });
}

void MultibandProcessor::process(juce::AudioBuffer<float>& buffer)
{
    if (buffer.getNumSamples() == 0 || buffer.getNumChannels() == 0)
        return;

    auto* leftChannel = buffer.getWritePointer(0);
    auto* rightChannel = buffer.getNumChannels() > 1 ? buffer.getWritePointer(1) : nullptr;

    for (auto& bandProcessor : bandProcessors)
        bandProcessor.beginBlock(buffer.getNumSamples());

    fullbandInGain.beginBlock(buffer.getNumSamples());
    fullbandOutGain.beginBlock(buffer.getNumSamples());
    fullbandRightGain.beginBlock(buffer.getNumSamples());
    fullbandLeftGain.beginBlock(buffer.getNumSamples());

    for (int sampleIndex = 0; sampleIndex < buffer.getNumSamples(); ++sampleIndex)
    {
        const auto leftInput = static_cast<double>(leftChannel[sampleIndex]);
        const auto rightInput = rightChannel != nullptr ? static_cast<double>(rightChannel[sampleIndex]) : leftInput;
        const auto fullbandGain = fullbandInGain.current;
        const auto fullbandOut = fullbandOutGain.current;
        const auto fullbandRight = fullbandRightGain.current;
        const auto fullbandLeft = fullbandLeftGain.current;
        const auto preLeft = leftInput * fullbandGain * fullbandLeft;
        const auto preRight = rightInput * fullbandGain * fullbandRight;
        const auto bands = crossover.processSample(preLeft, preRight);

        auto sumLeft = 0.0;
        auto sumRight = 0.0;

        const auto activeBandCount = activeSplitCount + 1;

        for (size_t bandIndex = 0; bandIndex < activeBandCount; ++bandIndex)
        {
            const auto bandOutput = bandProcessors[bandIndex].processSample(bands[bandIndex].left, bands[bandIndex].right);
            alignmentLeft[bandIndex][static_cast<size_t>(alignmentWritePosition)] = bandOutput.left;
            alignmentRight[bandIndex][static_cast<size_t>(alignmentWritePosition)] = bandOutput.right;

            const auto compensationSamples = targetLatencySamples - bandLatencies[bandIndex];
            const auto readPosition = wrapIndex(alignmentWritePosition - compensationSamples, alignmentBufferSize);
            const auto includeBand = ! anySoloActive || soloMask[bandIndex];

            if (includeBand)
            {
                sumLeft += alignmentLeft[bandIndex][static_cast<size_t>(readPosition)];
                sumRight += alignmentRight[bandIndex][static_cast<size_t>(readPosition)];
            }
        }

        leftChannel[sampleIndex] = static_cast<float>(sumLeft * fullbandOut);

        if (rightChannel != nullptr)
            rightChannel[sampleIndex] = static_cast<float>(sumRight * fullbandOut);

        fullbandInGain.advance();
        fullbandOutGain.advance();
        fullbandRightGain.advance();
        fullbandLeftGain.advance();
        alignmentWritePosition = wrapIndex(alignmentWritePosition + 1, alignmentBufferSize);
    }
}

int MultibandProcessor::getLatencySamples() const noexcept
{
    return targetLatencySamples;
}

void MultibandProcessor::clearAlignmentBuffers()
{
    for (auto& buffer : alignmentLeft)
        std::fill(buffer.begin(), buffer.end(), 0.0);

    for (auto& buffer : alignmentRight)
        std::fill(buffer.begin(), buffer.end(), 0.0);

    alignmentWritePosition = 0;
}

void MultibandProcessor::snapFullbandParameters() noexcept
{
    fullbandInGain.snapTo(fullbandInGain.target);
    fullbandOutGain.snapTo(fullbandOutGain.target);
    fullbandRightGain.snapTo(fullbandRightGain.target);
    fullbandLeftGain.snapTo(fullbandLeftGain.target);
}

void MultibandProcessor::updateLatencyCompensation()
{
    const auto activeBandCount = activeSplitCount + 1;
    targetLatencySamples = *std::max_element(bandLatencies.begin(), bandLatencies.begin() + static_cast<std::ptrdiff_t>(activeBandCount));
    jassert(targetLatencySamples < alignmentBufferSize);
}
} // namespace mxe::dsp
