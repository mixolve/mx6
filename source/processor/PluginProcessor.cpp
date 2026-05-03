#include "PluginProcessor.h"

#include "PluginEditor.h"

#include <cmath>

namespace
{
constexpr size_t numBands = mxe::dsp::MultibandProcessor::numBands;

juce::String formatParameterValue(const float value)
{
    auto rounded = std::round(static_cast<double>(value) * 10.0) / 10.0;

    if (std::abs(rounded) < 0.05)
        rounded = 0.0;

    return juce::String::formatted("%+08.1f", rounded);
}

enum class ParameterType
{
    floating,
    boolean,
};

enum class ParameterSlot : size_t
{
    inGn,
    inRight,
    inLeft,
    autoInGn,
    autoInRight,
    autoInLeft,
    wide,
    thLU,
    mkLU,
    thLD,
    mkLD,
    thRU,
    mkRU,
    thRD,
    mkRD,
    hwBypass,
    LLThResh,
    LLTension,
    LLRelease,
    LLmk,
    RRThResh,
    RRTension,
    RRRelease,
    RRmk,
    DMbypass,
    FFThResh,
    FFTension,
    FFRelease,
    FFmk,
    FFbypass,
    moRph,
    peakHoldHz,
    TensionFlooR,
    TensionHysT,
    delTa,
    count
};

enum class FullbandAutomationSlot : size_t
{
    inGn,
    inRight,
    inLeft,
    count
};

enum class FullbandVisibleSlot : size_t
{
    inGn,
    outGn,
    count
};

enum class CrossoverSlot : size_t
{
    xover1,
    xover2,
    xover3,
    xover4,
    xover5,
    count
};

constexpr size_t numParameterSlots = static_cast<size_t>(ParameterSlot::count);
constexpr size_t numFullbandVisibleSlots = static_cast<size_t>(FullbandVisibleSlot::count);
constexpr size_t numFullbandAutomationSlots = static_cast<size_t>(FullbandAutomationSlot::count);
constexpr size_t numCrossoverSlots = static_cast<size_t>(CrossoverSlot::count);

struct ParameterSpec
{
    const char* suffix = "";
    const char* name = "";
    ParameterType type = ParameterType::floating;
    float min = 0.0f;
    float max = 1.0f;
    float step = 0.01f;
    float defaultValue = 0.0f;
    const char* label = "";
};

constexpr auto parameterSpecs = std::to_array<ParameterSpec>({
    { "inGn", "Input Gain", ParameterType::floating, -24.0f, 24.0f, 0.1f, 0.0f, "dB" },
    { "inRight", "Input Right", ParameterType::floating, -24.0f, 24.0f, 0.1f, 0.0f, "dB" },
    { "inLeft", "Input Left", ParameterType::floating, -24.0f, 24.0f, 0.1f, 0.0f, "dB" },
    { "autoInGn", "AUTO INPUT-GAIN", ParameterType::floating, -24.0f, 24.0f, 0.1f, 0.0f, "dB" },
    { "autoInRight", "AUTO IN-RIGHT", ParameterType::floating, -24.0f, 24.0f, 0.1f, 0.0f, "dB" },
    { "autoInLeft", "AUTO IN-LEFT", ParameterType::floating, -24.0f, 24.0f, 0.1f, 0.0f, "dB" },
    { "wide", "Wide", ParameterType::floating, -100.0f, 400.0f, 0.1f, 0.0f, "%" },
    { "thLU", "L Up Threshold", ParameterType::floating, -24.0f, 0.0f, 0.1f, 0.0f, "dB" },
    { "mkLU", "L Up Out Gain", ParameterType::floating, -24.0f, 24.0f, 0.1f, 0.0f, "dB" },
    { "thLD", "L Down Threshold", ParameterType::floating, -24.0f, 0.0f, 0.1f, 0.0f, "dB" },
    { "mkLD", "L Down Out Gain", ParameterType::floating, -24.0f, 24.0f, 0.1f, 0.0f, "dB" },
    { "thRU", "R Up Threshold", ParameterType::floating, -24.0f, 0.0f, 0.1f, 0.0f, "dB" },
    { "mkRU", "R Up Out Gain", ParameterType::floating, -24.0f, 24.0f, 0.1f, 0.0f, "dB" },
    { "thRD", "R Down Threshold", ParameterType::floating, -24.0f, 0.0f, 0.1f, 0.0f, "dB" },
    { "mkRD", "R Down Out Gain", ParameterType::floating, -24.0f, 24.0f, 0.1f, 0.0f, "dB" },
    { "hwBypass", "HW Bypass", ParameterType::boolean, 0.0f, 1.0f, 1.0f, 1.0f, "" },
    { "LLThResh", "LL Threshold", ParameterType::floating, -24.0f, 0.0f, 0.1f, 0.0f, "dB" },
    { "LLTension", "LL Tension", ParameterType::floating, -100.0f, 100.0f, 0.1f, 0.0f, "%" },
    { "LLRelease", "LL Release", ParameterType::floating, 0.0f, 1000.0f, 0.1f, 0.0f, "ms" },
    { "LLmk", "LL Out Gain", ParameterType::floating, -24.0f, 24.0f, 0.1f, 0.0f, "dB" },
    { "RRThResh", "RR Threshold", ParameterType::floating, -24.0f, 0.0f, 0.1f, 0.0f, "dB" },
    { "RRTension", "RR Tension", ParameterType::floating, -100.0f, 100.0f, 0.1f, 0.0f, "%" },
    { "RRRelease", "RR Release", ParameterType::floating, 0.0f, 1000.0f, 0.1f, 0.0f, "ms" },
    { "RRmk", "RR Out Gain", ParameterType::floating, -24.0f, 24.0f, 0.1f, 0.0f, "dB" },
    { "DMbypass", "DM Bypass", ParameterType::boolean, 0.0f, 1.0f, 1.0f, 1.0f, "" },
    { "FFThResh", "FF Threshold", ParameterType::floating, -24.0f, 0.0f, 0.1f, 0.0f, "dB" },
    { "FFTension", "FF Tension", ParameterType::floating, -100.0f, 100.0f, 0.1f, 0.0f, "%" },
    { "FFRelease", "FF Release", ParameterType::floating, 0.0f, 1000.0f, 0.1f, 0.0f, "ms" },
    { "FFmk", "FF Out Gain", ParameterType::floating, -24.0f, 24.0f, 0.1f, 0.0f, "dB" },
    { "FFbypass", "FF Bypass", ParameterType::boolean, 0.0f, 1.0f, 1.0f, 1.0f, "" },
    { "moRph", "Morph", ParameterType::floating, 0.0f, 100.0f, 0.1f, 0.0f, "%" },
    { "peakHoldHz", "Peak Hold", ParameterType::floating, 21.0f, 3675.1f, 0.1f, 100.0f, "Hz" },
    { "TensionFlooR", "Tension Floor", ParameterType::floating, -96.0f, 0.0f, 0.1f, -96.0f, "dB" },
    { "TensionHysT", "Tension Hysteresis", ParameterType::floating, 0.0f, 100.0f, 0.1f, 0.0f, "%" },
    { "delTa", "Delta", ParameterType::boolean, 0.0f, 1.0f, 1.0f, 0.0f, "" },
});

constexpr auto fullbandAutomationSpecs = std::to_array<ParameterSpec>({
    { "autoInGn", "ENV FULLBAND INPUT-GAIN", ParameterType::floating, -24.0f, 24.0f, 0.1f, 0.0f, "dB" },
    { "autoInRight", "ENV FULLBAND IN-RIGHT", ParameterType::floating, -24.0f, 24.0f, 0.1f, 0.0f, "dB" },
    { "autoInLeft", "ENV FULLBAND IN-LEFT", ParameterType::floating, -24.0f, 24.0f, 0.1f, 0.0f, "dB" },
});

constexpr auto fullbandVisibleSpecs = std::to_array<ParameterSpec>({
    { "inGnVisible", "Fullband In Gain", ParameterType::floating, -24.0f, 24.0f, 0.1f, 0.0f, "dB" },
    { "outGnVisible", "Fullband Out Gain", ParameterType::floating, -24.0f, 24.0f, 0.1f, 0.0f, "dB" },
});

constexpr auto crossoverSpecs = std::to_array<ParameterSpec>({
    { "xover1", "Crossover 1", ParameterType::floating, 20.0f, 20000.0f, 1.0f, 134.0f, "Hz" },
    { "xover2", "Crossover 2", ParameterType::floating, 20.0f, 20000.0f, 1.0f, 523.0f, "Hz" },
    { "xover3", "Crossover 3", ParameterType::floating, 20.0f, 20000.0f, 1.0f, 2093.0f, "Hz" },
    { "xover4", "Crossover 4", ParameterType::floating, 20.0f, 20000.0f, 1.0f, 5000.0f, "Hz" },
    { "xover5", "Crossover 5", ParameterType::floating, 20.0f, 20000.0f, 1.0f, 10000.0f, "Hz" },
});

static_assert(parameterSpecs.size() == numParameterSlots);
static_assert(fullbandVisibleSpecs.size() == numFullbandVisibleSlots);
static_assert(fullbandAutomationSpecs.size() == numFullbandAutomationSlots);
static_assert(crossoverSpecs.size() == numCrossoverSlots);

constexpr size_t toIndex(const ParameterSlot slot)
{
    return static_cast<size_t>(slot);
}

constexpr bool isAutomationOnlySlot(const ParameterSlot slot)
{
    return slot == ParameterSlot::autoInGn
        || slot == ParameterSlot::autoInRight
        || slot == ParameterSlot::autoInLeft;
}

juce::String getAutomationTargetName(const ParameterSlot slot)
{
    if (slot == ParameterSlot::autoInGn)
        return "INPUT-GAIN";

    if (slot == ParameterSlot::autoInRight)
        return "IN-RIGHT";

    if (slot == ParameterSlot::autoInLeft)
        return "IN-LEFT";

    jassertfalse;
    return {};
}

juce::String makeBandAutomationParameterName(const size_t bandIndex, const ParameterSlot slot)
{
    return "ENV BAND " + juce::String(static_cast<int>(bandIndex + 1)) + " " + getAutomationTargetName(slot);
}

juce::String makeBandParameterId(const size_t bandIndex, const char* suffix)
{
    return "band" + juce::String(static_cast<int>(bandIndex + 1)) + "_" + suffix;
}

juce::String makeFullbandParameterId(const char* suffix)
{
    return "fullband_" + juce::String(suffix);
}

juce::String makeBandGroupId(const size_t bandIndex)
{
    return "band" + juce::String(static_cast<int>(bandIndex + 1));
}

juce::String makeBandGroupName(const size_t bandIndex)
{
    return "Band " + juce::String(static_cast<int>(bandIndex + 1));
}

juce::String makeFullbandGroupId()
{
    return "fullband";
}

juce::String makeFullbandGroupName()
{
    return "Fullband";
}

juce::String makeSoloParameterId(const size_t bandIndex)
{
    return "soloBand" + juce::String(static_cast<int>(bandIndex + 1));
}

juce::String makeActiveSplitCountParameterId()
{
    return "fullband_activeXovers";
}

juce::AudioProcessorValueTreeState::ParameterLayout buildParameterLayout()
{
    using Layout = juce::AudioProcessorValueTreeState::ParameterLayout;
    using Parameter = std::unique_ptr<juce::RangedAudioParameter>;

    auto floatParam = [] (const juce::String& id,
                          const juce::String& name,
                          const float min,
                          const float max,
                          const float step,
                          const float defaultValue,
                          const juce::String& label,
                          const bool isAutomatable) -> Parameter
    {
        auto range = juce::NormalisableRange<float> { min, max, step };
        auto attributes = juce::AudioParameterFloatAttributes()
                              .withLabel(label)
                              .withAutomatable(isAutomatable)
                              .withStringFromValueFunction([] (float value, int)
                              {
                                  return formatParameterValue(value);
                              })
                              .withValueFromStringFunction([] (const juce::String& text)
                              {
                                  return text.trim().getFloatValue();
                              });
        return std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { id, 1 }, name, range, defaultValue, attributes);
    };

