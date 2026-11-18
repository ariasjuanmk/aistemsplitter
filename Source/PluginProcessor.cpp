#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Paths.h"

namespace
{
constexpr int kChunk = 2048;       // muestras del host procesadas por pasada
constexpr int kScratchCap = 16384; // muestras crudas maximas por pasada

const char* const kBusNames[AIStemSplitterProcessor::kNumStems] = {
    "1 Vocals", "2 Drums", "3 Bass", "4 Guitar", "5 Piano", "6 Other"
};
} // namespace

juce::AudioProcessor::BusesProperties AIStemSplitterProcessor::makeBuses()
{
    auto b = BusesProperties().withOutput ("Mix", juce::AudioChannelSet::stereo(), true);
    for (int i = 0; i < kNumStems; ++i)
        b = b.withOutput (kBusNames[i], juce::AudioChannelSet::stereo(), false);
    return b;
}

juce::AudioProcessorValueTreeState::ParameterLayout AIStemSplitterProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "offset", 1 }, "Offset (s)",
        NormalisableRange<float> (-600.0f, 600.0f, 0.001f), 0.0f));

    for (int i = 0; i < kNumStems; ++i)
    {
        const auto n = String (i + 1);
        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { "gain" + n, 1 }, "Stem " + n + " gain",
            NormalisableRange<float> (-60.0f, 12.0f, 0.1f), 0.0f));
        layout.add (std::make_unique<AudioParameterBool> (ParameterID { "mute" + n, 1 }, "Stem " + n + " mute", false));
        layout.add (std::make_unique<AudioParameterBool> (ParameterID { "solo" + n, 1 }, "Stem " + n + " solo", false));
    }
    return layout;
}

AIStemSplitterProcessor::AIStemSplitterProcessor()
    : AudioProcessor (makeBuses()),
      apvts (*this, nullptr, "AIStemSplitter", createLayout())
{
    formats.registerBasicFormats();

    offsetParam = apvts.getRawParameterValue ("offset");
    for (int i = 0; i < kNumStems; ++i)
    {
        const auto n = juce::String (i + 1);
        gainParam[(size_t) i] = apvts.getRawParameterValue ("gain" + n);
        muteParam[(size_t) i] = apvts.getRawParameterValue ("mute" + n);
        soloParam[(size_t) i] = apvts.getRawParameterValue ("solo" + n);
        levels[(size_t) i] = 0.0f;
        lastGain[(size_t) i] = 1.0f;
    }

    scratch.setSize (2, kScratchCap);
    chunk.setSize (2, kChunk);

    engine.addListener (this);
}

AIStemSplitterProcessor::~AIStemSplitterProcessor()
{
    engine.removeListener (this);
    engine.cancel();
    {
        const juce::SpinLock::ScopedLockType lock (setLock);
        currentSet.reset(); // los readers se desregistran del hilo antes de pararlo
    }
    readAhead.stopThread (2000);
}

bool AIStemSplitterProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    for (const auto& bus : layouts.outputBuses)
        if (! bus.isDisabled() && bus != juce::AudioChannelSet::stereo())
            return false;
    return ! layouts.getMainOutputChannelSet().isDisabled();
}

void AIStemSplitterProcessor::prepareToPlay (double sampleRate, int)
{
    hostRate = sampleRate;
    for (auto& g : lastGain)
        g = 1.0f;
}

// --------------------------------------------------------------------------
// Audio
// --------------------------------------------------------------------------
void AIStemSplitterProcessor::readStem (Stem& s, juce::int64 start, int count,
                                        float* d0, float* d1, juce::int64 length)
{
    juce::FloatVectorOperations::clear (d0, count);
    juce::FloatVectorOperations::clear (d1, count);

    const juce::int64 from = juce::jmax<juce::int64> (0, start);
    const juce::int64 to = juce::jmin<juce::int64> (length, start + count);
    if (to <= from)
        return;

    const int destOffset = (int) (from - start);
    const int num = (int) (to - from);
    float* dest[2] = { d0 + destOffset, d1 + destOffset };
    s.reader->read (dest, 2, from, num); // sin bloquear: si no esta en buffer queda silencio
}

void AIStemSplitterProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples();
    buffer.clear();

    // posicion del host
    bool playing = false;
    juce::int64 hostPos = 0;
    if (auto* ph = getPlayHead())
    {
        if (auto pos = ph->getPosition())
        {
            playing = pos->getIsPlaying();
            if (auto t = pos->getTimeInSamples())
                hostPos = *t;
            else if (auto sec = pos->getTimeInSeconds())
                hostPos = (juce::int64) std::llround (*sec * hostRate);
        }
    }

    if (! playing)
    {
        for (auto& l : levels)
            l.store (l.load() * 0.8f);
        return;
    }

    const juce::SpinLock::ScopedTryLockType lock (setLock);
    if (! lock.isLocked() || currentSet == nullptr)
        return;

    auto& set = *currentSet;
    const double stemRate = set.sampleRate;
    const double ratio = stemRate / hostRate;
    const bool sameRate = std::abs (ratio - 1.0) < 1.0e-9;
    const double offsetSec = (double) offsetParam->load();

    bool anySolo = false;
    for (auto* s : soloParam)
        anySolo = anySolo || s->load() > 0.5f;

    auto mix = getBusBuffer (buffer, false, 0);
    const int numStems = (int) juce::jmin<size_t> (set.stems.size(), (size_t) kNumStems);

    // chunk maximo para no pasar de kScratchCap muestras crudas
    const int maxChunk = juce::jlimit (1, kChunk, (int) ((kScratchCap - 16) / juce::jmax (1.0, ratio)));

    for (int start = 0; start < numSamples; start += maxChunk)
    {
        const int n = juce::jmin (maxChunk, numSamples - start);
        const juce::int64 blockPos = hostPos + start;

        for (int i = 0; i < numStems; ++i)
        {
            auto& stem = *set.stems[(size_t) i];
            float* c0 = chunk.getWritePointer (0);
            float* c1 = chunk.getWritePointer (1);

            if (sameRate)
            {
                const juce::int64 src = blockPos - (juce::int64) std::llround (offsetSec * hostRate);
                readStem (stem, src, n, c0, c1, set.length);
            }
            else
            {
                // interpolacion lineal sobre la posicion absoluta (continua entre bloques)
                const double srcPos = ((double) blockPos / hostRate - offsetSec) * stemRate;
                const auto base = (juce::int64) std::floor (srcPos);
                const double frac = srcPos - (double) base;
                const int need = juce::jmin (kScratchCap, (int) std::ceil ((double) n * ratio + frac) + 2);
                float* s0 = scratch.getWritePointer (0);
                float* s1 = scratch.getWritePointer (1);
                readStem (stem, base, need, s0, s1, set.length);
                for (int k = 0; k < n; ++k)
                {
                    const double p = frac + (double) k * ratio;
                    const int i0 = (int) p;
                    const float t = (float) (p - (double) i0);
                    c0[k] = s0[i0] + t * (s0[i0 + 1] - s0[i0]);
                    c1[k] = s1[i0] + t * (s1[i0 + 1] - s1[i0]);
                }
            }

            // nivel para el medidor
            const float peak = juce::jmax (chunk.getMagnitude (0, 0, n), chunk.getMagnitude (1, 0, n));
            levels[(size_t) i].store (juce::jmax (peak, levels[(size_t) i].load() * 0.9f));

            // salida individual (cruda, sin mute/solo/ganancia)
            auto& bus = *getBus (false, i + 1);
            if (bus.isEnabled())
            {
                auto out = getBusBuffer (buffer, false, i + 1);
                for (int ch = 0; ch < juce::jmin (2, out.getNumChannels()); ++ch)
                    out.copyFrom (ch, start, chunk, ch, 0, n);
            }

            // mezcla: mute / solo / ganancia
            const bool audible = muteParam[(size_t) i]->load() < 0.5f
                                 && (! anySolo || soloParam[(size_t) i]->load() > 0.5f);
            const float target = audible ? juce::Decibels::decibelsToGain (gainParam[(size_t) i]->load(), -60.0f)
                                         : 0.0f;
            for (int ch = 0; ch < juce::jmin (2, mix.getNumChannels()); ++ch)
                mix.addFromWithRamp (ch, start, chunk.getReadPointer (ch), n, lastGain[(size_t) i], target);
            lastGain[(size_t) i] = target;
        }
    }
}

