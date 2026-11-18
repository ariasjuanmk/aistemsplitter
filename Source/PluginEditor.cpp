#include "PluginEditor.h"
#include "Paths.h"

namespace
{
const juce::Colour kBg      { 0xff14161c };
const juce::Colour kPanel   { 0xff1e212a };
const juce::Colour kText    { 0xffe6e8ee };
const juce::Colour kDim     { 0xff8a90a0 };
const juce::Colour kAccent  { 0xff5aa0f0 };
const juce::Colour kGood    { 0xff6fcf8a };
const juce::Colour kWarn    { 0xffe8b04a };
const juce::Colour kBad     { 0xffef6b6b };

juce::Colour stemColour (const juce::String& name)
{
    if (name.startsWith ("no_")) return juce::Colour::fromRGB (84, 170, 250);
    if (name == "vocals")        return juce::Colour::fromRGB (235, 92, 92);
    if (name == "drums")         return juce::Colour::fromRGB (247, 165, 59);
    if (name == "bass")          return juce::Colour::fromRGB (90, 130, 245);
    if (name == "guitar")        return juce::Colour::fromRGB (82, 201, 122);
    if (name == "piano")         return juce::Colour::fromRGB (178, 129, 241);
    return juce::Colour::fromRGB (145, 145, 145);
}

const char* const kQualityValues[]   = { "fast", "balanced", "high" };
const char* const kQualityNames[]    = { "Rapido", "Balanceado", "Alto" };
const char* const kModeValues[]      = { "off", "vocals", "drums", "bass", "guitar", "piano", "other" };
const char* const kModeNames[]       = { "6 stems", "2 stems: vocals", "2 stems: drums", "2 stems: bass",
                                         "2 stems: guitar", "2 stems: piano", "2 stems: other" };
const char* const kPrecisionValues[] = { "default", "int24", "float32" };
const char* const kPrecisionNames[]  = { "16 bit", "24 bit", "32 bit float" };
const char* const kDeviceValues[]    = { "auto", "cpu", "cuda" };
const char* const kDeviceNames[]     = { "Auto (CUDA, si no CPU)", "Solo CPU", "Forzar CUDA" };

template <size_t N>
void fillCombo (juce::ComboBox& box, const char* const (&names)[N])
{
    for (size_t i = 0; i < N; ++i)
        box.addItem (names[i], (int) i + 1);
}

template <size_t N>
juce::String valueOf (const juce::ComboBox& box, const char* const (&values)[N])
{
    const int idx = box.getSelectedId() - 1;
    return (idx >= 0 && (size_t) idx < N) ? values[idx] : values[0];
}

template <size_t N>
void selectValue (juce::ComboBox& box, const char* const (&values)[N], const juce::String& v)
{
    for (size_t i = 0; i < N; ++i)
        if (v == values[i])
        {
            box.setSelectedId ((int) i + 1, juce::dontSendNotification);
            return;
        }
    box.setSelectedId (1, juce::dontSendNotification);
}

void styleLabel (juce::Label& l, float size, juce::Colour c, bool bold = false)
{
    l.setFont (juce::Font (juce::FontOptions (size, bold ? juce::Font::bold : juce::Font::plain)));
    l.setColour (juce::Label::textColourId, c);
}
} // namespace

// --------------------------------------------------------------------------
// StemDragHandle
// --------------------------------------------------------------------------
void StemDragHandle::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (1.0f);
    g.setColour (isMouseOver() ? kAccent.withAlpha (0.35f) : kPanel.brighter (0.1f));
    g.fillRoundedRectangle (r, 5.0f);
    g.setColour (kText);
    g.setFont (juce::FontOptions (12.0f));
    g.drawText (label, getLocalBounds(), juce::Justification::centred);
}

void StemDragHandle::mouseDrag (const juce::MouseEvent& e)
{
    if (e.getDistanceFromDragStart() < 6 || ! getFiles)
        return;
    auto files = getFiles();
    files.removeEmptyStrings();
    if (files.isEmpty())
        return;
    juce::DragAndDropContainer::performExternalDragDropOfFiles (files, false, this);
}