    auto boolParam = [] (const juce::String& id,
                         const juce::String& name,
                         const bool defaultValue,
                         const bool isAutomatable) -> Parameter
    {
        auto attributes = juce::AudioParameterBoolAttributes()
                              .withAutomatable(isAutomatable);
        return std::make_unique<juce::AudioParameterBool>(juce::ParameterID { id, 1 }, name, defaultValue, attributes);
    };

    Layout layout;
    auto soloGroup = std::make_unique<juce::AudioProcessorParameterGroup>("monitor", "Monitor", " | ");

    for (size_t bandIndex = 0; bandIndex < numBands; ++bandIndex)
        soloGroup->addChild(boolParam(makeSoloParameterId(bandIndex), "Solo Band " + juce::String(static_cast<int>(bandIndex + 1)), false, false));

    layout.add(std::move(soloGroup));

    auto fullbandGroup = std::make_unique<juce::AudioProcessorParameterGroup>(makeFullbandGroupId(),
                                                                              makeFullbandGroupName(),
                                                                              " | ");

    for (const auto& spec : fullbandVisibleSpecs)
        fullbandGroup->addChild(floatParam(makeFullbandParameterId(spec.suffix), spec.name, spec.min, spec.max, spec.step, spec.defaultValue, spec.label, false));