// --------------------------------------------------------------------------
// Carga de stems
// --------------------------------------------------------------------------
void AIStemSplitterProcessor::loadStems (const juce::File& dir, const juce::StringArray& names)
{
    auto set = std::make_unique<StemSet>();
    for (const auto& name : names)
    {
        const auto file = dir.getChildFile (name + ".wav");
        if (! file.existsAsFile())
            continue;
        std::unique_ptr<juce::AudioFormatReader> raw (formats.createReaderFor (file));
        if (raw == nullptr || raw->numChannels < 1)
            continue;

        set->sampleRate = raw->sampleRate;
        set->length = juce::jmax (set->length, raw->lengthInSamples);

        auto stem = std::make_unique<Stem>();
        stem->name = name;
        stem->file = file;
        stem->reader = std::make_unique<juce::BufferingAudioReader> (raw.release(), readAhead, 1 << 17);
        stem->reader->setReadTimeout (0);
        set->stems.push_back (std::move (stem));
    }

    if (! readAhead.isThreadRunning())
        readAhead.startThread (juce::Thread::Priority::normal);

    std::unique_ptr<StemSet> old;
    {
        const juce::SpinLock::ScopedLockType lock (setLock);
        old = std::move (currentSet);
        currentSet = std::move (set);
    }
    old.reset(); // se libera fuera del lock
}

bool AIStemSplitterProcessor::hasStems() const
{
    return apvts.state.getProperty ("stemNames").toString().isNotEmpty();
}

juce::StringArray AIStemSplitterProcessor::getStemNames() const
{
    juce::StringArray out;
    out.addTokens (apvts.state.getProperty ("stemNames").toString(), "|", "");
    out.removeEmptyStrings();
    return out;
}

juce::File AIStemSplitterProcessor::getStemFile (int slot) const
{
    auto names = getStemNames();
    if (slot < 0 || slot >= names.size())
        return {};
    return juce::File (apvts.state.getProperty ("stemsDir").toString()).getChildFile (names[slot] + ".wav");
}

juce::File AIStemSplitterProcessor::getExportFile (int slot) const
{
    const auto src = getStemFile (slot);
    if (! src.existsAsFile())
        return {};

    const auto exportDir = src.getParentDirectory().getParentDirectory().getChildFile ("export");
    exportDir.createDirectory();
    const auto song = juce::File::createLegalFileName (getSourceName());
    const auto dst = exportDir.getChildFile (song + " - " + src.getFileNameWithoutExtension() + ".wav");
    if (! dst.existsAsFile() || dst.getSize() != src.getSize())
        src.copyFileTo (dst);
    return dst;
}

juce::String AIStemSplitterProcessor::getSourceName() const
{
    return apvts.state.getProperty ("sourceName").toString();
}

juce::String AIStemSplitterProcessor::getAnalysisText() const
{
    const int bpm = (int) apvts.state.getProperty ("bpm", 0);
    const auto key = apvts.state.getProperty ("key").toString();
    const int hz = (int) apvts.state.getProperty ("hz", 0);
    juce::StringArray parts;
    if (bpm > 0) parts.add (juce::String (bpm) + " BPM");
    if (key.isNotEmpty()) parts.add (key);
    if (hz > 0) parts.add (juce::String (hz) + " Hz");
    return parts.joinIntoString (" - ");
}