// --------------------------------------------------------------------------
// StemStrip
// --------------------------------------------------------------------------
StemStrip::StemStrip (AIStemSplitterProcessor& p, int s)
    : processor (p), slot (s),
      muteAtt (p.apvts, "mute" + juce::String (s + 1), mute),
      soloAtt (p.apvts, "solo" + juce::String (s + 1), solo),
      gainAtt (p.apvts, "gain" + juce::String (s + 1), gain)
{
    addAndMakeVisible (mute);
    addAndMakeVisible (solo);
    mute.setTooltip ("Silencia este stem en la salida Mix");
    solo.setTooltip ("Solo este stem en la salida Mix");

    gain.setSliderStyle (juce::Slider::LinearHorizontal);
    gain.setTextBoxStyle (juce::Slider::TextBoxRight, false, 56, 20);
    gain.setTextValueSuffix (" dB");
    gain.setDoubleClickReturnValue (true, 0.0);
    gain.setTooltip ("Ganancia en la salida Mix (las salidas individuales van sin procesar)");
    addAndMakeVisible (gain);

    drag.label = "arrastrar";
    drag.getFiles = [this]
    {
        return juce::StringArray (processor.getExportFile (slot).getFullPathName());
    };
    addAndMakeVisible (drag);
}

void StemStrip::setStem (const juce::String& n, bool isPresent)
{
    name = n;
    present = isPresent;
    mute.setEnabled (present);
    solo.setEnabled (present);
    gain.setEnabled (present);
    drag.setVisible (present);
    repaint();
}

void StemStrip::resized()
{
    auto r = getLocalBounds().reduced (0, 3);
    r.removeFromLeft (118);                       // nombre (se pinta)
    mute.setBounds (r.removeFromLeft (34));
    solo.setBounds (r.removeFromLeft (34));
    drag.setBounds (r.removeFromRight (84));
    r.removeFromRight (6);
    r.removeFromRight (70);                       // medidor (se pinta)
    gain.setBounds (r);
}

void StemStrip::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (0, 2);
    g.setColour (kPanel);
    g.fillRoundedRectangle (r, 6.0f);

    const auto col = present ? stemColour (name) : kDim.withAlpha (0.4f);
    g.setColour (col);
    g.fillRoundedRectangle (r.getX() + 6, r.getY() + 7, 5.0f, r.getHeight() - 14, 2.0f);

    g.setColour (present ? kText : kDim);
    g.setFont (juce::FontOptions (14.0f, juce::Font::bold));
    g.drawText (present ? name.toUpperCase() : juce::String ("-"), 18, 0, 96, getHeight(),
                juce::Justification::centredLeft);

    if (present)
    {
        const auto meter = getLocalBounds().withTrimmedRight (90).removeFromRight (64)
                               .withSizeKeepingCentre (64, 8).toFloat();
        g.setColour (kBg);
        g.fillRoundedRectangle (meter, 3.0f);
        const float lvl = juce::jlimit (0.0f, 1.0f, processor.getLevel (slot));
        g.setColour (col);
        g.fillRoundedRectangle (meter.withWidth (meter.getWidth() * lvl), 3.0f);
    }
}

