#include "PluginEditor.h"

#include <array>
#include <cmath>
#include <functional>
#include <memory>
#include <span>
#include <vector>

namespace
{
constexpr int fixedEditorWidth = 294;
constexpr int fixedEditorHeight = 516;
constexpr int editorInsetX = 4;
constexpr int editorInsetBottom = 4;
constexpr int parameterGap = 2;
constexpr int footerHeight = 20;
constexpr int pageColumnWidth = fixedEditorWidth - (editorInsetX * 2);
constexpr int sectionWidth = pageColumnWidth;
constexpr int sectionContentInsetX = 4;
constexpr int sectionRowGap = 6;
constexpr int controlRowWidth = sectionWidth - (sectionContentInsetX * 2);
constexpr int parameterValueWidth = 94;
constexpr int parameterNameWidth = controlRowWidth - parameterGap - parameterValueWidth;
constexpr int monitorButtonGap = 2;
constexpr int monitorRowOffsetY = 4;
constexpr float uiFontSize = 20.0f;
constexpr float valueBoxDragNormalisedPerPixel = 0.0050f;
constexpr float smoothWheelStepThreshold = 0.030f;
constexpr float wheelStepMultiplier = 2.0f;
constexpr float crossoverDragNormalisedPerPixel = 0.0005f;
constexpr float crossoverWheelStepMultiplier = 20.0f;
constexpr float crossoverMinGapHz = 1.0f;

const auto uiWhite = juce::Colour(0xffffffff);
const auto uiBlack = juce::Colour(0xff000000);
const auto uiAccent = juce::Colour(0xff9999ff);
const auto uiGrey950 = juce::Colour(0xff121212);
const auto uiGrey900 = juce::Colour(0xff1a1a1a);
const auto uiGrey800 = juce::Colour(0xff242424);
const auto uiGrey700 = juce::Colour(0xff363636);
const auto uiGrey500 = juce::Colour(0xff707070);

using ValueConstraint = std::function<float(float)>;

juce::FontOptions makeUiFont(const int styleFlags = juce::Font::plain, const float height = uiFontSize)
{
#if JUCE_TARGET_HAS_BINARY_DATA
    const auto useBold = (styleFlags & juce::Font::bold) != 0;

    static const auto regularTypeface = juce::Typeface::createSystemTypefaceFor(BinaryData::SometypeMonoRegular_ttf,
                                                                                BinaryData::SometypeMonoRegular_ttfSize);
    static const auto boldTypeface = juce::Typeface::createSystemTypefaceFor(BinaryData::SometypeMonoBold_ttf,
                                                                             BinaryData::SometypeMonoBold_ttfSize);

    if (auto typeface = useBold ? boldTypeface : regularTypeface)
        return juce::FontOptions(typeface).withHeight(height);
#endif

    return juce::FontOptions("Sometype Mono", height, styleFlags);
}

juce::String formatValueBoxText(const double value)
{
    auto rounded = std::round(value * 10.0) / 10.0;

    if (std::abs(rounded) < 0.05)
        rounded = 0.0;

    return juce::String::formatted("%+08.1f", rounded);
}

struct ControlSpec
{
    const char* suffix = "";
    const char* label = "";
    bool isToggle = false;
    bool tracksChangedState = true;
    bool valueInputOnly = false;
    float dragNormalisedPerPixel = valueBoxDragNormalisedPerPixel;
    float wheelMultiplier = wheelStepMultiplier;
};

struct SectionSpec
{
    const char* title = "";
    std::span<const ControlSpec> controls;
    bool startsExpanded = false;
    bool staysExpandedOnSelfClick = false;
};

constexpr auto halfWaveControls = std::to_array<ControlSpec>({
    { "thLU", "L-UP-THRESHOLD" },
    { "mkLU", "L-UP-OUT-GAIN" },
    { "thLD", "L-DOWN-THRESHOLD" },
    { "mkLD", "L-DOWN-OUT-GAIN" },
    { "thRU", "R-UP-THRESHOLD" },
    { "mkRU", "R-UP-OUT-GAIN" },
    { "thRD", "R-DOWN-THRESHOLD" },
    { "mkRD", "R-DOWN-OUT-GAIN" },
    { "hwBypass", "BYPASS", true, false },
});

constexpr auto dmControls = std::to_array<ControlSpec>({
    { "LLThResh", "L-THRESHOLD" },
    { "LLTension", "L-TENSION" },
    { "LLRelease", "L-RELEASE" },
    { "LLmk", "L-OUT-GAIN" },
    { "RRThResh", "R-THRESHOLD" },
    { "RRTension", "R-TENSION" },
    { "RRRelease", "R-RELEASE" },
    { "RRmk", "R-OUT-GAIN" },
    { "DMbypass", "BYPASS", true, false },
});

constexpr auto ffControls = std::to_array<ControlSpec>({
    { "FFThResh", "THRESHOLD" },
    { "FFTension", "TENSION" },
    { "FFRelease", "RELEASE" },
    { "FFmk", "OUT-GAIN" },
    { "FFbypass", "BYPASS", true, false },
});

constexpr auto globalControls = std::to_array<ControlSpec>({
    { "inGn", "INPUT-GAIN" },
    { "inRight", "IN-RIGHT" },
    { "inLeft", "IN-LEFT" },
    { "wide", "WIDE" },
    { "moRph", "MORPH" },
    { "peakHoldHz", "PEAK-HOLD" },
    { "TensionFlooR", "TEN-FLOOR" },
    { "TensionHysT", "TEN-HYST" },
    { "delTa", "DELTA", true, false },
});

constexpr auto fullbandControls = std::to_array<ControlSpec>({
    { "inGnVisible", "IN-GAIN" },
    { "outGnVisible", "OUT-GAIN" },
});

constexpr auto crossoverControls = std::to_array<ControlSpec>({
    { "xover1", "XOVER-1", false, true, false, crossoverDragNormalisedPerPixel, crossoverWheelStepMultiplier },
    { "xover2", "XOVER-2", false, true, false, crossoverDragNormalisedPerPixel, crossoverWheelStepMultiplier },
    { "xover3", "XOVER-3", false, true, false, crossoverDragNormalisedPerPixel, crossoverWheelStepMultiplier },
    { "xover4", "XOVER-4", false, true, false, crossoverDragNormalisedPerPixel, crossoverWheelStepMultiplier },
    { "xover5", "XOVER-5", false, true, false, crossoverDragNormalisedPerPixel, crossoverWheelStepMultiplier },
});

const auto halfWaveSection = SectionSpec { "HALF-WAVE", halfWaveControls, false, false };
const auto dmSection = SectionSpec { "DUAL-MONO", dmControls, false, false };
const auto ffSection = SectionSpec { "STEREO", ffControls, false, false };
const auto globalSection = SectionSpec { "GLOBAL", globalControls, true, false };
const auto fullbandSection = SectionSpec { "FULLBAND", fullbandControls, true, false };
const auto crossoverSection = SectionSpec { "CROSSOVER", crossoverControls, false, false };

juce::String makeBandParameterId(const size_t bandIndex, const char* suffix)
{
    return "band" + juce::String(static_cast<int>(bandIndex + 1)) + "_" + suffix;
}

juce::String makeFullbandParameterId(const char* suffix)
{
    return "fullband_" + juce::String(suffix);
}

juce::String makeSoloParameterId(const size_t bandIndex)
{
    return "soloBand" + juce::String(static_cast<int>(bandIndex + 1));
}

juce::String makeActiveSplitCountParameterId()
{
    return "fullband_activeXovers";
}

juce::String makeBandName(const size_t bandIndex)
{
    return juce::String(static_cast<int>(bandIndex + 1));
}

juce::Colour bandColour(const size_t bandIndex)
{
    juce::ignoreUnused(bandIndex);
    return uiAccent;
}

class WheelForwardingTextEditor final : public juce::TextEditor
{
public:
    explicit WheelForwardingTextEditor(juce::Slider& sliderToControl)
        : juce::TextEditor(sliderToControl.getName()),
          slider(sliderToControl)
    {
    }