    for (const auto& spec : fullbandAutomationSpecs)
        fullbandGroup->addChild(floatParam(makeFullbandParameterId(spec.suffix), spec.name, spec.min, spec.max, spec.step, spec.defaultValue, spec.label, true));

    for (const auto& spec : crossoverSpecs)
        fullbandGroup->addChild(floatParam(makeFullbandParameterId(spec.suffix), spec.name, spec.min, spec.max, spec.step, spec.defaultValue, spec.label, false));

    fullbandGroup->addChild(floatParam(makeActiveSplitCountParameterId(), "Active Crossovers", 0.0f, 5.0f, 1.0f, 5.0f, "", false));

    layout.add(std::move(fullbandGroup));

    for (size_t bandIndex = 0; bandIndex < numBands; ++bandIndex)
    {
        auto group = std::make_unique<juce::AudioProcessorParameterGroup>(makeBandGroupId(bandIndex),
                                                                          makeBandGroupName(bandIndex),
                                                                          " | ");

        for (size_t parameterIndex = 0; parameterIndex < parameterSpecs.size(); ++parameterIndex)
        {
            const auto& spec = parameterSpecs[parameterIndex];
            const auto slot = static_cast<ParameterSlot>(parameterIndex);
            const auto parameterId = makeBandParameterId(bandIndex, spec.suffix);
            const auto isAutomatable = isAutomationOnlySlot(slot);
            const auto parameterName = isAutomatable ? makeBandAutomationParameterName(bandIndex, slot)
                                                     : juce::String(spec.name);

            if (spec.type == ParameterType::boolean)
                group->addChild(boolParam(parameterId, parameterName, spec.defaultValue >= 0.5f, isAutomatable));
            else
                group->addChild(floatParam(parameterId, parameterName, spec.min, spec.max, spec.step, spec.defaultValue, spec.label, isAutomatable));
        }

        layout.add(std::move(group));
    }

