#include "DspCore.h"

#include <algorithm>
#include <cmath>

namespace mxe::dsp
{
namespace
{
constexpr double epsilon = 1.0e-9;
constexpr double maxLookaheadMs = 24.0;
constexpr double kneeRangeDb = 24.0;
} // namespace

void DspCore::SmoothedValue::snapTo(const double value) noexcept
{
    current = value;
    target = value;
    step = 0.0;
    remainingSteps = 0;
}

void DspCore::SmoothedValue::setTarget(const double value) noexcept
{
    target = value;
}

void DspCore::SmoothedValue::beginBlock(const int numSamples) noexcept
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

void DspCore::SmoothedValue::advance() noexcept
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

double DspCore::roundToJsfxStep(const double value)
{
    return std::floor((value * 10.0) + 0.5) * 0.1;
}

double DspCore::dbToAmp(const double decibels)
{
    return std::pow(10.0, decibels / 20.0);
}

double DspCore::safeAbs(const double value)
{
    return std::abs(value);
}

double DspCore::clamp1(const double value)
{
    return juce::jlimit(-1.0, 1.0, value);
}

double DspCore::satShape(const double value, const double kneeDb)
{
    if (kneeDb <= epsilon)
        return clamp1(value);

    const auto absoluteValue = safeAbs(value);
    const auto sign = value < 0.0 ? -1.0 : 1.0;

    auto knee = 1.0 - (kneeDb / 24.0) * (2.0 / 3.0);
    knee = juce::jlimit(1.0 / 3.0, 1.0, knee);

    if (absoluteValue <= knee)
        return value;

    if (absoluteValue >= 2.0 - knee)
        return sign;

    const auto delta = absoluteValue - knee;
    return sign * (absoluteValue - (delta * delta) / (4.0 * (1.0 - knee)));
}

double DspCore::tensionTarget(const double env,
                              const double threshold,
                              const double floorThreshold,
                              const double floorHysteresis,
                              const double tension)
{
    if (safeAbs(tension) <= epsilon || threshold <= epsilon)
        return env;

    if (env >= threshold)
        return env;

    const auto amount = safeAbs(tension) * 0.01;
    const auto shape = 1.0 + 42.0 * amount * amount;
    const auto fullRatio = std::min(1.0, env / threshold);

    double shapedFull = env;

    if (tension < 0.0)
        shapedFull = threshold * (1.0 - std::pow(1.0 - fullRatio, shape));
    else if (tension > 0.0)
        shapedFull = threshold * std::pow(fullRatio, shape);

    const auto clampedFloor = juce::jlimit(0.0, threshold, floorThreshold);

    double shapedGate = threshold;

    if (env <= clampedFloor)
    {
        shapedGate = 0.0;
    }
    else if (env <= threshold)
    {
        const auto span = threshold - clampedFloor;

        if (span <= epsilon)
        {
            shapedGate = threshold;
        }
        else
        {
            const auto gateRatio = std::min(1.0, (env - clampedFloor) / span);

            if (tension < 0.0)
                shapedGate = clampedFloor + span * (1.0 - std::pow(1.0 - gateRatio, shape));
            else if (tension > 0.0)
                shapedGate = clampedFloor + span * std::pow(gateRatio, shape);
            else
                shapedGate = env;
        }
    }

    return shapedGate + (shapedFull - shapedGate) * floorHysteresis;
}

int DspCore::wrapIndex(int index, const int size)
{
    jassert(size > 0);

    index %= size;

    if (index < 0)
        index += size;

    return index;
}

void DspCore::prepare(const double sampleRate, const int maxBlockSize, const int numChannels)
{
    currentSampleRate = sampleRate;
    currentMaxBlockSize = maxBlockSize;
    currentNumChannels = numChannels;

    resizeLookaheadBuffers();
    clearState();
    updateDerivedParameters();
    snapSmoothedParameters();
}

void DspCore::reset()
{
    clearState();
    updateDerivedParameters();
    snapSmoothedParameters();
}

void DspCore::setParameters(const Parameters& newParameters)
{
    parameters = newParameters;
    updateDerivedParameters();
}

void DspCore::beginBlock(const int numSamples)
{
    autoInGain.beginBlock(numSamples);
    autoInRightGain.beginBlock(numSamples);
    autoInLeftGain.beginBlock(numSamples);
    wideAmount.beginBlock(numSamples);
}

void DspCore::process(juce::AudioBuffer<float>& buffer)
{
    if (buffer.getNumSamples() == 0 || buffer.getNumChannels() == 0)
        return;

    beginBlock(buffer.getNumSamples());

    auto* leftChannel = buffer.getWritePointer(0);
    auto* rightChannel = buffer.getNumChannels() > 1 ? buffer.getWritePointer(1) : nullptr;

    for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
    {
        const auto leftInput = static_cast<double>(leftChannel[sample]);
        const auto rightInput = rightChannel != nullptr ? static_cast<double>(rightChannel[sample]) : leftInput;

        const auto output = processSample(leftInput, rightInput);

        leftChannel[sample] = static_cast<float>(output.left);

        if (rightChannel != nullptr)
            rightChannel[sample] = static_cast<float>(output.right);
    }
}

int DspCore::getLatencySamples() const noexcept
{
    return derived.latencySamples;
}

int DspCore::getMaximumLatencySamples(const double sampleRate) noexcept
{
    const auto safeSampleRate = std::max(1.0, sampleRate);
    const auto maxBufferSize = std::max(1, static_cast<int>(std::ceil(safeSampleRate * maxLookaheadMs * 0.001)) + 2);
    const auto maxTotalBufferSize = std::max(2, maxBufferSize * 2);
    return std::max(0, maxTotalBufferSize - 2);
}

DspCore::StereoSample DspCore::processSample(const double leftInput, const double rightInput)
{
    const auto inputGain = derived.inGain * autoInGain.current;
    const auto inputRightGain = derived.inRightGain * autoInRightGain.current;
    const auto inputLeftGain = derived.inLeftGain * autoInLeftGain.current;
    const auto widthGain = 1.0 + wideAmount.current;

    const auto gainedL = leftInput * inputGain * inputLeftGain;
    const auto gainedR = rightInput * inputGain * inputRightGain;
    const auto mid = 0.5 * (gainedL + gainedR);
    const auto side = 0.5 * (gainedL - gainedR) * widthGain;
    const auto inL = mid + side;
    const auto inR = mid - side;

    dryL[static_cast<size_t>(bufPosDry)] = inL;
    dryR[static_cast<size_t>(bufPosDry)] = inR;

    const auto posL = std::max(inL, 0.0);
    const auto negL = std::min(inL, 0.0);
    const auto posR = std::max(inR, 0.0);
    const auto negR = std::min(inR, 0.0);

    const auto gainLU = posL > derived.thLU ? (derived.thLU / posL) : 1.0;
    const auto gainLD = -negL > derived.thLD ? (derived.thLD / -negL) : 1.0;
    const auto gainRU = posR > derived.thRU ? (derived.thRU / posR) : 1.0;
    const auto gainRD = -negR > derived.thRD ? (derived.thRD / -negR) : 1.0;

    auto halfL = posL * gainLU * derived.mkLU + negL * gainLD * derived.mkLD;
    auto halfR = posR * gainRU * derived.mkRU + negR * gainRD * derived.mkRD;

    if (derived.hwBypass)
    {
        halfL = inL;
        halfR = inR;
    }

    const auto ctrlBaseLL = safeAbs(halfL);
    if (ctrlBaseLL > envBaseLL)
    {
        envBaseLL = ctrlBaseLL;
        holdBaseLL = derived.holdSamples;
    }
    else if (holdBaseLL > 0)
    {
        --holdBaseLL;
    }
    else
    {
        envBaseLL = ctrlBaseLL + (envBaseLL - ctrlBaseLL) * derived.llReleaseCoeff;
    }

    const auto targetBaseLL = tensionTarget(envBaseLL,
                                            derived.llThresh,
                                            derived.tensionFloor,
                                            derived.tensionHysteresis,
                                            derived.llTension);
    const auto baseGainTargetLL = envBaseLL > epsilon ? (targetBaseLL / envBaseLL) : 1.0;
    baseGainStateLL = baseGainTargetLL;

    const auto baseNowLL = halfL * baseGainStateLL;

    const auto ctrlBaseRR = safeAbs(halfR);
    if (ctrlBaseRR > envBaseRR)
    {
        envBaseRR = ctrlBaseRR;
        holdBaseRR = derived.holdSamples;
    }
    else if (holdBaseRR > 0)
    {
        --holdBaseRR;
    }
    else
    {
        envBaseRR = ctrlBaseRR + (envBaseRR - ctrlBaseRR) * derived.rrReleaseCoeff;
    }

    const auto targetBaseRR = tensionTarget(envBaseRR,
                                            derived.rrThresh,
                                            derived.tensionFloor,
                                            derived.tensionHysteresis,
                                            derived.rrTension);
    const auto baseGainTargetRR = envBaseRR > epsilon ? (targetBaseRR / envBaseRR) : 1.0;
    baseGainStateRR = baseGainTargetRR;

    const auto baseNowRR = halfR * baseGainStateRR;

    dmBaseL[static_cast<size_t>(bufPos)] = baseNowLL;
    dmBaseR[static_cast<size_t>(bufPos)] = baseNowRR;
    dmDryL[static_cast<size_t>(bufPos)] = halfL;
    dmDryR[static_cast<size_t>(bufPos)] = halfR;

    const auto peakNowLL = safeAbs(baseNowLL);
    const auto peakNowRR = safeAbs(baseNowRR);

    if (peakNowLL > envLL)
    {
        envLL = peakNowLL;
        holdLL = derived.holdSamples;
    }
    else if (holdLL > 0)
    {
        --holdLL;
    }
    else
    {
        envLL = peakNowLL;
    }

    if (peakNowRR > envRR)
    {
        envRR = peakNowRR;
        holdRR = derived.holdSamples;
    }
    else if (holdRR > 0)
    {
        --holdRR;
    }
    else
    {
        envRR = peakNowRR;
    }

    const auto gainRedTargetLL = envLL > derived.llThresh ? (derived.llThresh / envLL) : 1.0;
    const auto gainRedTargetRR = envRR > derived.rrThresh ? (derived.rrThresh / envRR) : 1.0;

    if (gainRedTargetLL < gainReductionStateLL)
        gainReductionStateLL = gainRedTargetLL;
    else
        gainReductionStateLL = gainRedTargetLL + (gainReductionStateLL - gainRedTargetLL) * derived.llReleaseCoeff;

    if (gainRedTargetRR < gainReductionStateRR)
        gainReductionStateRR = gainRedTargetRR;
    else
        gainReductionStateRR = gainRedTargetRR + (gainReductionStateRR - gainRedTargetRR) * derived.rrReleaseCoeff;

    dmGainL[static_cast<size_t>(bufPos)] = gainReductionStateLL;
    dmGainR[static_cast<size_t>(bufPos)] = gainReductionStateRR;

    const auto readPos = wrapIndex(bufPos - derived.lookaheadSamples, derived.bufferSize);

    const auto delayBaseLL = dmBaseL[static_cast<size_t>(readPos)];
    const auto delayBaseRR = dmBaseR[static_cast<size_t>(readPos)];
    const auto delayedDmDryL = dmDryL[static_cast<size_t>(readPos)];
    const auto delayedDmDryR = dmDryR[static_cast<size_t>(readPos)];
    const auto delayedGainRedLL = dmGainL[static_cast<size_t>(readPos)];
    const auto delayedGainRedRR = dmGainR[static_cast<size_t>(readPos)];

    const auto reducedDmBaseL = delayBaseLL * delayedGainRedLL;
    const auto reducedDmBaseR = delayBaseRR * delayedGainRedRR;

    const auto dmClipL = derived.llThresh > epsilon
        ? derived.llThresh * satShape(delayBaseLL / derived.llThresh, derived.clipKneeDb)
        : satShape(delayBaseLL, derived.clipKneeDb);
    const auto dmClipR = derived.rrThresh > epsilon
        ? derived.rrThresh * satShape(delayBaseRR / derived.rrThresh, derived.clipKneeDb)
        : satShape(delayBaseRR, derived.clipKneeDb);

    const auto dmLimL = reducedDmBaseL;
    const auto dmLimR = reducedDmBaseR;
    const auto dmWetL = (dmClipL + (dmLimL - dmClipL) * derived.morph) * derived.llMakeup;
    const auto dmWetR = (dmClipR + (dmLimR - dmClipR) * derived.morph) * derived.rrMakeup;
    const auto dmOutL = derived.dmBypass ? delayedDmDryL : dmWetL;
    const auto dmOutR = derived.dmBypass ? delayedDmDryR : dmWetR;

    const auto ctrlBaseFF = std::max(safeAbs(dmOutL), safeAbs(dmOutR));
    if (ctrlBaseFF > envBaseFF)
    {
        envBaseFF = ctrlBaseFF;
        holdBaseFF = derived.holdSamples;
    }
    else if (holdBaseFF > 0)
    {
        --holdBaseFF;
    }
    else
    {
        envBaseFF = ctrlBaseFF + (envBaseFF - ctrlBaseFF) * derived.ffReleaseCoeff;
    }

    const auto targetBaseFF = tensionTarget(envBaseFF,
                                            derived.ffThresh,
                                            derived.tensionFloor,
                                            derived.tensionHysteresis,
                                            derived.ffTension);
    const auto baseGainTargetFF = envBaseFF > epsilon ? (targetBaseFF / envBaseFF) : 1.0;
    baseGainStateFF = baseGainTargetFF;

    const auto baseNowFFL = dmOutL * baseGainStateFF;
    const auto baseNowFFR = dmOutR * baseGainStateFF;

    ffBaseL[static_cast<size_t>(bufPos)] = dmOutL;
    ffBaseR[static_cast<size_t>(bufPos)] = dmOutR;
    ffDryL[static_cast<size_t>(bufPos)] = dmOutL;
    ffDryR[static_cast<size_t>(bufPos)] = dmOutR;
    ffBaseGain[static_cast<size_t>(bufPos)] = baseGainStateFF;

    const auto peakNowFF = std::max(safeAbs(baseNowFFL), safeAbs(baseNowFFR));

    if (peakNowFF > envFF)
    {
        envFF = peakNowFF;
        holdFF = derived.holdSamples;
    }
    else if (holdFF > 0)
    {
        --holdFF;
    }
    else
    {
        envFF = peakNowFF;
    }

    const auto gainRedTargetFF = envFF > derived.ffThresh ? (derived.ffThresh / envFF) : 1.0;

    if (gainRedTargetFF < gainReductionStateFF)
        gainReductionStateFF = gainRedTargetFF;
    else
        gainReductionStateFF = gainRedTargetFF + (gainReductionStateFF - gainRedTargetFF) * derived.ffReleaseCoeff;

    ffGain[static_cast<size_t>(bufPos)] = gainReductionStateFF;

    const auto delayBaseFFL = ffBaseL[static_cast<size_t>(readPos)];
    const auto delayBaseFFR = ffBaseR[static_cast<size_t>(readPos)];
    const auto delayedFfDryL = ffDryL[static_cast<size_t>(readPos)];
    const auto delayedFfDryR = ffDryR[static_cast<size_t>(readPos)];
    const auto delayedBaseGainFF = ffBaseGain[static_cast<size_t>(readPos)];
    const auto delayedGainRedFF = ffGain[static_cast<size_t>(readPos)];

    const auto baseDelayedFFL = delayBaseFFL * delayedBaseGainFF;
    const auto baseDelayedFFR = delayBaseFFR * delayedBaseGainFF;
    const auto reducedFfBaseL = baseDelayedFFL * delayedGainRedFF;
    const auto reducedFfBaseR = baseDelayedFFR * delayedGainRedFF;
    const auto ffClipL = derived.ffThresh > epsilon
        ? derived.ffThresh * satShape(baseDelayedFFL / derived.ffThresh, derived.clipKneeDb)
        : satShape(baseDelayedFFL, derived.clipKneeDb);
    const auto ffClipR = derived.ffThresh > epsilon
        ? derived.ffThresh * satShape(baseDelayedFFR / derived.ffThresh, derived.clipKneeDb)
        : satShape(baseDelayedFFR, derived.clipKneeDb);
    const auto ffLimL = reducedFfBaseL;
    const auto ffLimR = reducedFfBaseR;
    const auto ffWetL = (ffClipL + (ffLimL - ffClipL) * derived.morph) * derived.ffMakeup;
    const auto ffWetR = (ffClipR + (ffLimR - ffClipR) * derived.morph) * derived.ffMakeup;
    const auto outL = derived.ffBypass ? delayedFfDryL : ffWetL;
    const auto outR = derived.ffBypass ? delayedFfDryR : ffWetR;

    const auto readPosDry = wrapIndex(bufPosDry - derived.totalLookaheadSamples, derived.dryBufferSize);
    const auto delayedDryL = dryL[static_cast<size_t>(readPosDry)];
    const auto delayedDryR = dryR[static_cast<size_t>(readPosDry)];

    StereoSample output;
    output.left = derived.delta ? (delayedDryL - outL) : outL;
    output.right = derived.delta ? (delayedDryR - outR) : outR;

    bufPos = wrapIndex(bufPos + 1, derived.bufferSize);
    bufPosDry = wrapIndex(bufPosDry + 1, derived.dryBufferSize);
    advanceSmoothedParameters();

    return output;
}

void DspCore::resizeLookaheadBuffers()
{
    maxBuf = std::max(1, static_cast<int>(std::ceil(currentSampleRate * maxLookaheadMs * 0.001)) + 2);
    maxTotalBuf = std::max(2, maxBuf * 2);

    dmBaseL.assign(static_cast<size_t>(maxBuf), 0.0);
    dmBaseR.assign(static_cast<size_t>(maxBuf), 0.0);
    dmDryL.assign(static_cast<size_t>(maxBuf), 0.0);
    dmDryR.assign(static_cast<size_t>(maxBuf), 0.0);
    dmGainL.assign(static_cast<size_t>(maxBuf), 1.0);
    dmGainR.assign(static_cast<size_t>(maxBuf), 1.0);
    ffBaseL.assign(static_cast<size_t>(maxBuf), 0.0);
    ffBaseR.assign(static_cast<size_t>(maxBuf), 0.0);
    ffDryL.assign(static_cast<size_t>(maxBuf), 0.0);
    ffDryR.assign(static_cast<size_t>(maxBuf), 0.0);
    ffBaseGain.assign(static_cast<size_t>(maxBuf), 1.0);
    ffGain.assign(static_cast<size_t>(maxBuf), 1.0);
    dryL.assign(static_cast<size_t>(maxTotalBuf), 0.0);
    dryR.assign(static_cast<size_t>(maxTotalBuf), 0.0);
}

void DspCore::clearState()
{
    std::fill(dmBaseL.begin(), dmBaseL.end(), 0.0);
    std::fill(dmBaseR.begin(), dmBaseR.end(), 0.0);
    std::fill(dmDryL.begin(), dmDryL.end(), 0.0);
    std::fill(dmDryR.begin(), dmDryR.end(), 0.0);
    std::fill(dmGainL.begin(), dmGainL.end(), 1.0);
    std::fill(dmGainR.begin(), dmGainR.end(), 1.0);
    std::fill(ffBaseL.begin(), ffBaseL.end(), 0.0);
    std::fill(ffBaseR.begin(), ffBaseR.end(), 0.0);
    std::fill(ffDryL.begin(), ffDryL.end(), 0.0);
    std::fill(ffDryR.begin(), ffDryR.end(), 0.0);
    std::fill(ffBaseGain.begin(), ffBaseGain.end(), 1.0);
    std::fill(ffGain.begin(), ffGain.end(), 1.0);
    std::fill(dryL.begin(), dryL.end(), 0.0);
    std::fill(dryR.begin(), dryR.end(), 0.0);

    holdLL = 0;
    holdRR = 0;
    holdFF = 0;
    holdBaseLL = 0;
    holdBaseRR = 0;
    holdBaseFF = 0;
    envLL = 0.0;
    envRR = 0.0;
    envFF = 0.0;
    envBaseLL = 0.0;
    envBaseRR = 0.0;
    envBaseFF = 0.0;
    baseGainStateLL = 1.0;
    baseGainStateRR = 1.0;
    baseGainStateFF = 1.0;
    gainReductionStateLL = 1.0;
    gainReductionStateRR = 1.0;
    gainReductionStateFF = 1.0;
    bufPos = 0;
    bufPosDry = 0;
}

void DspCore::updateDerivedParameters()
{
    const auto sampleRate = std::max(1.0, currentSampleRate);

    derived.inGain = dbToAmp(roundToJsfxStep(parameters.inGn));
    derived.inRightGain = dbToAmp(roundToJsfxStep(parameters.inRight));
    derived.inLeftGain = dbToAmp(roundToJsfxStep(parameters.inLeft));
    derived.autoInGainTarget = dbToAmp(roundToJsfxStep(parameters.autoInGn));
    derived.autoInRightTarget = dbToAmp(roundToJsfxStep(parameters.autoInRight));
    derived.autoInLeftTarget = dbToAmp(roundToJsfxStep(parameters.autoInLeft));
    derived.wideTarget = roundToJsfxStep(parameters.wide) * 0.01;
    derived.thLU = dbToAmp(roundToJsfxStep(parameters.thLU));
    derived.mkLU = dbToAmp(roundToJsfxStep(parameters.mkLU));
    derived.thLD = dbToAmp(roundToJsfxStep(parameters.thLD));
    derived.mkLD = dbToAmp(roundToJsfxStep(parameters.mkLD));
    derived.thRU = dbToAmp(roundToJsfxStep(parameters.thRU));
    derived.mkRU = dbToAmp(roundToJsfxStep(parameters.mkRU));
    derived.thRD = dbToAmp(roundToJsfxStep(parameters.thRD));
    derived.mkRD = dbToAmp(roundToJsfxStep(parameters.mkRD));
    derived.hwBypass = parameters.hwBypass;

    derived.llThresh = dbToAmp(roundToJsfxStep(parameters.LLThResh));
    derived.llTension = roundToJsfxStep(parameters.LLTension);
    const auto llReleaseMs = roundToJsfxStep(parameters.LLRelease);
    derived.llReleaseCoeff = llReleaseMs <= 0.0 ? 0.0 : std::exp(-1.0 / std::max(1.0, llReleaseMs * 0.001 * sampleRate));
    derived.llMakeup = dbToAmp(roundToJsfxStep(parameters.LLmk));

    derived.rrThresh = dbToAmp(roundToJsfxStep(parameters.RRThResh));
    derived.rrTension = roundToJsfxStep(parameters.RRTension);
    const auto rrReleaseMs = roundToJsfxStep(parameters.RRRelease);
    derived.rrReleaseCoeff = rrReleaseMs <= 0.0 ? 0.0 : std::exp(-1.0 / std::max(1.0, rrReleaseMs * 0.001 * sampleRate));
    derived.rrMakeup = dbToAmp(roundToJsfxStep(parameters.RRmk));
    derived.dmBypass = parameters.DMbypass;

    derived.ffThresh = dbToAmp(roundToJsfxStep(parameters.FFThResh));
    derived.ffTension = roundToJsfxStep(parameters.FFTension);
    const auto ffReleaseMs = roundToJsfxStep(parameters.FFRelease);
    derived.ffReleaseCoeff = ffReleaseMs <= 0.0 ? 0.0 : std::exp(-1.0 / std::max(1.0, ffReleaseMs * 0.001 * sampleRate));
    derived.ffMakeup = dbToAmp(roundToJsfxStep(parameters.FFmk));
    derived.ffBypass = parameters.FFbypass;

    derived.morph = roundToJsfxStep(parameters.moRph) * 0.01;
    derived.clipKneeDb = kneeRangeDb * derived.morph;

    auto holdHz = roundToJsfxStep(parameters.peakHoldHz);
    holdHz = std::max(21.0, holdHz);

    derived.tensionFloor = dbToAmp(roundToJsfxStep(parameters.TensionFlooR));
    derived.tensionHysteresis = roundToJsfxStep(parameters.TensionHysT) * 0.01;
    derived.delta = parameters.delTa;

    const auto holdTotalMs = 500.0 / holdHz;
    auto holdSamples = static_cast<int>(std::floor(holdTotalMs * 0.001 * sampleRate));
    holdSamples = std::max(0, holdSamples - 5);

    derived.holdSamples = holdSamples;
    derived.lookaheadSamples = juce::jlimit(0, std::max(0, maxBuf - 2), holdSamples);
    derived.totalLookaheadSamples = juce::jlimit(0, std::max(0, maxTotalBuf - 2), derived.lookaheadSamples * 2);
    derived.bufferSize = derived.lookaheadSamples + 1;
    derived.dryBufferSize = derived.totalLookaheadSamples + 1;
    derived.latencySamples = derived.totalLookaheadSamples;

    bufPos = wrapIndex(bufPos, derived.bufferSize);
    bufPosDry = wrapIndex(bufPosDry, derived.dryBufferSize);

    autoInGain.setTarget(derived.autoInGainTarget);
    autoInRightGain.setTarget(derived.autoInRightTarget);
    autoInLeftGain.setTarget(derived.autoInLeftTarget);
    wideAmount.setTarget(derived.wideTarget);
}

void DspCore::snapSmoothedParameters()
{
    autoInGain.snapTo(derived.autoInGainTarget);
    autoInRightGain.snapTo(derived.autoInRightTarget);
    autoInLeftGain.snapTo(derived.autoInLeftTarget);
    wideAmount.snapTo(derived.wideTarget);
}

void DspCore::advanceSmoothedParameters() noexcept
{
    autoInGain.advance();
    autoInRightGain.advance();
    autoInLeftGain.advance();
    wideAmount.advance();
}
} // namespace mxe::dsp