    void mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel) override
    {
        slider.mouseWheelMove(event.getEventRelativeTo(&slider), wheel);
    }

private:
    juce::Slider& slider;
};

class WheelForwardingSliderLabel final : public juce::Label
{
public:
    explicit WheelForwardingSliderLabel(juce::Slider& sliderToControl)
        : juce::Label({}, {}),
          slider(sliderToControl)
    {
        setMouseCursor(juce::MouseCursor::UpDownResizeCursor);
        setEditable(false, false, false);
    }

    std::function<void()> onResetRequest;
    std::function<void(const juce::MouseEvent&)> onDragStart;
    std::function<void(const juce::MouseEvent&)> onDragMove;
    std::function<void(const juce::MouseEvent&)> onDragEnd;

    void mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel) override
    {
        slider.mouseWheelMove(event.getEventRelativeTo(&slider), wheel);
    }

    void mouseDown(const juce::MouseEvent& event) override
    {
        if (event.mods.isPopupMenu())
            return;

        if (event.mods.isShiftDown())
            return;

        if (event.mods.isLeftButtonDown() && onDragStart)
            onDragStart(event);
    }

    void mouseDrag(const juce::MouseEvent& event) override
    {
        if (event.mods.isPopupMenu() || event.mods.isShiftDown())
            return;

        if (event.mods.isLeftButtonDown() && onDragMove)
            onDragMove(event);
    }

    void mouseUp(const juce::MouseEvent& event) override
    {
        if (event.mods.isPopupMenu())
        {
            showEditor();

            return;
        }

        if (event.mods.isShiftDown())
        {
            if (onResetRequest)
                onResetRequest();

            return;
        }

        if (onDragEnd)
            onDragEnd(event);
    }

    void mouseDoubleClick(const juce::MouseEvent& event) override
    {
        juce::ignoreUnused(event);
    }

    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override
    {
        return createIgnoredAccessibilityHandler(*this);
    }

protected:
    juce::TextEditor* createEditorComponent() override
    {
        auto* editor = new WheelForwardingTextEditor(slider);
        editor->applyFontToAllText(getLookAndFeel().getLabelFont(*this));
        editor->setJustification(juce::Justification::centredRight);
        copyAllExplicitColoursTo(*editor);
        return editor;
    }

private:
    juce::Slider& slider;
};

class ResettableValueSlider final : public juce::Slider
{
public:
    std::function<void()> onResetRequest;
    std::function<void(const juce::MouseEvent&)> onValueBoxDragStart;
    std::function<void(const juce::MouseEvent&)> onValueBoxDragMove;
    std::function<void(const juce::MouseEvent&)> onValueBoxDragEnd;
};

class ResettableParameterLabel final : public juce::Label
{
public:
    using juce::Label::Label;
};

class ValueBoxComponent final : public juce::Component
{
public:
    ValueBoxComponent(ResettableValueSlider& sliderToControl,
                      juce::RangedAudioParameter* parameterToControl,
                      ValueConstraint valueConstraintIn,
                      const bool allowGestureEditingIn,
                      const float dragNormalisedPerPixelIn,
                      const float wheelStepMultiplierIn)
        : slider(sliderToControl),
          parameter(parameterToControl),
          valueConstraint(std::move(valueConstraintIn)),
          allowGestureEditing(allowGestureEditingIn),
          dragNormalisedPerPixel(dragNormalisedPerPixelIn),
          wheelMultiplier(wheelStepMultiplierIn)
    {
        setMouseCursor(allowGestureEditing ? juce::MouseCursor::UpDownResizeCursor
                                           : juce::MouseCursor::IBeamCursor);
    }

    ~ValueBoxComponent() override
    {
        stopGlobalEditTracking();
    }

    std::function<void()> onResetRequest;

    void setOutlineColour(const juce::Colour colour)
    {
        if (outlineColour == colour)
            return;

        outlineColour = colour;

        if (editor != nullptr)
            editor->setColour(juce::TextEditor::outlineColourId, outlineColour);

        repaint();
    }

    void setHighlightColour(const juce::Colour colour)
    {
        if (highlightColour == colour)
            return;

        highlightColour = colour;

        if (editor != nullptr)
            editor->setColour(juce::TextEditor::highlightColourId, highlightColour);
    }

    void paint(juce::Graphics& graphics) override
    {
        graphics.setColour(uiGrey800);
        graphics.fillRect(getLocalBounds());

        graphics.setColour(outlineColour);
        graphics.drawRect(getLocalBounds(), 1);

        graphics.setColour(uiWhite);
        graphics.setFont(makeUiFont());
        graphics.drawFittedText(formatValueBoxText(slider.getValue()),
                                getLocalBounds().reduced(4, 0),
                                juce::Justification::centredRight,
                                1,
                                1.0f);
    }

    void resized() override
    {
        if (editor != nullptr)
            editor->setBounds(getLocalBounds());
    }

    void mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel) override
    {
        juce::ignoreUnused(event);

        if (! allowGestureEditing)
            return;

        if (parameter == nullptr)
            return;

        if (editor != nullptr)
            hideEditor(false);

        if (wheel.isInertial)
            return;

        const auto directionalDelta = (std::abs(wheel.deltaX) > std::abs(wheel.deltaY) ? -wheel.deltaX : wheel.deltaY)
            * (wheel.isReversed ? -1.0f : 1.0f);

        if (std::abs(directionalDelta) < 1.0e-6f)
            return;

        const auto range = parameter->getNormalisableRange();
        const auto interval = range.interval > 0.0f ? range.interval : juce::jmax(0.001f, (range.end - range.start) * 0.001f);

        int stepCount = 0;

        if (wheel.isSmooth)
        {
            smoothWheelAccumulator += directionalDelta;

            while (smoothWheelAccumulator >= smoothWheelStepThreshold)
            {
                ++stepCount;
                smoothWheelAccumulator -= smoothWheelStepThreshold;
            }

            while (smoothWheelAccumulator <= -smoothWheelStepThreshold)
            {
                --stepCount;
                smoothWheelAccumulator += smoothWheelStepThreshold;
            }
        }
        else
        {
            smoothWheelAccumulator = 0.0f;
            stepCount = directionalDelta > 0.0f ? 1 : -1;
        }

        if (stepCount == 0)
            return;

        const auto currentValue = parameter->convertFrom0to1(parameter->getValue());
        const auto unclampedValue = currentValue + (interval * wheelMultiplier * static_cast<float>(stepCount));
        const auto constrainedValue = constrainValue(unclampedValue);
        const auto legalValue = range.snapToLegalValue(juce::jlimit(range.start, range.end, constrainedValue));

        parameter->beginChangeGesture();
        parameter->setValueNotifyingHost(parameter->convertTo0to1(legalValue));
        parameter->endChangeGesture();
        repaint();
    }

    void mouseDown(const juce::MouseEvent& event) override
    {
        auto* clickedComponent = event.originalComponent;
        const auto clickIsInsideThisValueBox = clickedComponent != nullptr
            && (clickedComponent == this || isParentOf(clickedComponent));

        if (editor != nullptr && ! clickIsInsideThisValueBox)
        {
            hideEditor(false);
            return;
        }

        if (! clickIsInsideThisValueBox)
            return;

        if (event.mods.isPopupMenu())
            return;

        if (event.mods.isShiftDown() && allowGestureEditing)
        {
            if (onResetRequest)
                onResetRequest();

            return;
        }

        if (! allowGestureEditing || ! event.mods.isLeftButtonDown() || parameter == nullptr)
            return;

        dragStartNormalisedValue = parameter->getValue();

        if (! isDragging)
        {
            parameter->beginChangeGesture();
            isDragging = true;
        }
    }

    void mouseDrag(const juce::MouseEvent& event) override
    {
        if (! isDragging || parameter == nullptr)
            return;

        const auto deltaPixels = -event.getDistanceFromDragStartY();
        const auto newNormalisedValue = juce::jlimit(0.0f,
                                                     1.0f,
                                                     dragStartNormalisedValue + (static_cast<float>(deltaPixels) * dragNormalisedPerPixel));
        const auto newValue = constrainValue(parameter->convertFrom0to1(newNormalisedValue));

        parameter->setValueNotifyingHost(parameter->convertTo0to1(newValue));
    }

    void mouseUp(const juce::MouseEvent& event) override
    {
        if (isDragging && parameter != nullptr)
        {
            parameter->endChangeGesture();
            isDragging = false;
            return;
        }

        if (event.mods.isPopupMenu())
            showEditor();
    }