    return layout;
}
} // namespace

MxeAudioProcessor::MxeAudioProcessor()
    : juce::AudioProcessor(BusesProperties()
                               .withInput("Input", juce::AudioChannelSet::stereo(), true)
                               .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      valueTreeState(*this, nullptr, "PARAMETERS", createParameterLayout())
{
    cacheParameterPointers();
}

MxeAudioProcessor::~MxeAudioProcessor() = default;

void MxeAudioProcessor::prepareToPlay(const double sampleRate, const int samplesPerBlock)
{
    multibandProcessor.prepare(sampleRate, samplesPerBlock, getTotalNumOutputChannels());
    syncParameters();
    multibandProcessor.reset();
    setLatencySamples(multibandProcessor.getLatencySamples());
}

void MxeAudioProcessor::releaseResources()
{
}

bool MxeAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto mainInput = layouts.getMainInputChannelSet();
    const auto mainOutput = layouts.getMainOutputChannelSet();

    if (mainInput != mainOutput)
        return false;

    return mainOutput == juce::AudioChannelSet::mono()
        || mainOutput == juce::AudioChannelSet::stereo();
}

void MxeAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    for (auto channel = getTotalNumInputChannels(); channel < getTotalNumOutputChannels(); ++channel)
        buffer.clear(channel, 0, buffer.getNumSamples());

    syncParameters();

    multibandProcessor.process(buffer);
}

