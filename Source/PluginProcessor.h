#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include <array>
#include <atomic>
#include <memory>
#include "EngineRunner.h"

class AIStemSplitterProcessor : public juce::AudioProcessor,
                                public juce::ChangeBroadcaster,
                                private EngineRunner::Listener
{
public:
    static constexpr int kNumStems = 6;

    AIStemSplitterProcessor();
    ~AIStemSplitterProcessor() override;

    // ---- AudioProcessor ----
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "AI Stem Splitter by Oliver Tkach"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    // ---- Separacion (hilo de mensajes) ----
    // Devuelve "" si arranco, o un mensaje de error.
    juce::String startSeparation (const juce::File& input, const SeparationOptions&);

    // ---- Estado para la interfaz (hilo de mensajes) ----
    bool hasStems() const;
    juce::StringArray getStemNames() const;
    juce::File getStemFile (int slot) const;
    juce::File getExportFile (int slot) const; // copia con nombre "<cancion> - <stem>.wav"
    juce::String getSourceName() const;
    juce::String getAnalysisText() const;      // "120 BPM - Am - 440 Hz"
    juce::String getDeviceText() const;        // "CUDA (RTX ...)" / "CPU"
    float getLevel (int slot) const { return levels[(size_t) slot].load(); }

    // Opciones persistentes de la interfaz (se guardan en el proyecto).
    juce::String getOption (const juce::String& key, const juce::String& def) const;
    void setOption (const juce::String& key, const juce::String& value);

    juce::AudioProcessorValueTreeState apvts;
    EngineRunner engine;

private:
    struct Stem
    {
        juce::String name;
        juce::File file;
        std::unique_ptr<juce::BufferingAudioReader> reader;
    };
    struct StemSet
    {
        std::vector<std::unique_ptr<Stem>> stems;
        double sampleRate = 44100.0;
        juce::int64 length = 0;
    };

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    static BusesProperties makeBuses();

    void loadStems (const juce::File& dir, const juce::StringArray& names);
    void restoreFromState();
    void applyResult (const juce::var& result, const juce::File& input);
    void readStem (Stem&, juce::int64 start, int count, float* d0, float* d1, juce::int64 length);

    // EngineRunner::Listener
    void engineFinished (EngineRunner::Task, bool ok, const juce::var& result, const juce::String& error) override;

    juce::AudioFormatManager formats;
    juce::TimeSliceThread readAhead { "AI Stem read-ahead" };

    juce::SpinLock setLock;
    std::unique_ptr<StemSet> currentSet; // protegido por setLock

    std::atomic<float>* offsetParam = nullptr;
    std::array<std::atomic<float>*, kNumStems> gainParam {}, muteParam {}, soloParam {};
    std::array<std::atomic<float>, kNumStems> levels {};
    std::array<float, kNumStems> lastGain {};

    juce::AudioBuffer<float> scratch;   // lectura cruda
    juce::AudioBuffer<float> chunk;     // un stem ya remuestreado
    double hostRate = 44100.0;

    juce::File pendingInput; // archivo que se esta separando
    juce::String pendingError;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AIStemSplitterProcessor)
};