private:
    void showEditor()
    {
        if (editor != nullptr)
            return;

        auto textEditor = std::make_unique<WheelForwardingTextEditor>(slider);
        textEditor->setFont(makeUiFont());
        textEditor->setJustification(juce::Justification::centredRight);
        textEditor->setColour(juce::TextEditor::textColourId, uiWhite);
        textEditor->setColour(juce::TextEditor::backgroundColourId, uiGrey800);
        textEditor->setColour(juce::TextEditor::outlineColourId, outlineColour);
        textEditor->setColour(juce::TextEditor::focusedOutlineColourId, outlineColour);
        textEditor->setColour(juce::TextEditor::highlightColourId, highlightColour);
        textEditor->setText(formatValueBoxText(slider.getValue()), false);
        textEditor->onReturnKey = [this] { hideEditor(false); };
        textEditor->onEscapeKey = [this] { hideEditor(true); };
        textEditor->onFocusLost = [this] { hideEditor(false); };

        addAndMakeVisible(*textEditor);
        editor = std::move(textEditor);
        startGlobalEditTracking();
        resized();
        editor->grabKeyboardFocus();
        editor->selectAll();
    }

    void hideEditor(const bool discard)
    {
        if (editor == nullptr)
            return;

        if (! discard && parameter != nullptr)
        {
            const auto enteredValue = editor->getText().trim().getDoubleValue();
            const auto range = parameter->getNormalisableRange();
            const auto clampedValue = juce::jlimit(static_cast<double>(range.start),
                                                   static_cast<double>(range.end),
                                                   enteredValue);
            const auto legalValue = range.snapToLegalValue(constrainValue(static_cast<float>(clampedValue)));

            parameter->beginChangeGesture();
            parameter->setValueNotifyingHost(parameter->convertTo0to1(legalValue));
            parameter->endChangeGesture();
        }

        removeChildComponent(editor.get());
        editor.reset();
        stopGlobalEditTracking();
        repaint();
    }

    void startGlobalEditTracking()
    {
        if (isTrackingGlobalClicks)
            return;

        juce::Desktop::getInstance().addGlobalMouseListener(this);
        isTrackingGlobalClicks = true;
    }

    void stopGlobalEditTracking()
    {
        if (! isTrackingGlobalClicks)
            return;

        juce::Desktop::getInstance().removeGlobalMouseListener(this);
        isTrackingGlobalClicks = false;
    }

    float constrainValue(const float value) const
    {
        if (valueConstraint)
            return valueConstraint(value);

        return value;
    }

    ResettableValueSlider& slider;
    juce::RangedAudioParameter* parameter = nullptr;
    ValueConstraint valueConstraint;
    bool allowGestureEditing = true;
    float dragNormalisedPerPixel = valueBoxDragNormalisedPerPixel;
    float wheelMultiplier = wheelStepMultiplier;
    juce::Colour outlineColour = uiGrey500;
    juce::Colour highlightColour = uiAccent;
    bool isDragging = false;
    bool isTrackingGlobalClicks = false;
    float dragStartNormalisedValue = 0.0f;
    float smoothWheelAccumulator = 0.0f;
    std::unique_ptr<WheelForwardingTextEditor> editor;
};

class MxeLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    juce::Font getLabelFont(juce::Label&) override
    {
        return makeUiFont();
    }

    juce::Label* createSliderTextBox(juce::Slider& slider) override
    {
        auto* label = new WheelForwardingSliderLabel(slider);

        if (auto* resettableSlider = dynamic_cast<ResettableValueSlider*>(&slider))
        {
            label->onResetRequest = resettableSlider->onResetRequest;
            label->onDragStart = resettableSlider->onValueBoxDragStart;
            label->onDragMove = resettableSlider->onValueBoxDragMove;
            label->onDragEnd = resettableSlider->onValueBoxDragEnd;
        }

        label->setJustificationType(juce::Justification::centredRight);
        label->setKeyboardType(juce::TextInputTarget::decimalKeyboard);

        label->setColour(juce::Label::textColourId, slider.findColour(juce::Slider::textBoxTextColourId));
        label->setColour(juce::Label::backgroundColourId,
                         (slider.getSliderStyle() == juce::Slider::LinearBar || slider.getSliderStyle() == juce::Slider::LinearBarVertical)
                             ? juce::Colours::transparentBlack
                             : slider.findColour(juce::Slider::textBoxBackgroundColourId));
        label->setColour(juce::Label::outlineColourId, slider.findColour(juce::Slider::textBoxOutlineColourId));
        label->setColour(juce::TextEditor::textColourId, slider.findColour(juce::Slider::textBoxTextColourId));
        label->setColour(juce::TextEditor::backgroundColourId, slider.findColour(juce::Slider::textBoxBackgroundColourId));
        label->setColour(juce::TextEditor::outlineColourId, slider.findColour(juce::Slider::textBoxOutlineColourId));
        label->setColour(juce::TextEditor::focusedOutlineColourId, slider.findColour(juce::Slider::textBoxOutlineColourId));
        label->setColour(juce::TextEditor::highlightColourId, slider.findColour(juce::Slider::textBoxHighlightColourId));

        return label;
    }

    Slider::SliderLayout getSliderLayout(juce::Slider& slider) override
    {
        return { {}, slider.getLocalBounds() };
    }

    int getTabButtonBestWidth(juce::TabBarButton& button, int tabDepth) override
    {
        juce::ignoreUnused(button);
        return juce::jlimit(14, 18, tabDepth);
    }

    juce::Font getTabButtonFont(juce::TabBarButton& button, float height) override
    {
        juce::ignoreUnused(button);
        juce::ignoreUnused(height);
        return withDefaultMetrics(makeUiFont(juce::Font::bold));
    }
};

MxeLookAndFeel& getMxeLookAndFeel()
{
    static MxeLookAndFeel lookAndFeel;
    return lookAndFeel;
}

class BoxTextButton final : public juce::TextButton
{
public:
    explicit BoxTextButton(const juce::Colour accent)
        : accentColour(accent)
    {
    }

    void setChangedState(const bool shouldBeChanged) { juce::ignoreUnused(shouldBeChanged); }
    void setTextJustification(const juce::Justification justification) { textJustification = justification; }