juce::AudioProcessorEditor* MxeAudioProcessor::createEditor()
{
    return new MxeAudioProcessorEditor(*this);
}

bool MxeAudioProcessor::hasEditor() const
{
    return true;
}

const juce::String MxeAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool MxeAudioProcessor::acceptsMidi() const
{
    return false;
}

bool MxeAudioProcessor::producesMidi() const
{
    return false;
}

bool MxeAudioProcessor::isMidiEffect() const
{
    return false;
}

double MxeAudioProcessor::getTailLengthSeconds() const
{
    return 0.0;
}

int MxeAudioProcessor::getNumPrograms()
{
    return 1;
}

int MxeAudioProcessor::getCurrentProgram()
{
    return 0;
}

void MxeAudioProcessor::setCurrentProgram(const int index)
{
    juce::ignoreUnused(index);
}

const juce::String MxeAudioProcessor::getProgramName(const int index)
{
    juce::ignoreUnused(index);
    return {};
}

void MxeAudioProcessor::changeProgramName(const int index, const juce::String& newName)
{
    juce::ignoreUnused(index, newName);
}

void MxeAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    if (auto stateXml = valueTreeState.copyState().createXml())
        copyXmlToBinary(*stateXml, destData);
}

void MxeAudioProcessor::setStateInformation(const void* data, const int sizeInBytes)
{
    if (auto xmlState = getXmlFromBinary(data, sizeInBytes))
    {
        if (xmlState->hasTagName(valueTreeState.state.getType()))
            valueTreeState.replaceState(juce::ValueTree::fromXml(*xmlState));
    }
}

juce::AudioProcessorValueTreeState& MxeAudioProcessor::getValueTreeState() noexcept
{
    return valueTreeState;
}

const juce::AudioProcessorValueTreeState& MxeAudioProcessor::getValueTreeState() const noexcept
{
    return valueTreeState;
}

juce::AudioProcessorValueTreeState::ParameterLayout MxeAudioProcessor::createParameterLayout()
{
    return buildParameterLayout();
}