// --------------------------------------------------------------------------
// Editor
// --------------------------------------------------------------------------
AIStemSplitterEditor::AIStemSplitterEditor (AIStemSplitterProcessor& p)
    : AudioProcessorEditor (&p), processor (p),
      offsetAtt (p.apvts, "offset", offset)
{
    // OJO: setSize() va al final del constructor. Llama a resized() al instante y
    // resized() usa componentes (strips) que todavia no existen aca.
    title.setText ("AI Stem Splitter by Oliver Tkach", juce::dontSendNotification);
    styleLabel (title, 20.0f, kText, true);
    addAndMakeVisible (title);

    styleLabel (engineLabel, 13.0f, kDim);
    addAndMakeVisible (engineLabel);

    styleLabel (dropLabel, 15.0f, kText);
    dropLabel.setJustificationType (juce::Justification::centred);
    dropLabel.setInterceptsMouseClicks (false, false); // el clic llega al editor (abre el selector)
    addAndMakeVisible (dropLabel);

    styleLabel (statusLabel, 13.0f, kDim);
    statusLabel.setJustificationType (juce::Justification::topLeft);
    statusLabel.setMinimumHorizontalScale (1.0f);
    addAndMakeVisible (statusLabel);

    styleLabel (analysisLabel, 13.0f, kAccent);
    analysisLabel.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (analysisLabel);

    fillCombo (quality, kQualityNames);
    fillCombo (mode, kModeNames);
    fillCombo (precision, kPrecisionNames);
    fillCombo (device, kDeviceNames);
    selectValue (quality, kQualityValues, processor.getOption ("quality", "balanced"));
    selectValue (mode, kModeValues, processor.getOption ("mode", "off"));
    selectValue (precision, kPrecisionValues, processor.getOption ("precision", "default"));
    selectValue (device, kDeviceValues, processor.getOption ("device", "auto"));
    quality.onChange   = [this] { processor.setOption ("quality",   valueOf (quality, kQualityValues)); };
    mode.onChange      = [this] { processor.setOption ("mode",      valueOf (mode, kModeValues)); };
    precision.onChange = [this] { processor.setOption ("precision", valueOf (precision, kPrecisionValues)); };
    device.onChange    = [this] { processor.setOption ("device",    valueOf (device, kDeviceValues)); };

    for (auto* c : { &quality, &mode, &precision, &device })
        addAndMakeVisible (c);

    auto setupCaption = [this] (juce::Label& l, const juce::String& text)
    {
        l.setText (text, juce::dontSendNotification);
        styleLabel (l, 12.0f, kDim);
        addAndMakeVisible (l);
    };
    setupCaption (qualityL, "Calidad");
    setupCaption (modeL, "Modo");
    setupCaption (precisionL, "Precision de salida");
    setupCaption (deviceL, "Dispositivo");

    chooseButton.onClick = [this] { chooseFile(); };
    pasteButton.onClick = [this] { pastePath(); };
    pasteButton.setTooltip ("Pega la ruta de un archivo copiada desde el Explorador "
                            "(Shift + clic derecho > Copiar como ruta de acceso)");
    startButton.onClick = [this] { startSplit(); };
    cancelButton.onClick = [this] { processor.engine.cancel(); };
    engineButton.onClick = [this] { showEngineMenu(); };
    for (auto* b : { &chooseButton, &pasteButton, &startButton, &cancelButton, &engineButton })
        addAndMakeVisible (b);
    cancelButton.setVisible (false);
    progressBar.setVisible (false);
    addAndMakeVisible (progressBar);

    for (int i = 0; i < AIStemSplitterProcessor::kNumStems; ++i)
    {
        strips[(size_t) i] = std::make_unique<StemStrip> (processor, i);
        addAndMakeVisible (*strips[(size_t) i]);
    }

    dragAll.label = "arrastrar todos";
    dragAll.getFiles = [this]
    {
        juce::StringArray files;
        for (int i = 0; i < processor.getStemNames().size(); ++i)
            files.add (processor.getExportFile (i).getFullPathName());
        return files;
    };
    addAndMakeVisible (dragAll);

    offset.setSliderStyle (juce::Slider::LinearHorizontal);
    offset.setTextBoxStyle (juce::Slider::TextBoxRight, false, 70, 20);
    offset.setTextValueSuffix (" s");
    offset.setDoubleClickReturnValue (true, 0.0);
    offset.setTooltip ("Desplaza los stems respecto al inicio del proyecto");
    addAndMakeVisible (offset);
    offsetL.setText ("Offset de reproduccion", juce::dontSendNotification);
    styleLabel (offsetL, 12.0f, kDim);
    addAndMakeVisible (offsetL);

    creditLabel.setText (juce::String (juce::CharPointer_UTF8 ("Basado en el ReaScript AI Stem Splitter de Oliver Tkach. "
                                                                  "Motor de IA: Demucs (Meta).")),
                         juce::dontSendNotification);
    styleLabel (creditLabel, 11.0f, kDim);
    creditLabel.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (creditLabel);

    processor.addChangeListener (this);
    processor.engine.addListener (this);

    refreshStrips();
    refreshEngineLabel();
    setInputFile (juce::File());

    if (processor.engine.isBusy())
    {
        if (processor.engine.currentTask() != EngineRunner::Task::probe)
            setBusy (true);
    }
    else if (! processor.engine.hasProbed())
    {
        processor.engine.startProbe(); // recien ahora (no al escanear plugins)
    }

    setSize (680, 664);
    startTimerHz (30);
}