    void paintButton(juce::Graphics& graphics, bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override
    {
        juce::ignoreUnused(shouldDrawButtonAsHighlighted);

        const auto fill = shouldDrawButtonAsDown ? uiGrey700 : uiGrey800;
        const auto outline = getToggleState() ? accentColour : uiGrey500;
        const auto textColour = uiWhite;

        graphics.setColour(fill);
        graphics.fillRect(getLocalBounds());

        graphics.setColour(outline);
        graphics.drawRect(getLocalBounds(), 1);

        graphics.setColour(textColour);
        graphics.setFont(makeUiFont());
        const auto textBounds = textJustification.testFlags(juce::Justification::left)
            ? getLocalBounds().withTrimmedLeft(6).reduced(0, 3)
            : getLocalBounds().reduced(3);
        graphics.drawFittedText(getButtonText(), textBounds, textJustification, 1, 1.0f);
    }

private:
    juce::Colour accentColour;
    juce::Justification textJustification = juce::Justification::centred;
};

class ParameterControl final : public juce::Component
{
public:
    ParameterControl(juce::AudioProcessorValueTreeState& state,
                     const juce::String& parameterId,
                     const ControlSpec& spec,
                     const juce::Colour accent,
                     std::function<void()> onChangedStateChangeIn,
                     ValueConstraint valueConstraintIn = {},
                     std::function<void()> onSoloClickIn = {},
                     std::function<bool()> isSoloActiveIn = {},
                     std::function<bool()> isSoloEnabledIn = {})
        : isToggle(spec.isToggle),
          hasSoloButton(spec.isToggle && juce::String(spec.suffix) == "delTa" && onSoloClickIn != nullptr),
          tracksChangedState(spec.tracksChangedState),
          accentColour(accent),
          parameter(dynamic_cast<juce::RangedAudioParameter*>(state.getParameter(parameterId))),
          onChangedStateChange(std::move(onChangedStateChangeIn)),
          valueConstraint(std::move(valueConstraintIn)),
          onSoloClick(std::move(onSoloClickIn)),
          isSoloActive(std::move(isSoloActiveIn)),
          isSoloEnabled(std::move(isSoloEnabledIn)),
          toggle(accent),
          soloButton(accent)
    {
        jassert(parameter != nullptr);

        if (isToggle)
        {
            toggle.setButtonText(spec.label);
            toggle.setClickingTogglesState(true);
            toggle.onStateChange = [this]
            {
                refreshChangedAppearance();
            };
            buttonAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
                state,
                parameterId,
                toggle);

            addAndMakeVisible(toggle);

            if (hasSoloButton)
            {
                soloButton.setButtonText("SOLO");
                soloButton.setClickingTogglesState(false);
                soloButton.onClick = [this]
                {
                    if (onSoloClick)
                        onSoloClick();

                    refreshExternalState();
                };
                addAndMakeVisible(soloButton);
                refreshExternalState();
            }
        }
        else
        {
            title.setText(spec.label, juce::dontSendNotification);
            title.setJustificationType(juce::Justification::centredLeft);
            title.setColour(juce::Label::textColourId, uiWhite);
            title.setColour(juce::Label::backgroundColourId, uiGrey800);
            title.setColour(juce::Label::outlineColourId, uiGrey500);
            title.setFont(makeUiFont());
            title.setBorderSize({ 0, 6, 0, 2 });
            title.setMinimumHorizontalScale(1.0f);

            slider.setSliderStyle(juce::Slider::LinearHorizontal);
            slider.setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
            slider.setLookAndFeel(&getMxeLookAndFeel());
            slider.setInterceptsMouseClicks(false, false);
            slider.setAlpha(0.0f);
            slider.onResetRequest = [this]
            {
                resetParameterToDefault();
            };
            slider.onValueChange = [this]
            {
                if (valueBox != nullptr)
                    valueBox->repaint();

                refreshChangedAppearance();
            };
            slider.textFromValueFunction = [] (const double value)
            {
                return formatValueBoxText(value);
            };
            slider.valueFromTextFunction = [] (const juce::String& text)
            {
                return text.trim().getDoubleValue();
            };
            slider.setColour(juce::Slider::thumbColourId, uiBlack);
            slider.setColour(juce::Slider::trackColourId, uiBlack);
            slider.setColour(juce::Slider::backgroundColourId, uiBlack);
            slider.setColour(juce::Slider::textBoxBackgroundColourId, uiGrey800);
            slider.setColour(juce::Slider::textBoxTextColourId, uiWhite);
            slider.setColour(juce::Slider::textBoxOutlineColourId, uiGrey500);
            slider.setColour(juce::Slider::textBoxHighlightColourId, accentColour);
            sliderAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
                state,
                parameterId,
                slider);

            valueBox = std::make_unique<ValueBoxComponent>(slider,
                                                           parameter,
                                                           valueConstraint,
                                                           ! spec.valueInputOnly,
                                                           spec.dragNormalisedPerPixel,
                                                           spec.wheelMultiplier);
            valueBox->onResetRequest = [this]
            {
                resetParameterToDefault();
            };
            applyCurrentAppearance();

            addAndMakeVisible(title);
            addAndMakeVisible(slider);
            addAndMakeVisible(*valueBox);
        }

        refreshChangedAppearance();
        applyCurrentAppearance();
        callbacksEnabled = true;
    }

    int getPreferredHeight() const noexcept
    {
        return isToggle ? 24 : 26;
    }

    bool isChangedFromDefault() const noexcept
    {
        return changedState;
    }

    void setControlEnabled(const bool shouldBeEnabled)
    {
        setEnabled(shouldBeEnabled);
        setAlpha(shouldBeEnabled ? 1.0f : 0.45f);
    }

    void refreshExternalState()
    {
        if (! hasSoloButton)
            return;

        const auto enabled = isSoloEnabled == nullptr || isSoloEnabled();
        soloButton.setEnabled(enabled);
        soloButton.setAlpha(enabled ? 1.0f : 0.45f);
        soloButton.setToggleState(enabled && isSoloActive != nullptr && isSoloActive(), juce::dontSendNotification);
    }

    ~ParameterControl() override
    {
        if (! isToggle)
            slider.setLookAndFeel(nullptr);
    }

    void resized() override
    {
        auto bounds = getLocalBounds();

        if (isToggle)
        {
            if (hasSoloButton)
            {
                auto row = bounds;
                toggle.setBounds(row.removeFromLeft(parameterNameWidth));
                row.removeFromLeft(parameterGap);
                soloButton.setBounds(row);
                refreshExternalState();
                return;
            }

            toggle.setBounds(bounds);
            return;
        }

        auto row = bounds;
        title.setBounds(row.removeFromLeft(parameterNameWidth));
        row.removeFromLeft(parameterGap);
        slider.setBounds(row);

        if (valueBox != nullptr)
            valueBox->setBounds(row);
    }

private:
    void applyCurrentAppearance()
    {
        if (isToggle)
        {
            toggle.setChangedState(changedState);
            return;
        }

        const auto outline = uiGrey500;
        const auto highlight = accentColour;
        title.setColour(juce::Label::outlineColourId, outline);

        if (valueBox != nullptr)
        {
            valueBox->setOutlineColour(outline);
            valueBox->setHighlightColour(highlight);
        }
    }

    void resetParameterToDefault()
    {
        if (parameter == nullptr)
            return;

        parameter->beginChangeGesture();
        auto defaultValue = parameter->convertFrom0to1(parameter->getDefaultValue());

        if (valueConstraint)
            defaultValue = valueConstraint(defaultValue);

        parameter->setValueNotifyingHost(parameter->convertTo0to1(defaultValue));
        parameter->endChangeGesture();
    }

    void refreshChangedAppearance()
    {
        if (! tracksChangedState)
        {
            if (changedState)
            {
                changedState = false;
                applyCurrentAppearance();

                if (callbacksEnabled && onChangedStateChange)
                    onChangedStateChange();
            }

            return;
        }

        const auto shouldBeChanged = parameter != nullptr
            && std::abs(parameter->getValue() - parameter->getDefaultValue()) > 1.0e-6f;

        if (changedState == shouldBeChanged)
            return;

        changedState = shouldBeChanged;
        applyCurrentAppearance();

        if (callbacksEnabled && onChangedStateChange)
            onChangedStateChange();
    }

    const bool isToggle = false;
    const bool hasSoloButton = false;
    const bool tracksChangedState = true;
    const juce::Colour accentColour;
    juce::RangedAudioParameter* parameter = nullptr;
    std::function<void()> onChangedStateChange;
    ValueConstraint valueConstraint;
    std::function<void()> onSoloClick;
    std::function<bool()> isSoloActive;
    std::function<bool()> isSoloEnabled;
    bool callbacksEnabled = false;
    bool changedState = false;
    BoxTextButton toggle;
    BoxTextButton soloButton;
    ResettableParameterLabel title;
    ResettableValueSlider slider;
    std::unique_ptr<ValueBoxComponent> valueBox;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> sliderAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> buttonAttachment;
};