void MxeAudioProcessor::cacheParameterPointers()
{
    rawActiveSplitCountParameter = valueTreeState.getRawParameterValue(makeActiveSplitCountParameterId());
    jassert(rawActiveSplitCountParameter != nullptr);

    for (size_t bandIndex = 0; bandIndex < numBands; ++bandIndex)
    {
        rawSoloParameters[bandIndex] = valueTreeState.getRawParameterValue(makeSoloParameterId(bandIndex));
        jassert(rawSoloParameters[bandIndex] != nullptr);
    }

    for (size_t parameterIndex = 0; parameterIndex < numFullbandAutomationSlots; ++parameterIndex)
    {
        rawFullbandParameters[parameterIndex] = valueTreeState.getRawParameterValue(makeFullbandParameterId(fullbandAutomationSpecs[parameterIndex].suffix));
        jassert(rawFullbandParameters[parameterIndex] != nullptr);
    }

    for (size_t parameterIndex = 0; parameterIndex < numFullbandVisibleSlots; ++parameterIndex)
    {
        rawFullbandVisibleParameters[parameterIndex] = valueTreeState.getRawParameterValue(makeFullbandParameterId(fullbandVisibleSpecs[parameterIndex].suffix));
        jassert(rawFullbandVisibleParameters[parameterIndex] != nullptr);
    }

    for (size_t parameterIndex = 0; parameterIndex < numCrossoverSlots; ++parameterIndex)
    {
        rawCrossoverParameters[parameterIndex] = valueTreeState.getRawParameterValue(makeFullbandParameterId(crossoverSpecs[parameterIndex].suffix));
        jassert(rawCrossoverParameters[parameterIndex] != nullptr);
    }

    for (size_t bandIndex = 0; bandIndex < numBands; ++bandIndex)
    {
        for (size_t parameterIndex = 0; parameterIndex < numParameterSlots; ++parameterIndex)
        {
            rawBandParameters[bandIndex][parameterIndex] = valueTreeState.getRawParameterValue(
                makeBandParameterId(bandIndex, parameterSpecs[parameterIndex].suffix));

            jassert(rawBandParameters[bandIndex][parameterIndex] != nullptr);
        }
    }
}

mxe::dsp::DspCore::Parameters MxeAudioProcessor::readBandParameters(const size_t bandIndex) const
{
    const auto loadFloat = [this, bandIndex] (const ParameterSlot slot)
    {
        if (const auto* value = rawBandParameters[bandIndex][toIndex(slot)])
            return value->load();

        jassertfalse;
        return 0.0f;
    };

    const auto loadBool = [&loadFloat] (const ParameterSlot slot)
    {
        return loadFloat(slot) >= 0.5f;
    };

    mxe::dsp::DspCore::Parameters parameters;
    parameters.inGn = loadFloat(ParameterSlot::inGn);
    parameters.inRight = loadFloat(ParameterSlot::inRight);
    parameters.inLeft = loadFloat(ParameterSlot::inLeft);
    parameters.autoInGn = loadFloat(ParameterSlot::autoInGn);
    parameters.autoInRight = loadFloat(ParameterSlot::autoInRight);
    parameters.autoInLeft = loadFloat(ParameterSlot::autoInLeft);
    parameters.wide = loadFloat(ParameterSlot::wide);
    parameters.thLU = loadFloat(ParameterSlot::thLU);
    parameters.mkLU = loadFloat(ParameterSlot::mkLU);
    parameters.thLD = loadFloat(ParameterSlot::thLD);
    parameters.mkLD = loadFloat(ParameterSlot::mkLD);
    parameters.thRU = loadFloat(ParameterSlot::thRU);
    parameters.mkRU = loadFloat(ParameterSlot::mkRU);
    parameters.thRD = loadFloat(ParameterSlot::thRD);
    parameters.mkRD = loadFloat(ParameterSlot::mkRD);
    parameters.hwBypass = loadBool(ParameterSlot::hwBypass);
    parameters.LLThResh = loadFloat(ParameterSlot::LLThResh);
    parameters.LLTension = loadFloat(ParameterSlot::LLTension);
    parameters.LLRelease = loadFloat(ParameterSlot::LLRelease);
    parameters.LLmk = loadFloat(ParameterSlot::LLmk);
    parameters.RRThResh = loadFloat(ParameterSlot::RRThResh);
    parameters.RRTension = loadFloat(ParameterSlot::RRTension);
    parameters.RRRelease = loadFloat(ParameterSlot::RRRelease);
    parameters.RRmk = loadFloat(ParameterSlot::RRmk);
    parameters.DMbypass = loadBool(ParameterSlot::DMbypass);
    parameters.FFThResh = loadFloat(ParameterSlot::FFThResh);
    parameters.FFTension = loadFloat(ParameterSlot::FFTension);
    parameters.FFRelease = loadFloat(ParameterSlot::FFRelease);
    parameters.FFmk = loadFloat(ParameterSlot::FFmk);
    parameters.FFbypass = loadBool(ParameterSlot::FFbypass);
    parameters.moRph = loadFloat(ParameterSlot::moRph);
    parameters.peakHoldHz = loadFloat(ParameterSlot::peakHoldHz);
    parameters.TensionFlooR = loadFloat(ParameterSlot::TensionFlooR);
    parameters.TensionHysT = loadFloat(ParameterSlot::TensionHysT);
    parameters.delTa = loadBool(ParameterSlot::delTa);
    return parameters;
}