AIStemSplitterEditor::~AIStemSplitterEditor()
{
    stopTimer();
    processor.engine.removeListener (this);
    processor.removeChangeListener (this);
}

// ---- utilidades ----
bool AIStemSplitterEditor::isAudioFile (const juce::File& f)
{
    static const juce::StringArray exts { ".wav", ".mp3", ".flac", ".ogg", ".aif", ".aiff", ".m4a", ".wma", ".aac" };
    return exts.contains (f.getFileExtension().toLowerCase());
}

SeparationOptions AIStemSplitterEditor::currentOptions() const
{
    SeparationOptions o;
    o.quality = valueOf (quality, kQualityValues);
    o.twoStems = valueOf (mode, kModeValues);
    o.precision = valueOf (precision, kPrecisionValues);
    o.device = valueOf (device, kDeviceValues);
    return o;
}

void AIStemSplitterEditor::setInputFile (const juce::File& f)
{
    inputFile = f;
    if (f.existsAsFile())
        dropLabel.setText (f.getFileName(), juce::dontSendNotification);
    else if (processor.hasStems())
        dropLabel.setText ("Stems cargados de: " + processor.getSourceName()
                           + "\nSolta otro archivo de audio para separar de nuevo",
                           juce::dontSendNotification);
    else
        dropLabel.setText ("Solta aca un archivo de audio (WAV, MP3, FLAC...)\no hace clic aqui para elegirlo",
                           juce::dontSendNotification);
    repaint();
}

void AIStemSplitterEditor::chooseFile()
{
    chooser = std::make_unique<juce::FileChooser> ("Elegir audio", juce::File(),
                                                   "*.wav;*.mp3;*.flac;*.ogg;*.aif;*.aiff;*.m4a;*.wma;*.aac");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [safe = juce::Component::SafePointer<AIStemSplitterEditor> (this)] (const juce::FileChooser& fc)
                          {
                              if (safe != nullptr && fc.getResult().existsAsFile())
                                  safe->setInputFile (fc.getResult());
                          });
}

bool AIStemSplitterEditor::isInterestedInFileDrag (const juce::StringArray& files)
{
    // Acepta cualquier archivo; si no es audio, filesDropped avisa en pantalla.
    return files.size() > 0;
}

void AIStemSplitterEditor::fileDragEnter (const juce::StringArray&, int, int)
{
    dragHover = true;
    repaint();
}

void AIStemSplitterEditor::fileDragExit (const juce::StringArray&)
{
    dragHover = false;
    repaint();
}

void AIStemSplitterEditor::filesDropped (const juce::StringArray& files, int, int)
{
    dragHover = false;
    repaint();
    if (busy)
    {
        showStatus ("Espera a que termine el proceso actual.", kWarn);
        return;
    }
    for (const auto& f : files)
        if (isAudioFile (juce::File (f)))
        {
            setInputFile (juce::File (f));
            showStatus ("Archivo cargado. Toca \"Separar\".", kGood);
            return;
        }
    showStatus ("Formato no soportado. Usa WAV, MP3, FLAC, OGG, AIFF, M4A, WMA o AAC.", kBad);
}

