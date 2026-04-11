#include "MultibandCrossover.h"

#include <algorithm>
#include <cmath>

namespace mxe::dsp
{
namespace
{
constexpr double qA = 0.541196100146197;
constexpr double qB = 1.306562964876377;
constexpr double minFrequency = 10.0;
constexpr double minSplitGapHz = 1.0;
constexpr double nyquistMargin = 0.499;
constexpr double pi = 3.14159265358979323846;
} // namespace

double MultibandCrossover::clampFrequency(const double frequency, const double sampleRate)
{
    return std::clamp(frequency, minFrequency, nyquistMargin * sampleRate);
}

MultibandCrossover::BiquadCoefficients MultibandCrossover::makeLowpass(const double sampleRate,
                                                                       const double frequency,
                                                                       const double q)
{
    const auto clampedFrequency = clampFrequency(frequency, sampleRate);
    const auto w0 = 2.0 * pi * clampedFrequency / sampleRate;
    const auto cs = std::cos(w0);
    const auto sn = std::sin(w0);
    const auto alpha = sn / (2.0 * q);

    const auto b0 = (1.0 - cs) * 0.5;
    const auto b1 = 1.0 - cs;
    const auto b2 = (1.0 - cs) * 0.5;
    const auto a0 = 1.0 + alpha;
    const auto a1 = -2.0 * cs;
    const auto a2 = 1.0 - alpha;

    return { b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0 };
}

MultibandCrossover::BiquadCoefficients MultibandCrossover::makeHighpass(const double sampleRate,
                                                                        const double frequency,
                                                                        const double q)
{
    const auto clampedFrequency = clampFrequency(frequency, sampleRate);
    const auto w0 = 2.0 * pi * clampedFrequency / sampleRate;
    const auto cs = std::cos(w0);
    const auto sn = std::sin(w0);
    const auto alpha = sn / (2.0 * q);

    const auto b0 = (1.0 + cs) * 0.5;
    const auto b1 = -(1.0 + cs);
    const auto b2 = (1.0 + cs) * 0.5;
    const auto a0 = 1.0 + alpha;
    const auto a1 = -2.0 * cs;
    const auto a2 = 1.0 - alpha;

    return { b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0 };
}

size_t MultibandCrossover::compIndex(const size_t splitIndex, const size_t bandIndex)
{
    return (splitIndex * (splitIndex - 1)) / 2 + bandIndex;
}

double MultibandCrossover::processCascade(double input, const Cascade& coefficients, CascadeState& state)
{
    auto output = input;

    for (size_t stageIndex = 0; stageIndex < coefficients.size(); ++stageIndex)
    {
        const auto& stageCoefficients = coefficients[stageIndex];
        auto& stageState = state[stageIndex];

        const auto t = stageCoefficients.b0 * output + stageState.s1;
        stageState.s1 = stageCoefficients.b1 * output - stageCoefficients.a1 * t + stageState.s2;
        stageState.s2 = stageCoefficients.b2 * output - stageCoefficients.a2 * t;
        output = t;
    }

    return output;
}

MultibandCrossover::SplitFrequencies MultibandCrossover::constrainSplitFrequencies(const SplitFrequencies& sourceFrequencies,
                                                                                   const double sampleRate)
{
    SplitFrequencies constrained {};
    const auto maxFrequency = nyquistMargin * sampleRate;

    for (size_t splitIndex = 0; splitIndex < constrained.size(); ++splitIndex)
    {
        const auto lowerBound = splitIndex == 0 ? minFrequency
                                                : constrained[splitIndex - 1] + minSplitGapHz;
        const auto remainingSplits = static_cast<double>(constrained.size() - splitIndex - 1);
        const auto upperBound = std::max(lowerBound, maxFrequency - (remainingSplits * minSplitGapHz));
        constrained[splitIndex] = std::clamp(sourceFrequencies[splitIndex], lowerBound, upperBound);
    }

    return constrained;
}

void MultibandCrossover::prepare(const double sampleRate)
{
    currentSampleRate = sampleRate;

    for (size_t splitIndex = 0; splitIndex < numSplits; ++splitIndex)
        updateSplit(splitIndex, frequencies[splitIndex]);

    reset();
}

void MultibandCrossover::reset()
{
    mainLpLeft = {};
    mainLpRight = {};
    mainHpLeft = {};
    mainHpRight = {};
    compLpLeft = {};
    compLpRight = {};
    compHpLeft = {};
    compHpRight = {};
}

void MultibandCrossover::setSplitFrequencies(const SplitFrequencies& newFrequencies)
{
    frequencies = constrainSplitFrequencies(newFrequencies, currentSampleRate);

    for (size_t splitIndex = 0; splitIndex < numSplits; ++splitIndex)
        updateSplit(splitIndex, frequencies[splitIndex]);

    reset();
}

void MultibandCrossover::setActiveSplitCount(const size_t newActiveSplitCount)
{
    activeSplitCount = std::min(newActiveSplitCount, numSplits);
    reset();
}

void MultibandCrossover::updateSplit(const size_t splitIndex, const double frequency)
{
    lowpassCoefficients[splitIndex] = {
        makeLowpass(currentSampleRate, frequency, qA),
        makeLowpass(currentSampleRate, frequency, qB),
        makeLowpass(currentSampleRate, frequency, qA),
        makeLowpass(currentSampleRate, frequency, qB),
    };

    highpassCoefficients[splitIndex] = {
        makeHighpass(currentSampleRate, frequency, qA),
        makeHighpass(currentSampleRate, frequency, qB),
        makeHighpass(currentSampleRate, frequency, qA),
        makeHighpass(currentSampleRate, frequency, qB),
    };
}

MultibandCrossover::BandArray MultibandCrossover::processSample(const double leftInput, const double rightInput)
{
    BandArray bands {};

    auto remainderLeft = leftInput;
    auto remainderRight = rightInput;

    for (size_t splitIndex = 0; splitIndex < activeSplitCount; ++splitIndex)
    {
        bands[splitIndex].left = processCascade(remainderLeft, lowpassCoefficients[splitIndex], mainLpLeft[splitIndex]);
        bands[splitIndex].right = processCascade(remainderRight, lowpassCoefficients[splitIndex], mainLpRight[splitIndex]);
        remainderLeft = processCascade(remainderLeft, highpassCoefficients[splitIndex], mainHpLeft[splitIndex]);
        remainderRight = processCascade(remainderRight, highpassCoefficients[splitIndex], mainHpRight[splitIndex]);
    }

    bands[activeSplitCount].left = remainderLeft;
    bands[activeSplitCount].right = remainderRight;

    for (size_t bandIndex = 0; bandIndex < activeSplitCount; ++bandIndex)
    {
        auto compensatedLeft = bands[bandIndex].left;
        auto compensatedRight = bands[bandIndex].right;

        for (size_t splitIndex = bandIndex + 1; splitIndex < activeSplitCount; ++splitIndex)
        {
            const auto compensatorIndex = compIndex(splitIndex, bandIndex);
            compensatedLeft = processCascade(compensatedLeft, lowpassCoefficients[splitIndex], compLpLeft[compensatorIndex])
                            + processCascade(compensatedLeft, highpassCoefficients[splitIndex], compHpLeft[compensatorIndex]);
            compensatedRight = processCascade(compensatedRight, lowpassCoefficients[splitIndex], compLpRight[compensatorIndex])
                             + processCascade(compensatedRight, highpassCoefficients[splitIndex], compHpRight[compensatorIndex]);
        }

        bands[bandIndex].left = compensatedLeft;
        bands[bandIndex].right = compensatedRight;
    }

    return bands;
}
} // namespace mxe::dsp