class SectionComponent final : public juce::Component
{
public:
    struct ExtraControlPlacement
    {
        size_t afterVisibleControlCount = 0;
    };

    SectionComponent(juce::AudioProcessorValueTreeState& state,
                     const std::function<juce::String(const char*)>& parameterIdProvider,
                     const SectionSpec& spec,
                     const juce::Colour accent,
                     std::function<void()> onLayoutChangeIn,
                     std::function<void()> onChangedStateChangeIn,
                     std::function<void(bool)> onExpandedChangedIn,
                     std::function<ValueConstraint(const char*)> valueConstraintProvider = {},
                     std::function<void(const char*)> soloClickProvider = {},
                     std::function<bool(const char*)> soloActiveProvider = {},
                     std::function<bool(const char*)> soloEnabledProvider = {},
                     std::function<size_t()> extraRowsBeforeControlsProviderIn = {},
                     std::function<size_t()> enabledControlCountProviderIn = {})
        : accentColour(accent),
          onLayoutChange(std::move(onLayoutChangeIn)),
          onChangedStateChange(std::move(onChangedStateChangeIn)),
          onExpandedChanged(std::move(onExpandedChangedIn)),
          extraRowsBeforeControlsProvider(std::move(extraRowsBeforeControlsProviderIn)),
          enabledControlCountProvider(std::move(enabledControlCountProviderIn)),
          staysExpandedOnSelfClick(spec.staysExpandedOnSelfClick),
          headerButton(accent)
    {
        headerButton.setButtonText(spec.title);
        headerButton.setClickingTogglesState(true);
        headerButton.setToggleState(spec.startsExpanded, juce::dontSendNotification);
        headerButton.onClick = [this]
        {
            if (! headerButton.getToggleState() && staysExpandedOnSelfClick)
                headerButton.setToggleState(true, juce::dontSendNotification);

            updateExpandedState();

            if (onExpandedChanged)
                onExpandedChanged(headerButton.getToggleState());

            if (onLayoutChange)
                onLayoutChange();
        };
        addAndMakeVisible(headerButton);

        controls.reserve(spec.controls.size());

        for (const auto& controlSpec : spec.controls)
        {
            auto control = std::make_unique<ParameterControl>(state,
                                                              parameterIdProvider(controlSpec.suffix),
                                                              controlSpec,
                                                              accentColour,
                                                              [this]
                                                              {
                                                                  refreshChangedAppearance();
                                                              },
                                                              valueConstraintProvider ? valueConstraintProvider(controlSpec.suffix) : ValueConstraint {},
                                                              soloClickProvider ? std::function<void()> { [soloClickProvider, suffix = controlSpec.suffix] { soloClickProvider(suffix); } } : std::function<void()> {},
                                                              soloActiveProvider ? std::function<bool()> { [soloActiveProvider, suffix = controlSpec.suffix] { return soloActiveProvider(suffix); } } : std::function<bool()> {},
                                                              soloEnabledProvider ? std::function<bool()> { [soloEnabledProvider, suffix = controlSpec.suffix] { return soloEnabledProvider(suffix); } } : std::function<bool()> {});
            addAndMakeVisible(*control);
            controls.push_back(std::move(control));
        }

        updateExpandedState();
        refreshChangedAppearance();
        callbacksEnabled = true;
    }

    int getPreferredHeight() const
    {
        auto height = 22;

        if (headerButton.getToggleState())
        {
            auto hasRows = false;

            for (size_t rowIndex = 0; rowIndex < getExtraRowsBeforeControls(); ++rowIndex)
            {
                if (hasRows)
                    height += sectionRowGap;

                height += 24;
                hasRows = true;
            }

            for (const auto& control : controls)
            {
                if (hasRows)
                    height += sectionRowGap;

                height += control->getPreferredHeight();
                hasRows = true;
            }
        }

        return height;
    }

    bool isChangedFromDefault() const noexcept
    {
        return changedState;
    }

    bool isExpanded() const noexcept
    {
        return headerButton.getToggleState();
    }

    void refreshExternalState()
    {
        updateExpandedState();

        for (auto& control : controls)
            control->refreshExternalState();
    }

    juce::Rectangle<int> getExtraControlBounds(const size_t extraControlIndex) const
    {
        return getExtraControlBounds(extraControlIndex, {});
    }

    juce::Rectangle<int> getExtraControlBounds(const size_t extraControlIndex, const ExtraControlPlacement placement) const
    {
        if (! headerButton.getToggleState())
            return {};

        const auto existingControlsBeforeExtra = juce::jmin(controls.size(), placement.afterVisibleControlCount);

        auto bounds = getLocalBounds().withTrimmedLeft(sectionContentInsetX).withTrimmedRight(sectionContentInsetX);
        bounds.removeFromTop(22);

        if (placement.afterVisibleControlCount == 0)
        {
            for (size_t index = 0; index < extraControlIndex; ++index)
            {
                bounds.removeFromTop(24);

                if (index + 1 < extraControlIndex)
                    bounds.removeFromTop(sectionRowGap);
            }

            return bounds.removeFromTop(24);
        }

        for (size_t rowIndex = 0; rowIndex < getExtraRowsBeforeControls(); ++rowIndex)
        {
            bounds.removeFromTop(24);

            if (rowIndex + 1 < getExtraRowsBeforeControls() || existingControlsBeforeExtra > 0 || extraControlIndex > 0)
                bounds.removeFromTop(sectionRowGap);
        }

        const auto controlsBeforeExtra = existingControlsBeforeExtra;

        for (size_t controlIndex = 0; controlIndex < controlsBeforeExtra; ++controlIndex)
        {
            bounds.removeFromTop(controls[controlIndex]->getPreferredHeight());

            if (controlIndex + 1 < controlsBeforeExtra || extraControlIndex > 0)
                bounds.removeFromTop(sectionRowGap);
        }

        for (size_t index = 0; index < extraControlIndex; ++index)
        {
            bounds.removeFromTop(24);

            if (index + 1 < extraControlIndex)
                bounds.removeFromTop(sectionRowGap);
        }

        return bounds.removeFromTop(24);
    }

    void setExpanded(const bool shouldBeExpanded)
    {
        if (headerButton.getToggleState() == shouldBeExpanded)
            return;

        headerButton.setToggleState(shouldBeExpanded, juce::dontSendNotification);
        updateExpandedState();
    }

    void paint(juce::Graphics& graphics) override
    {
        juce::ignoreUnused(graphics);
    }

    void resized() override
    {
        auto bounds = getLocalBounds().withTrimmedLeft(sectionContentInsetX).withTrimmedRight(sectionContentInsetX);
        headerButton.setBounds(bounds.removeFromTop(22));

        if (! headerButton.getToggleState())
            return;

        auto hasRows = false;

        for (size_t rowIndex = 0; rowIndex < getExtraRowsBeforeControls(); ++rowIndex)
        {
            if (hasRows)
                bounds.removeFromTop(sectionRowGap);

            bounds.removeFromTop(24);
            hasRows = true;
        }

        for (size_t controlIndex = 0; controlIndex < controls.size(); ++controlIndex)
        {
            auto& control = controls[controlIndex];
            const auto height = control->getPreferredHeight();

            if (hasRows)
                bounds.removeFromTop(sectionRowGap);

            control->setBounds(bounds.removeFromTop(height));
            hasRows = true;
        }
    }

private:
    void refreshChangedAppearance()
    {
        bool shouldBeChanged = false;

        for (const auto& control : controls)
        {
            if (control->isChangedFromDefault())
            {
                shouldBeChanged = true;
                break;
            }
        }

        if (changedState == shouldBeChanged)
            return;

        changedState = shouldBeChanged;
        headerButton.setChangedState(changedState);

        if (callbacksEnabled && onChangedStateChange)
            onChangedStateChange();
    }