void AIStemSplitterEditor::mouseUp (const juce::MouseEvent& e)
{
    if (! busy && e.mouseWasClicked() && dropArea.contains (e.getPosition()))
        chooseFile();
}

void AIStemSplitterEditor::showStatus (const juce::String& text, juce::Colour colour)
{
    statusLabel.setText (text, juce::dontSendNotification);
    statusLabel.setColour (juce::Label::textColourId, colour);
}

void AIStemSplitterEditor::pastePath()
{
    if (busy)
        return;
    const auto text = juce::SystemClipboard::getTextFromClipboard().trim().unquoted().trim();
    const juce::File f (text);
    if (text.isNotEmpty() && f.existsAsFile() && isAudioFile (f))
    {
        setInputFile (f);
        showStatus ("Archivo cargado. Toca \"Separar\".", kGood);
    }
    else
    {
        showStatus ("El portapapeles no tiene la ruta de un archivo de audio. En el Explorador: "
                    "Shift + clic derecho sobre el archivo > Copiar como ruta de acceso.", kWarn);
    }
}

// ---- estado ----
void AIStemSplitterEditor::setBusy (bool b)
{
    busy = b;
    startButton.setEnabled (! b);
    chooseButton.setEnabled (! b);
    pasteButton.setEnabled (! b);
    engineButton.setEnabled (! b);
    for (auto* c : { &quality, &mode, &precision, &device })
        c->setEnabled (! b);
    cancelButton.setVisible (b);
    progressBar.setVisible (b);
    if (b)
        progressValue = 0.0;
    resized();
}

void AIStemSplitterEditor::refreshStrips()
{
    const auto names = processor.getStemNames();
    for (int i = 0; i < AIStemSplitterProcessor::kNumStems; ++i)
        if (strips[(size_t) i] != nullptr)
            strips[(size_t) i]->setStem (i < names.size() ? names[i] : juce::String(), i < names.size());

    const auto analysis = processor.getAnalysisText();
    const auto dev = processor.getDeviceText();
    analysisLabel.setText ((analysis.isNotEmpty() ? analysis : juce::String())
                               + (dev.isNotEmpty() ? "   |   " + dev : juce::String()),
                           juce::dontSendNotification);
    dragAll.setVisible (names.size() > 0);
}

void AIStemSplitterEditor::refreshEngineLabel()
{
    const auto& eng = processor.engine;
    const auto info = eng.getInfo();
    juce::String text;
    juce::Colour col = kDim;

    if (! eng.hasProbed())
    {
        text = "Buscando motor (Python + Demucs)...";
    }
    else if (info.ready() && info.cuda)
    {
        text = "Motor listo - GPU: " + (info.gpuName.isNotEmpty() ? info.gpuName : juce::String ("NVIDIA")) + " (CUDA)";
        col = kGood;
    }
    else if (info.ready() && info.nvidiaPresent)
    {
        text = "Motor listo en CPU. Hay GPU NVIDIA pero PyTorch no tiene CUDA: Motor > Instalar motor.";
        col = kWarn;
    }
    else if (info.ready())
    {
        text = "Motor listo - CPU (no se detecto CUDA)";
        col = kGood;
    }
    else if (info.valid)
    {
        text = "Python " + info.pythonVersion + " encontrado, pero falta Demucs: Motor > Instalar motor.";
        col = kWarn;
    }
    else
    {
        text = "No se encontro Python 3.10+. Instalalo desde python.org (Add to PATH) o usa Motor > Elegir python.exe.";
        col = kBad;
    }
    engineLabel.setText (text, juce::dontSendNotification);
    engineLabel.setColour (juce::Label::textColourId, col);
}