juce::String AIStemSplitterProcessor::getDeviceText() const
{
    const auto dev = apvts.state.getProperty ("device").toString();
    const auto gpu = apvts.state.getProperty ("gpu").toString();
    if (dev == "cuda")
        return "CUDA" + (gpu.isNotEmpty() ? " (" + gpu + ")" : juce::String());
    return dev.isNotEmpty() ? dev.toUpperCase() : juce::String();
}

juce::String AIStemSplitterProcessor::getOption (const juce::String& key, const juce::String& def) const
{
    return apvts.state.getProperty ("opt_" + key, def).toString();
}

void AIStemSplitterProcessor::setOption (const juce::String& key, const juce::String& value)
{
    apvts.state.setProperty ("opt_" + key, value, nullptr);
}

// --------------------------------------------------------------------------
// Separacion
// --------------------------------------------------------------------------
juce::String AIStemSplitterProcessor::startSeparation (const juce::File& input, const SeparationOptions& opts)
{
    if (! input.existsAsFile())
        return "El archivo no existe.";
    if (engine.isBusy())
        return "El motor esta ocupado.";
    if (! engine.getInfo().ready())
        return "El motor no esta listo. Usa el menu \"Motor\" para instalarlo o elegir Python.";

    // la carpeta de cache depende del archivo y de las opciones: si ya existe, no se repite el trabajo
    const auto hash = juce::String::toHexString (
        (input.getFullPathName() + "|" + juce::String (input.getSize()) + "|"
         + juce::String (input.getLastModificationTime().toMilliseconds()) + "|" + opts.toKey()).hashCode64());
    auto name = juce::File::createLegalFileName (input.getFileNameWithoutExtension()).substring (0, 40);
    const auto outDir = stempaths::cacheDir().getChildFile (name + "_" + hash);

    pendingInput = input;

    const auto cached = outDir.getChildFile ("result.json");
    if (cached.existsAsFile())
    {
        const auto result = juce::JSON::parse (cached.loadFileAsString());
        if (result.isObject() && juce::File (result["stems_dir"].toString()).isDirectory())
        {
            applyResult (result, input);
            return {};
        }
    }

    if (! engine.startSeparate (input, outDir, opts))
        return "No se pudo iniciar el motor.";
    return {};
}

void AIStemSplitterProcessor::applyResult (const juce::var& result, const juce::File& input)
{
    const juce::File dir (result["stems_dir"].toString());
    juce::StringArray names;
    if (auto* arr = result["stems"].getArray())
        for (const auto& n : *arr)
            names.add (n.toString());

    auto& st = apvts.state;
    st.setProperty ("stemsDir", dir.getFullPathName(), nullptr);
    st.setProperty ("stemNames", names.joinIntoString ("|"), nullptr);
    st.setProperty ("sourceName", input.getFileNameWithoutExtension(), nullptr);
    st.setProperty ("bpm", (int) result["bpm"], nullptr);
    st.setProperty ("key", result["key"].toString(), nullptr);
    st.setProperty ("hz", (int) result["hz"], nullptr);
    st.setProperty ("device", result["device"].toString(), nullptr);
    st.setProperty ("gpu", result["gpu"].toString(), nullptr);

    loadStems (dir, names);
    sendChangeMessage();
}

void AIStemSplitterProcessor::engineFinished (EngineRunner::Task task, bool ok, const juce::var& result,
                                              const juce::String&)
{
    if (task == EngineRunner::Task::separate && ok)
        applyResult (result, pendingInput);
}

// --------------------------------------------------------------------------
// Estado del proyecto
// --------------------------------------------------------------------------
void AIStemSplitterProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, dest);
}

void AIStemSplitterProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
        {
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
            restoreFromState();
        }
}

void AIStemSplitterProcessor::restoreFromState()
{
    const juce::File dir (apvts.state.getProperty ("stemsDir").toString());
    const auto names = getStemNames();
    if (dir.isDirectory() && names.size() > 0)
        loadStems (dir, names);
    sendChangeMessage();
}

juce::AudioProcessorEditor* AIStemSplitterProcessor::createEditor()
{
    return new AIStemSplitterEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new AIStemSplitterProcessor();
}