    void updateExpandedState()
    {
        const auto expanded = headerButton.getToggleState();
        const auto enabledControlCount = getEnabledControlCount();

        for (size_t controlIndex = 0; controlIndex < controls.size(); ++controlIndex)
        {
            controls[controlIndex]->setVisible(expanded);
            controls[controlIndex]->setControlEnabled(controlIndex < enabledControlCount);
        }
    }

    size_t getExtraRowsBeforeControls() const
    {
        return extraRowsBeforeControlsProvider != nullptr ? extraRowsBeforeControlsProvider() : 0;
    }

    size_t getEnabledControlCount() const
    {
        if (enabledControlCountProvider)
            return enabledControlCountProvider();

        return controls.size();
    }

    const juce::Colour accentColour;
    std::function<void()> onLayoutChange;
    std::function<void()> onChangedStateChange;
    std::function<void(bool)> onExpandedChanged;
    std::function<size_t()> extraRowsBeforeControlsProvider;
    std::function<size_t()> enabledControlCountProvider;
    const bool staysExpandedOnSelfClick = false;
    bool callbacksEnabled = false;
    bool changedState = false;
    BoxTextButton headerButton;
    std::vector<std::unique_ptr<ParameterControl>> controls;
};

class BandPageComponent final : public juce::Component
{
public:
    BandPageComponent(juce::AudioProcessorValueTreeState& state,
                      const std::function<juce::String(const char*)>& parameterIdProvider,
                      const juce::Colour accent,
                      std::function<void()> onChangedStateChangeIn,
                      std::function<void()> onSoloClickIn = {},
                      std::function<bool()> isSoloActiveIn = {},
                      std::function<bool()> isSoloEnabledIn = {})
        : onChangedStateChange(std::move(onChangedStateChangeIn)),
          onSoloClick(std::move(onSoloClickIn)),
          isSoloActive(std::move(isSoloActiveIn)),
          isSoloEnabled(std::move(isSoloEnabledIn)),
          halfWave(state,
                   parameterIdProvider,
                   halfWaveSection,
                   accent,
                   [this] { resized(); },
                   [this] { refreshChangedAppearance(); },
                   [this] (bool expanded) { handleSectionExpanded(0, expanded); }),
          dm(state,
             parameterIdProvider,
             dmSection,
             accent,
             [this] { resized(); },
             [this] { refreshChangedAppearance(); },
             [this] (bool expanded) { handleSectionExpanded(1, expanded); }),
          ff(state,
             parameterIdProvider,
             ffSection,
             accent,
             [this] { resized(); },
             [this] { refreshChangedAppearance(); },
             [this] (bool expanded) { handleSectionExpanded(2, expanded); }),
          global(state,
                 parameterIdProvider,
                 globalSection,
                 accent,
                 [this] { resized(); },
                 [this] { refreshChangedAppearance(); },
                 [this] (bool expanded) { handleSectionExpanded(3, expanded); },
                 {},
                 [this] (const char* suffix)
                 {
                     if (juce::String(suffix) == "delTa" && onSoloClick)
                         onSoloClick();
                 },
                 [this] (const char* suffix)
                 {
                     return juce::String(suffix) == "delTa" && isSoloActive != nullptr && isSoloActive();
                 },
                 [this] (const char* suffix)
                 {
                     return juce::String(suffix) == "delTa" && (isSoloEnabled == nullptr || isSoloEnabled());
                 })
    {
        addAndMakeVisible(halfWave);
        addAndMakeVisible(dm);
        addAndMakeVisible(ff);
        addAndMakeVisible(global);
        refreshChangedAppearance();
        callbacksEnabled = true;
    }

    bool isChangedFromDefault() const noexcept
    {
        return changedState;
    }

    void refreshExternalState()
    {
        global.refreshExternalState();
    }

    int getPreferredHeight() const
    {
        return 8
            + halfWave.getPreferredHeight()
            + sectionRowGap
            + dm.getPreferredHeight()
            + sectionRowGap
            + ff.getPreferredHeight()
            + sectionRowGap
            + global.getPreferredHeight();
    }

    void paint(juce::Graphics& graphics) override
    {
        juce::ignoreUnused(graphics);
    }

    void resized() override
    {
        auto bounds = getLocalBounds().reduced(0, 4);

        auto sectionBounds = bounds.removeFromTop(halfWave.getPreferredHeight());
        halfWave.setBounds(sectionBounds);
        bounds.removeFromTop(sectionRowGap);
        sectionBounds = bounds.removeFromTop(dm.getPreferredHeight());
        dm.setBounds(sectionBounds);
        bounds.removeFromTop(sectionRowGap);
        sectionBounds = bounds.removeFromTop(ff.getPreferredHeight());
        ff.setBounds(sectionBounds);
        bounds.removeFromTop(sectionRowGap);
        sectionBounds = bounds.removeFromTop(global.getPreferredHeight());
        global.setBounds(sectionBounds);
    }

private:
    void handleSectionExpanded(const int sectionIndex, const bool expanded)
    {
        if (! expanded)
            return;

        if (sectionIndex != 0)
            halfWave.setExpanded(false);

        if (sectionIndex != 1)
            dm.setExpanded(false);

        if (sectionIndex != 2)
            ff.setExpanded(false);

        if (sectionIndex != 3)
            global.setExpanded(false);

        resized();
    }

    void refreshChangedAppearance()
    {
        const auto shouldBeChanged = halfWave.isChangedFromDefault()
            || dm.isChangedFromDefault()
            || ff.isChangedFromDefault()
            || global.isChangedFromDefault();

        if (changedState == shouldBeChanged)
            return;

        changedState = shouldBeChanged;
        repaint();

        if (callbacksEnabled && onChangedStateChange)
            onChangedStateChange();
    }

    std::function<void()> onChangedStateChange;
    std::function<void()> onSoloClick;
    std::function<bool()> isSoloActive;
    std::function<bool()> isSoloEnabled;
    bool callbacksEnabled = false;
    bool changedState = false;
    SectionComponent halfWave;
    SectionComponent dm;
    SectionComponent ff;
    SectionComponent global;
};