void AIStemSplitterEditor::startSplit()
{
    if (busy)
        return;
    if (! inputFile.existsAsFile())
    {
        statusText = "Primero solta o elegi un archivo de audio.";
        statusIsError = true;
        statusLabel.setText (statusText, juce::dontSendNotification);
        statusLabel.setColour (juce::Label::textColourId, kBad);
        return;
    }

    const auto err = processor.startSeparation (inputFile, currentOptions());
    if (err.isNotEmpty())
    {
        statusLabel.setText (err, juce::dontSendNotification);
        statusLabel.setColour (juce::Label::textColourId, kBad);
        return;
    }

    if (processor.engine.isBusy())
    {
        setBusy (true);
        statusLabel.setText ("Iniciando...", juce::dontSendNotification);
        statusLabel.setColour (juce::Label::textColourId, kDim);
    }
    else
    {
        statusLabel.setText ("Listo (resultado ya procesado, tomado de la cache).", juce::dontSendNotification);
        statusLabel.setColour (juce::Label::textColourId, kGood);
    }
}

void AIStemSplitterEditor::showEngineMenu()
{
    juce::PopupMenu m;
    m.addItem (1, "Detectar de nuevo");
    m.addSeparator();
    m.addItem (2, "Instalar motor (CUDA si hay GPU NVIDIA)");
    m.addItem (3, "Instalar motor solo CPU");
    m.addSeparator();
    m.addItem (4, "Elegir python.exe...");
    m.addItem (5, "Volver a deteccion automatica");
    m.addItem (6, "Abrir carpeta de datos");

    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&engineButton),
                     [safe = juce::Component::SafePointer<AIStemSplitterEditor> (this)] (int r)
    {
        if (safe == nullptr || r == 0)
            return;
        auto& self = *safe;
        auto& eng = self.processor.engine;

        auto confirmInstall = [safe] (bool cpuOnly)
        {
            auto opts = juce::MessageBoxOptions::makeOptionsOkCancel (
                juce::MessageBoxIconType::QuestionIcon, "Instalar motor",
                "Se crea un entorno de Python propio en:\n" + stempaths::venvDir().getFullPathName()
                    + "\n\nInstala PyTorch" + (cpuOnly ? " (CPU)" : " (con CUDA si hay GPU NVIDIA)")
                    + " y Demucs. Son varios GB y puede tardar varios minutos. Seguir?",
                "Instalar", "Cancelar");
            juce::AlertWindow::showAsync (opts, [safe, cpuOnly] (int result)
            {
                if (result != 1 || safe == nullptr)
                    return;
                if (safe->processor.engine.startSetup (cpuOnly))
                {
                    safe->setBusy (true);
                    safe->statusLabel.setText ("Instalando motor...", juce::dontSendNotification);
                    safe->statusLabel.setColour (juce::Label::textColourId, kDim);
                }
            });
        };

        switch (r)
        {
            case 1:
                if (eng.startProbe())
                    self.refreshEngineLabel();
                break;
            case 2: confirmInstall (false); break;
            case 3: confirmInstall (true); break;
            case 4:
                self.chooser = std::make_unique<juce::FileChooser> ("Elegir python.exe", juce::File(), "python*");
                self.chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                                           [safe] (const juce::FileChooser& fc)
                {
                    if (safe == nullptr || ! fc.getResult().existsAsFile())
                        return;
                    stempaths::setCustomPython (fc.getResult());
                    if (safe->processor.engine.startProbe())
                        safe->refreshEngineLabel();
                });
                break;
            case 5:
                stempaths::setCustomPython (juce::File());
                if (eng.startProbe())
                    self.refreshEngineLabel();
                break;
            case 6:
                stempaths::dataRoot().createDirectory();
                stempaths::dataRoot().revealToUser();
                break;
            default: break;
        }
    });
}

// ---- callbacks del motor ----
void AIStemSplitterEditor::engineStatus (const juce::String& s)
{
    statusLabel.setText (s, juce::dontSendNotification);
    statusLabel.setColour (juce::Label::textColourId, kDim);
}

void AIStemSplitterEditor::engineProgress (float p)
{
    progressValue = (double) p;
}

void AIStemSplitterEditor::engineProbed (const EngineInfo&)
{
    refreshEngineLabel();
    if (! busy)
        statusLabel.setText ({}, juce::dontSendNotification);
}

