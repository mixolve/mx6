#pragma once

#include <JuceHeader.h>

#include "PluginProcessor.h"

#include <array>
#include <memory>

class MxeAudioProcessorEditor final : public juce::AudioProcessorEditor
{
public:
    explicit MxeAudioProcessorEditor(MxeAudioProcessor&);
    ~MxeAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    static constexpr size_t numBands = mxe::dsp::MultibandProcessor::numBands;
    static constexpr size_t numMonitorButtons = numBands + 1;

    void selectBand(size_t bandIndex);
    void toggleManualSolo(size_t bandIndex);
    void setAllBandsMonitoring();
    void setAutoSoloEnabled(bool shouldBeEnabled);
    void changeActiveSplitCount(int delta);
    void syncMonitorParameters();
    void updateMonitorButtons();
    void updateBandPageVisibility();
    void updatePageChangedIndicators();
    size_t getActiveSplitCount() const;
    size_t getActiveBandCount() const;

    std::array<std::unique_ptr<juce::Button>, numMonitorButtons> monitorButtons;
    std::array<juce::RangedAudioParameter*, numBands> soloParameters {};
    juce::RangedAudioParameter* activeSplitCountParameter = nullptr;
    std::array<std::unique_ptr<juce::Component>, numBands> bandPages;
    std::unique_ptr<juce::Component> allBandsPage;
    juce::Label footerLabel;
    size_t visibleBandIndex = 0;
    std::array<bool, numBands> manualSoloMask {};
    bool allBandsActive = true;
    bool autoSoloEnabled = true;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MxeAudioProcessorEditor)
};
