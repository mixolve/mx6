#pragma once

#include "DspCore.h"
#include "MultibandCrossover.h"

#include <array>
#include <vector>

namespace mxe::dsp
{
class MultibandProcessor
{
public:
    static constexpr size_t numBands = MultibandCrossover::numBands;
    static constexpr size_t numSplits = MultibandCrossover::numSplits;
    using Parameters = DspCore::Parameters;
    using BandParameters = std::array<Parameters, numBands>;
    using CrossoverFrequencies = MultibandCrossover::SplitFrequencies;
    using SoloMask = std::array<bool, numBands>;
    struct FullbandParameters
    {
        float inGn = 0.0f;
        float outGn = 0.0f;
        float autoInGn = 0.0f;
        float autoInRight = 0.0f;
        float autoInLeft = 0.0f;
    };

    void prepare(double sampleRate, int maxBlockSize, int numChannels);
    void reset();
    void setBandParameters(const BandParameters& newParameters);
    void setActiveSplitCount(size_t newActiveSplitCount);
    void setCrossoverFrequencies(const CrossoverFrequencies& newFrequencies);
    void setFullbandParameters(const FullbandParameters& newParameters);
    void setSoloMask(const SoloMask& newSoloMask);
    void process(juce::AudioBuffer<float>& buffer);
    int getLatencySamples() const noexcept;

private:
    struct SmoothedValue
    {
        double current = 1.0;
        double target = 1.0;
        double step = 0.0;
        int remainingSteps = 0;

        void snapTo(double value) noexcept;
        void setTarget(double value) noexcept;
        void beginBlock(int numSamples) noexcept;
        void advance() noexcept;
    };

    static double roundToJsfxStep(double value);
    static double dbToAmp(double decibels);
    static int wrapIndex(int index, int size);

    void snapFullbandParameters() noexcept;
    void clearAlignmentBuffers();
    void updateLatencyCompensation();

    BandParameters parameters {};
    CrossoverFrequencies crossoverFrequencies { 134.0, 523.0, 2093.0, 5000.0, 10000.0 };
    size_t activeSplitCount = numSplits;
    FullbandParameters fullbandParameters {};
    MultibandCrossover crossover;
    std::array<DspCore, numBands> bandProcessors;
    std::array<int, numBands> bandLatencies {};
    SoloMask soloMask {};
    bool anySoloActive = false;
    SmoothedValue fullbandInGain;
    SmoothedValue fullbandOutGain;
    SmoothedValue fullbandRightGain;
    SmoothedValue fullbandLeftGain;
    std::array<std::vector<double>, numBands> alignmentLeft;
    std::array<std::vector<double>, numBands> alignmentRight;
    int alignmentBufferSize = 1;
    int alignmentWritePosition = 0;
    int targetLatencySamples = 0;
};
} // namespace mxe::dsp