void AIStemSplitterEditor::engineFinished (EngineRunner::Task task, bool ok, const juce::var&,
                                           const juce::String& error)
{
    setBusy (false);
    refreshEngineLabel();

    if (ok)
    {
        statusLabel.setText (task == EngineRunner::Task::setup ? "Motor instalado. Ya podes separar."
                                                               : "Listo. Reproduci el proyecto o arrastra los stems al DAW.",
                             juce::dontSendNotification);
        statusLabel.setColour (juce::Label::textColourId, kGood);
    }
    else
    {
        statusLabel.setText (error, juce::dontSendNotification);
        statusLabel.setColour (juce::Label::textColourId, kBad);
    }
}

void AIStemSplitterEditor::changeListenerCallback (juce::ChangeBroadcaster*)
{
    refreshStrips();
    setInputFile (inputFile);
}

void AIStemSplitterEditor::timerCallback()
{
    for (auto& s : strips)
        if (s != nullptr)
            s->repaint();
}

// ---- dibujo y layout ----
void AIStemSplitterEditor::paint (juce::Graphics& g)
{
    g.fillAll (kBg);

    g.setColour (kPanel);
    const auto zone = dropArea.toFloat();
    g.fillRoundedRectangle (zone, 10.0f);
    if (dragHover)
    {
        g.setColour (kAccent.withAlpha (0.25f));
        g.fillRoundedRectangle (zone, 10.0f);
    }
    g.setColour (kAccent.withAlpha (dragHover ? 1.0f : 0.5f));
    g.drawRoundedRectangle (zone.reduced (1.0f), 10.0f, dragHover ? 2.5f : 1.5f);
}

void AIStemSplitterEditor::resized()
{
    auto r = getLocalBounds().reduced (16);

    auto top = r.removeFromTop (34);
    engineButton.setBounds (top.removeFromRight (100).reduced (0, 4));
    title.setBounds (top);

    engineLabel.setBounds (r.removeFromTop (22));
    r.removeFromTop (6);

    auto drop = r.removeFromTop (80);
    dropArea = drop;
    auto btns = drop.removeFromRight (150).reduced (8, 6);
    chooseButton.setBounds (btns.removeFromTop (30));
    btns.removeFromTop (4);
    pasteButton.setBounds (btns.removeFromTop (30));
    dropLabel.setBounds (drop);
    r.removeFromTop (10);

    // opciones
    auto opts = r.removeFromTop (48);
    const int w = opts.getWidth() / 4;
    auto place = [&] (juce::Label& l, juce::ComboBox& c)
    {
        auto cell = opts.removeFromLeft (w).reduced (3, 0);
        l.setBounds (cell.removeFromTop (18));
        c.setBounds (cell.removeFromTop (26));
    };
    place (qualityL, quality);
    place (modeL, mode);
    place (precisionL, precision);
    place (deviceL, device);
    r.removeFromTop (8);

    // acciones
    auto actions = r.removeFromTop (34);
    startButton.setBounds (actions.removeFromLeft (130));
    actions.removeFromLeft (8);
    if (busy)
    {
        cancelButton.setBounds (actions.removeFromLeft (100));
        actions.removeFromLeft (10);
        progressBar.setBounds (actions.reduced (0, 6));
    }
    r.removeFromTop (6);
    statusLabel.setBounds (r.removeFromTop (58));
    r.removeFromTop (4);

    // stems
    auto head = r.removeFromTop (22);
    dragAll.setBounds (head.removeFromRight (130).reduced (0, 1));
    analysisLabel.setBounds (head);
    for (auto& s : strips)
    {
        auto row = r.removeFromTop (38);
        if (s != nullptr)
            s->setBounds (row);
    }
    r.removeFromTop (8);

    auto off = r.removeFromTop (26);
    offsetL.setBounds (off.removeFromLeft (150));
    offset.setBounds (off);
    r.removeFromTop (6);
    creditLabel.setBounds (r.removeFromTop (18));
}