class FullbandPageComponent final : public juce::Component
{
public:
    FullbandPageComponent(juce::AudioProcessorValueTreeState& state,
                          const std::function<juce::String(const char*)>& parameterIdProvider,
                          const bool autoSoloEnabled,
                          std::function<size_t()> activeSplitCountProviderIn,
                          std::function<void(int)> onActiveSplitCountChangeIn,
                          std::function<void(bool)> onAutoSoloChangedIn)
        : activeSplitCountProvider(std::move(activeSplitCountProviderIn)),
          fullband(state,
                   parameterIdProvider,
                   fullbandSection,
                   uiAccent,
                   [this] { resized(); },
                   [] {},
                   [this] (const bool expanded)
                   {
                       if (expanded)
                           crossover.setExpanded(false);
                   }),
          crossover(state,
                    parameterIdProvider,
                    crossoverSection,
                    uiAccent,
                    [this] { resized(); },
                    [] {},
                    [this] (const bool expanded)
                    {
                        if (expanded)
                            fullband.setExpanded(false);
                    },
                    [&state, parameterIdProvider] (const char* suffix)
                    {
                        const auto suffixString = juce::String(suffix);
                        auto parameterIndex = static_cast<size_t>(0);
                        auto found = false;

                        for (size_t index = 0; index < crossoverControls.size(); ++index)
                        {
                            if (suffixString == crossoverControls[index].suffix)
                            {
                                parameterIndex = index;
                                found = true;
                                break;
                            }
                        }

                        if (! found)
                            return ValueConstraint {};

                        return ValueConstraint { [&state, parameterIdProvider, parameterIndex] (const float value)
                        {
                            auto lowerBound = 20.0f;
                            auto upperBound = 20000.0f;

                            if (parameterIndex > 0)
                            {
                                if (auto* previousParameter = dynamic_cast<juce::RangedAudioParameter*>(
                                        state.getParameter(parameterIdProvider(crossoverControls[parameterIndex - 1].suffix))))
                                {
                                    lowerBound = previousParameter->convertFrom0to1(previousParameter->getValue()) + crossoverMinGapHz;
                                }
                            }

                            if (parameterIndex + 1 < crossoverControls.size())
                            {
                                if (auto* nextParameter = dynamic_cast<juce::RangedAudioParameter*>(
                                        state.getParameter(parameterIdProvider(crossoverControls[parameterIndex + 1].suffix))))
                                {
                                    upperBound = nextParameter->convertFrom0to1(nextParameter->getValue()) - crossoverMinGapHz;
                                }
                            }

                            return juce::jlimit(lowerBound, juce::jmax(lowerBound, upperBound), value);
                        } };
                    },
                    {},
                    {},
                    {},
                    []
                    {
                        return static_cast<size_t>(1);
                    },
                    [this]
                    {
                        return activeSplitCountProvider != nullptr
                            ? juce::jmin(crossoverControls.size(), activeSplitCountProvider())
                            : crossoverControls.size();
                    }),
          autoSoloButton(uiAccent),
          addCrossoverButton(uiAccent),
          removeCrossoverButton(uiAccent),
          onActiveSplitCountChange(std::move(onActiveSplitCountChangeIn)),
          onAutoSoloChanged(std::move(onAutoSoloChangedIn))
    {
        addAndMakeVisible(fullband);
        addAndMakeVisible(crossover);

        autoSoloButton.setButtonText("AUTO-SOLO");
        autoSoloButton.setClickingTogglesState(true);
        autoSoloButton.setToggleState(autoSoloEnabled, juce::dontSendNotification);
        autoSoloButton.onClick = [this]
        {
            if (onAutoSoloChanged)
                onAutoSoloChanged(autoSoloButton.getToggleState());
        };
        addAndMakeVisible(autoSoloButton);

        addCrossoverButton.setButtonText("XOV-ADD");
        addCrossoverButton.setClickingTogglesState(false);
        addCrossoverButton.setTextJustification(juce::Justification::centredLeft);
        addCrossoverButton.onClick = [this]
        {
            if (onActiveSplitCountChange)
                onActiveSplitCountChange(1);
        };
        crossover.addAndMakeVisible(addCrossoverButton);

        removeCrossoverButton.setButtonText("XOV-DEL");
        removeCrossoverButton.setClickingTogglesState(false);
        removeCrossoverButton.onClick = [this]
        {
            if (onActiveSplitCountChange)
                onActiveSplitCountChange(-1);
        };
        crossover.addAndMakeVisible(removeCrossoverButton);
    }

    int getPreferredHeight() const
    {
        return 8
            + fullband.getPreferredHeight()
            + sectionRowGap
            + crossover.getPreferredHeight()
            + sectionRowGap
            + 24;
    }

    void paint(juce::Graphics& graphics) override
    {
        juce::ignoreUnused(graphics);
    }

    void refreshExternalState()
    {
        crossover.refreshExternalState();
    }

    void resized() override
    {
        auto bounds = getLocalBounds().reduced(0, 4);
        fullband.setBounds(bounds.removeFromTop(fullband.getPreferredHeight()));
        bounds.removeFromTop(sectionRowGap);
        crossover.setBounds(bounds.removeFromTop(crossover.getPreferredHeight()));
        auto xoverButtons = crossover.getExtraControlBounds(0, {});
        addCrossoverButton.setVisible(! xoverButtons.isEmpty());
        removeCrossoverButton.setVisible(! xoverButtons.isEmpty());

        if (! xoverButtons.isEmpty())
        {
            addCrossoverButton.setBounds(xoverButtons.removeFromLeft(parameterNameWidth));
            xoverButtons.removeFromLeft(parameterGap);
            removeCrossoverButton.setBounds(xoverButtons);
        }

        bounds.removeFromTop(sectionRowGap);
        autoSoloButton.setBounds(bounds.removeFromTop(24).reduced(sectionContentInsetX, 0));
    }

private:
    std::function<size_t()> activeSplitCountProvider;
    SectionComponent fullband;
    SectionComponent crossover;
    BoxTextButton autoSoloButton;
    BoxTextButton addCrossoverButton;
    BoxTextButton removeCrossoverButton;
    std::function<void(int)> onActiveSplitCountChange;
    std::function<void(bool)> onAutoSoloChanged;
};
} // namespace

MxeAudioProcessorEditor::MxeAudioProcessorEditor(MxeAudioProcessor& processorToEdit)
    : juce::AudioProcessorEditor(&processorToEdit)
{
    auto& valueTreeState = processorToEdit.getValueTreeState();
    activeSplitCountParameter = dynamic_cast<juce::RangedAudioParameter*>(valueTreeState.getParameter(makeActiveSplitCountParameterId()));
    jassert(activeSplitCountParameter != nullptr);

    for (size_t bandIndex = 0; bandIndex < numBands; ++bandIndex)
    {
        auto* parameter = dynamic_cast<juce::RangedAudioParameter*>(valueTreeState.getParameter(makeSoloParameterId(bandIndex)));
        jassert(parameter != nullptr);
        soloParameters[bandIndex] = parameter;

        auto button = std::make_unique<BoxTextButton>(uiAccent);
        button->setButtonText(makeBandName(bandIndex));
        button->setClickingTogglesState(false);
        button->onClick = [this, bandIndex] { selectBand(bandIndex); };
        addAndMakeVisible(*button);
        monitorButtons[bandIndex] = std::move(button);

        auto page = std::make_unique<BandPageComponent>(
            valueTreeState,
            [bandIndex] (const char* suffix)
            {
                return makeBandParameterId(bandIndex, suffix);
            },
            bandColour(bandIndex),
            [this]
            {
                updatePageChangedIndicators();
            },
            [this, bandIndex]
            {
                toggleManualSolo(bandIndex);
            },
            [this, bandIndex]
            {
                return manualSoloMask[bandIndex];
            },
            [this]
            {
                return ! autoSoloEnabled;
            });
        addAndMakeVisible(*page);
        bandPages[bandIndex] = std::move(page);
    }

    size_t activeSoloCount = 0;

    for (size_t bandIndex = 0; bandIndex < numBands; ++bandIndex)
    {
        if (soloParameters[bandIndex] != nullptr && soloParameters[bandIndex]->getValue() >= 0.5f)
        {
            if (activeSoloCount == 0)
                visibleBandIndex = bandIndex;

            ++activeSoloCount;
        }
    }

    allBandsActive = activeSoloCount != 1;

    auto allButton = std::make_unique<BoxTextButton>(uiAccent);
    allButton->setButtonText("A");
    allButton->setClickingTogglesState(false);
    allButton->onClick = [this] { setAllBandsMonitoring(); };
    addAndMakeVisible(*allButton);
    monitorButtons[numBands] = std::move(allButton);

    allBandsPage = std::make_unique<FullbandPageComponent>(
        valueTreeState,
        [] (const char* suffix)
        {
            return makeFullbandParameterId(suffix);
        },
        autoSoloEnabled,
        [this]
        {
            return getActiveSplitCount();
        },
        [this] (const int delta)
        {
            changeActiveSplitCount(delta);
        },
        [this] (const bool shouldBeEnabled)
        {
            setAutoSoloEnabled(shouldBeEnabled);
        });
    addAndMakeVisible(*allBandsPage);

    footerLabel.setText("MXE by MIXOLVE", juce::dontSendNotification);
    footerLabel.setJustificationType(juce::Justification::centred);
    footerLabel.setColour(juce::Label::textColourId, uiWhite);
    footerLabel.setFont(makeUiFont());
    addAndMakeVisible(footerLabel);

    updatePageChangedIndicators();
    updateMonitorButtons();
    updateBandPageVisibility();

    setResizable(false, false);
    setResizeLimits(fixedEditorWidth, fixedEditorHeight, fixedEditorWidth, fixedEditorHeight);
    setSize(fixedEditorWidth, fixedEditorHeight);
}