mxe::dsp::MultibandProcessor::FullbandParameters MxeAudioProcessor::readFullbandParameters() const
{
    const auto loadAutomationFloat = [this] (const FullbandAutomationSlot slot)
    {
        if (const auto* value = rawFullbandParameters[static_cast<size_t>(slot)])
            return value->load();

        jassertfalse;
        return 0.0f;
    };

    const auto loadVisibleFloat = [this] (const FullbandVisibleSlot slot)
    {
        if (const auto* value = rawFullbandVisibleParameters[static_cast<size_t>(slot)])
            return value->load();

        jassertfalse;
        return 0.0f;
    };

    mxe::dsp::MultibandProcessor::FullbandParameters parameters;
    parameters.inGn = loadVisibleFloat(FullbandVisibleSlot::inGn);
    parameters.outGn = loadVisibleFloat(FullbandVisibleSlot::outGn);
    parameters.autoInGn = loadAutomationFloat(FullbandAutomationSlot::inGn);
    parameters.autoInRight = loadAutomationFloat(FullbandAutomationSlot::inRight);
    parameters.autoInLeft = loadAutomationFloat(FullbandAutomationSlot::inLeft);
    return parameters;
}

mxe::dsp::MultibandProcessor::CrossoverFrequencies MxeAudioProcessor::readCrossoverFrequencies() const
{
    mxe::dsp::MultibandProcessor::CrossoverFrequencies frequencies {};

    for (size_t parameterIndex = 0; parameterIndex < frequencies.size(); ++parameterIndex)
    {
        if (const auto* value = rawCrossoverParameters[parameterIndex])
            frequencies[parameterIndex] = static_cast<double>(value->load());
        else
            jassertfalse;
    }

    return frequencies;
}

size_t MxeAudioProcessor::readActiveSplitCount() const
{
    if (const auto* value = rawActiveSplitCountParameter)
        return static_cast<size_t>(juce::jlimit(0, static_cast<int>(numCrossoverSlots), static_cast<int>(std::round(value->load()))));

    jassertfalse;
    return numCrossoverSlots;
}

mxe::dsp::MultibandProcessor::SoloMask MxeAudioProcessor::readSoloMask() const
{
    mxe::dsp::MultibandProcessor::SoloMask soloMask {};

    for (size_t bandIndex = 0; bandIndex < numBands; ++bandIndex)
    {
        if (const auto* value = rawSoloParameters[bandIndex])
            soloMask[bandIndex] = value->load() >= 0.5f;
        else
            jassertfalse;
    }

    return soloMask;
}

void MxeAudioProcessor::syncParameters()
{
    for (size_t bandIndex = 0; bandIndex < numBands; ++bandIndex)
        currentBandParameters[bandIndex] = readBandParameters(bandIndex);

    currentFullbandParameters = readFullbandParameters();
    currentCrossoverFrequencies = readCrossoverFrequencies();
    currentActiveSplitCount = readActiveSplitCount();
    currentSoloMask = readSoloMask();
    multibandProcessor.setBandParameters(currentBandParameters);
    multibandProcessor.setActiveSplitCount(currentActiveSplitCount);
    multibandProcessor.setCrossoverFrequencies(currentCrossoverFrequencies);
    multibandProcessor.setFullbandParameters(currentFullbandParameters);
    multibandProcessor.setSoloMask(currentSoloMask);

    setLatencySamples(multibandProcessor.getLatencySamples());
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new MxeAudioProcessor();
}
