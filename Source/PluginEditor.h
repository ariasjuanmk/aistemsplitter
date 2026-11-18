#pragma once

#include "PluginProcessor.h"

// Asa para arrastrar un stem (WAV) desde el plugin hasta la linea de tiempo del DAW.
class StemDragHandle : public juce::Component
{
public:
    std::function<juce::StringArray()> getFiles;
    juce::String label = "arrastrar";

    void paint (juce::Graphics& g) override;
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseEnter (const juce::MouseEvent&) override { setMouseCursor (juce::MouseCursor::DraggingHandCursor); }
};

class StemStrip : public juce::Component
{
public:
    StemStrip (AIStemSplitterProcessor&, int slot);
    void setStem (const juce::String& name, bool present);
    void resized() override;
    void paint (juce::Graphics&) override;

private:
    AIStemSplitterProcessor& processor;
    int slot;
    juce::String name;
    bool present = false;

    juce::ToggleButton mute { "M" }, solo { "S" };
    juce::Slider gain;
    StemDragHandle drag;
    juce::AudioProcessorValueTreeState::ButtonAttachment muteAtt, soloAtt;
    juce::AudioProcessorValueTreeState::SliderAttachment gainAtt;
};

class AIStemSplitterEditor : public juce::AudioProcessorEditor,
                             public juce::FileDragAndDropTarget,
                             private juce::Timer,
                             private juce::ChangeListener,
                             private EngineRunner::Listener
{
public:
    explicit AIStemSplitterEditor (AIStemSplitterProcessor&);
    ~AIStemSplitterEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int x, int y) override;
    void fileDragEnter (const juce::StringArray& files, int x, int y) override;
    void fileDragExit (const juce::StringArray& files) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    void timerCallback() override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;

    void engineStatus (const juce::String&) override;
    void engineProgress (float) override;
    void engineProbed (const EngineInfo&) override;
    void engineFinished (EngineRunner::Task, bool, const juce::var&, const juce::String&) override;

    void setInputFile (const juce::File&);
    void pastePath();
    void showStatus (const juce::String& text, juce::Colour colour);
    void chooseFile();
    void startSplit();
    void showEngineMenu();
    void refreshEngineLabel();
    void refreshStrips();
    void setBusy (bool busy);
    SeparationOptions currentOptions() const;
    static bool isAudioFile (const juce::File&);

    AIStemSplitterProcessor& processor;

    juce::File inputFile;
    bool busy = false;
    bool dragHover = false;
    juce::Rectangle<int> dropArea;
    double progressValue = 0.0;
    juce::String statusText;
    bool statusIsError = false;

    juce::Label title, engineLabel, statusLabel, dropLabel, analysisLabel, creditLabel;
    juce::TextButton chooseButton { "Elegir archivo..." }, pasteButton { "Pegar ruta" },
                     startButton { "Separar" },
                     cancelButton { "Cancelar" }, engineButton { "Motor  v" };
    juce::ComboBox quality, mode, precision, device;
    juce::Label qualityL, modeL, precisionL, deviceL;
    juce::ProgressBar progressBar { progressValue };

    std::array<std::unique_ptr<StemStrip>, AIStemSplitterProcessor::kNumStems> strips;
    StemDragHandle dragAll;
    juce::Slider offset;
    juce::Label offsetL;
    juce::AudioProcessorValueTreeState::SliderAttachment offsetAtt;

    std::unique_ptr<juce::FileChooser> chooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AIStemSplitterEditor)
};