MxeAudioProcessorEditor::~MxeAudioProcessorEditor() = default;

void MxeAudioProcessorEditor::paint(juce::Graphics& graphics)
{
    graphics.fillAll(uiGrey800);
}

void MxeAudioProcessorEditor::resized()
{
    auto bounds = getLocalBounds();
    bounds.removeFromLeft(editorInsetX);
    bounds.removeFromRight(editorInsetX);
    bounds.removeFromBottom(editorInsetBottom);
    auto monitorRow = bounds.removeFromTop(22).reduced(sectionContentInsetX, 0);
    const auto buttonCount = static_cast<int>(monitorButtons.size());
    const auto totalGapWidth = monitorButtonGap * (buttonCount - 1);
    const auto baseButtonWidth = (monitorRow.getWidth() - totalGapWidth) / buttonCount;
    auto remainder = (monitorRow.getWidth() - totalGapWidth) - (baseButtonWidth * buttonCount);

    for (auto& button : monitorButtons)
    {
        const auto buttonWidth = baseButtonWidth + (remainder > 0 ? 1 : 0);

        if (button != nullptr)
            button->setBounds(monitorRow.removeFromLeft(buttonWidth).translated(0, monitorRowOffsetY));
        else
            monitorRow.removeFromLeft(buttonWidth);

        monitorRow.removeFromLeft(monitorButtonGap);
        remainder = juce::jmax(0, remainder - 1);
    }

    bounds.removeFromTop(4);

    auto footerBounds = bounds.removeFromBottom(footerHeight);
    footerLabel.setBounds(footerBounds.translated(0, -3));
    bounds.removeFromBottom(4);

    for (size_t bandIndex = 0; bandIndex < numBands; ++bandIndex)
    {
        if (auto* page = bandPages[bandIndex].get())
            page->setBounds(bounds);
    }

    if (auto* page = allBandsPage.get())
        page->setBounds(bounds);

}

void MxeAudioProcessorEditor::selectBand(const size_t bandIndex)
{
    visibleBandIndex = juce::jmin(bandIndex, getActiveBandCount() - 1);
    allBandsActive = false;

    if (autoSoloEnabled)
        manualSoloMask = {};

    updateMonitorButtons();
    updateBandPageVisibility();
    syncMonitorParameters();
    resized();
}

void MxeAudioProcessorEditor::toggleManualSolo(const size_t bandIndex)
{
    if (autoSoloEnabled || bandIndex >= getActiveBandCount())
        return;

    visibleBandIndex = juce::jmin(bandIndex, numBands - 1);
    allBandsActive = false;
    manualSoloMask[visibleBandIndex] = ! manualSoloMask[visibleBandIndex];
    updateMonitorButtons();
    updateBandPageVisibility();
    syncMonitorParameters();
}

void MxeAudioProcessorEditor::changeActiveSplitCount(const int delta)
{
    if (activeSplitCountParameter == nullptr)
        return;

    const auto currentValue = static_cast<int>(std::round(activeSplitCountParameter->convertFrom0to1(activeSplitCountParameter->getValue())));
    const auto newValue = juce::jlimit(0, static_cast<int>(numBands - 1), currentValue + delta);
    activeSplitCountParameter->beginChangeGesture();
    activeSplitCountParameter->setValueNotifyingHost(activeSplitCountParameter->convertTo0to1(static_cast<float>(newValue)));
    activeSplitCountParameter->endChangeGesture();

    visibleBandIndex = juce::jmin(visibleBandIndex, getActiveBandCount() - 1);
    manualSoloMask = {};

    if (auto* fullbandPage = dynamic_cast<FullbandPageComponent*>(allBandsPage.get()))
        fullbandPage->refreshExternalState();

    updateMonitorButtons();
    updateBandPageVisibility();
    syncMonitorParameters();
    resized();
}

void MxeAudioProcessorEditor::setAllBandsMonitoring()
{
    allBandsActive = true;
    updateMonitorButtons();
    updateBandPageVisibility();
    syncMonitorParameters();
}

void MxeAudioProcessorEditor::setAutoSoloEnabled(const bool shouldBeEnabled)
{
    autoSoloEnabled = shouldBeEnabled;
    manualSoloMask = {};
    updateMonitorButtons();
    syncMonitorParameters();
}

void MxeAudioProcessorEditor::syncMonitorParameters()
{
    for (size_t bandIndex = 0; bandIndex < numBands; ++bandIndex)
    {
        auto* parameter = soloParameters[bandIndex];

        if (parameter == nullptr)
            continue;

        const auto enabled = bandIndex < getActiveBandCount()
            && ((autoSoloEnabled && ! allBandsActive && bandIndex == visibleBandIndex)
                || (! autoSoloEnabled && manualSoloMask[bandIndex]));
        const auto newValue = parameter->convertTo0to1(enabled ? 1.0f : 0.0f);

        parameter->beginChangeGesture();
        parameter->setValueNotifyingHost(newValue);
        parameter->endChangeGesture();
    }
}

void MxeAudioProcessorEditor::updateMonitorButtons()
{
    const auto activeBandCount = getActiveBandCount();

    for (size_t bandIndex = 0; bandIndex < numBands; ++bandIndex)
    {
        if (auto* button = monitorButtons[bandIndex].get())
        {
            const auto isActiveBand = bandIndex < activeBandCount;
            button->setVisible(true);
            button->setEnabled(isActiveBand);
            button->setAlpha(isActiveBand ? 1.0f : 0.45f);
            button->setToggleState(! allBandsActive && bandIndex == visibleBandIndex, juce::dontSendNotification);
        }
    }

    if (auto* button = monitorButtons[numBands].get())
    {
        button->setVisible(true);
        button->setToggleState(allBandsActive, juce::dontSendNotification);
    }

    for (auto& page : bandPages)
    {
        if (auto* bandPage = dynamic_cast<BandPageComponent*>(page.get()))
            bandPage->refreshExternalState();
    }
}

void MxeAudioProcessorEditor::updateBandPageVisibility()
{
    const auto activeBandCount = getActiveBandCount();

    for (size_t bandIndex = 0; bandIndex < numBands; ++bandIndex)
    {
        if (auto* page = bandPages[bandIndex].get())
            page->setVisible(! allBandsActive && bandIndex < activeBandCount && bandIndex == visibleBandIndex);
    }

    if (auto* page = allBandsPage.get())
        page->setVisible(allBandsActive);
}

size_t MxeAudioProcessorEditor::getActiveSplitCount() const
{
    if (activeSplitCountParameter == nullptr)
        return numBands - 1;

    return static_cast<size_t>(juce::jlimit(0,
                                           static_cast<int>(numBands - 1),
                                           static_cast<int>(std::round(activeSplitCountParameter->convertFrom0to1(activeSplitCountParameter->getValue())))));
}

size_t MxeAudioProcessorEditor::getActiveBandCount() const
{
    return getActiveSplitCount() + 1;
}

void MxeAudioProcessorEditor::updatePageChangedIndicators()
{
}
