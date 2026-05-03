#pragma once

#include <JuceHeader.h>

#include <vector>

namespace mxe::dsp
{
class DspCore
{
public:
    struct StereoSample
    {
        double left = 0.0;
        double right = 0.0;
    };

    struct Parameters
    {
        float inGn = 0.0f;
        float inRight = 0.0f;
        float inLeft = 0.0f;
        float autoInGn = 0.0f;
        float autoInRight = 0.0f;
        float autoInLeft = 0.0f;
        float wide = 0.0f;
        float thLU = 0.0f;
        float mkLU = 0.0f;
        float thLD = 0.0f;
        float mkLD = 0.0f;
        float thRU = 0.0f;
        float mkRU = 0.0f;
        float thRD = 0.0f;
        float mkRD = 0.0f;
        bool hwBypass = true;
        float LLThResh = 0.0f;
        float LLTension = 0.0f;
        float LLRelease = 0.0f;
        float LLmk = 0.0f;
        float RRThResh = 0.0f;
        float RRTension = 0.0f;
        float RRRelease = 0.0f;
        float RRmk = 0.0f;
        bool DMbypass = true;
        float FFThResh = 0.0f;
        float FFTension = 0.0f;
        float FFRelease = 0.0f;
        float FFmk = 0.0f;
        bool FFbypass = true;
        float moRph = 0.0f;
        float peakHoldHz = 100.0f;
        float TensionFlooR = -96.0f;
        float TensionHysT = 0.0f;
        bool delTa = false;
    };

    void prepare(double sampleRate, int maxBlockSize, int numChannels);
    void reset();
    void setParameters(const Parameters& newParameters);
    void beginBlock(int numSamples);
    void process(juce::AudioBuffer<float>& buffer);
    StereoSample processSample(double leftInput, double rightInput);
    static int getMaximumLatencySamples(double sampleRate) noexcept;
    int getLatencySamples() const noexcept;

private:
    struct DerivedParameters
    {
        double inGain = 1.0;
        double inRightGain = 1.0;
        double inLeftGain = 1.0;
        double autoInGainTarget = 1.0;
        double autoInRightTarget = 1.0;
        double autoInLeftTarget = 1.0;
        double wideTarget = 0.0;
        double thLU = 1.0;
        double mkLU = 1.0;
        double thLD = 1.0;
        double mkLD = 1.0;
        double thRU = 1.0;
        double mkRU = 1.0;
        double thRD = 1.0;
        double mkRD = 1.0;
        bool hwBypass = true;
        double llThresh = 1.0;
        double llTension = 0.0;
        double llReleaseCoeff = 0.0;
        double llMakeup = 1.0;
        double rrThresh = 1.0;
        double rrTension = 0.0;
        double rrReleaseCoeff = 0.0;
        double rrMakeup = 1.0;
        bool dmBypass = true;
        double ffThresh = 1.0;
        double ffTension = 0.0;
        double ffReleaseCoeff = 0.0;
        double ffMakeup = 1.0;
        bool ffBypass = true;
        double morph = 0.0;
        double clipKneeDb = 0.0;
        double tensionFloor = 0.0;
        double tensionHysteresis = 0.0;
        bool delta = false;
        int holdSamples = 0;
        int lookaheadSamples = 0;
        int totalLookaheadSamples = 0;
        int bufferSize = 1;
        int dryBufferSize = 1;
        int latencySamples = 0;
    };

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
    static double safeAbs(double value);
    static double clamp1(double value);
    static double satShape(double value, double kneeDb);
    static double tensionTarget(double env, double threshold, double floorThreshold, double floorHysteresis, double tension);
    static int wrapIndex(int index, int size);

    void resizeLookaheadBuffers();
    void clearState();
    void updateDerivedParameters();
    void snapSmoothedParameters();
    void advanceSmoothedParameters() noexcept;

    Parameters parameters;
    DerivedParameters derived;

    double currentSampleRate = 44100.0;
    int currentMaxBlockSize = 0;
    int currentNumChannels = 0;
    int maxBuf = 1;
    int maxTotalBuf = 2;

    int holdLL = 0;
    int holdRR = 0;
    int holdFF = 0;
    int holdBaseLL = 0;
    int holdBaseRR = 0;
    int holdBaseFF = 0;
    double envLL = 0.0;
    double envRR = 0.0;
    double envFF = 0.0;
    double envBaseLL = 0.0;
    double envBaseRR = 0.0;
    double envBaseFF = 0.0;
    double baseGainStateLL = 1.0;
    double baseGainStateRR = 1.0;
    double baseGainStateFF = 1.0;
    double gainReductionStateLL = 1.0;
    double gainReductionStateRR = 1.0;
    double gainReductionStateFF = 1.0;
    int bufPos = 0;
    int bufPosDry = 0;

    SmoothedValue autoInGain;
    SmoothedValue autoInRightGain;
    SmoothedValue autoInLeftGain;
    SmoothedValue wideAmount;

    std::vector<double> dmBaseL;
    std::vector<double> dmBaseR;
    std::vector<double> dmDryL;
    std::vector<double> dmDryR;
    std::vector<double> dmGainL;
    std::vector<double> dmGainR;
    std::vector<double> ffBaseL;
    std::vector<double> ffBaseR;
    std::vector<double> ffDryL;
    std::vector<double> ffDryR;
    std::vector<double> ffBaseGain;
    std::vector<double> ffGain;
    std::vector<double> dryL;
    std::vector<double> dryR;
};
} // namespace mxe::dsp
