#include "PluginProcessor.h"

PhantomAsymProcessor::PhantomAsymProcessor()
    : AudioProcessor (BusesProperties()
                        .withInput  ("Input",     juce::AudioChannelSet::stereo(), true)
                        .withOutput ("Output",    juce::AudioChannelSet::stereo(), true)
                        .withInput  ("Sidechain", juce::AudioChannelSet::stereo(), false)),
      apvts (*this, nullptr, "PARAMETERS", createLayout())
{
}

juce::AudioProcessorValueTreeState::ParameterLayout PhantomAsymProcessor::createLayout()
{
    using namespace juce;
    std::vector<std::unique_ptr<RangedAudioParameter>> params;

    params.push_back (std::make_unique<AudioParameterFloat> (
        ParameterID { "depth", 1 }, "Depth",
        NormalisableRange<float> (0.0f, 100.0f, 0.1f), 100.0f,
        AudioParameterFloatAttributes().withLabel ("%")));

    params.push_back (std::make_unique<AudioParameterBool> (
        ParameterID { "bypass", 1 }, "Depth Bypass", false));

    params.push_back (std::make_unique<AudioParameterChoice> (
        ParameterID { "mode", 1 }, "Mode",
        StringArray { "Half", "Whole" }, 0));   // 0 = Half (default), 1 = Whole

    params.push_back (std::make_unique<AudioParameterFloat> (
        ParameterID { "polarity", 1 }, "Polarity",
        NormalisableRange<float> (0.0f, 180.0f, 0.1f), 0.0f,
        AudioParameterFloatAttributes()
            .withStringFromValueFunction ([] (float v, int) -> String
            {
                if (v <= 0.05f)   return "Positive";
                if (v >= 179.95f) return "Negative";
                return String (v, 1) + String::charToString ((juce_wchar) 0x00b0);   // grados: "°"
            })
            .withValueFromStringFunction ([] (const String& t) -> float
            {
                const auto s = t.trim().toLowerCase();
                if (s.startsWith ("pos")) return 0.0f;
                if (s.startsWith ("neg")) return 180.0f;
                return jlimit (0.0f, 180.0f, s.getFloatValue());
            })));

    params.push_back (std::make_unique<AudioParameterFloat> (
        ParameterID { "keygain", 1 }, "Key Input",
        NormalisableRange<float> (-24.0f, 36.0f, 0.01f), 0.0f,
        AudioParameterFloatAttributes().withLabel ("dB")));

    params.push_back (std::make_unique<AudioParameterBool> (
        ParameterID { "listen", 1 }, "Key Listen", false));

    params.push_back (std::make_unique<AudioParameterFloat> (
        ParameterID { "output", 1 }, "Output",
        NormalisableRange<float> (-12.0f, 6.0f, 0.01f), 0.0f,
        AudioParameterFloatAttributes().withLabel ("dB")));

    return { params.begin(), params.end() };
}

void PhantomAsymProcessor::prepareToPlay (double sampleRate, int)
{
    currentSampleRate = sampleRate;
    controlState = 0.0f;
    cycleOn = false;
    nextDuck = true;
    wasPositive = false;
    cyclePeak = 0.0f;
    prevPeak = 0.0f;
    quietCount = 0;
    activeHistory.assign ((size_t) (0.08 * sampleRate) + 2, 0);
    histPos = 0;
    periodSamples = (float) (sampleRate / 50.0);
    samplesSinceRise = 0;
    listenState = 0.0f;
    bypassState = apvts.getRawParameterValue ("bypass")->load() > 0.5f ? 0.0f : 1.0f;
}

bool PhantomAsymProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    if (out != layouts.getMainInputChannelSet())
        return false;

    const auto sc = layouts.getChannelSet (true, 1);
    if (! sc.isDisabled() && sc != juce::AudioChannelSet::mono() && sc != juce::AudioChannelSet::stereo())
        return false;

    return true;
}

void PhantomAsymProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    auto mainBuf = getBusBuffer (buffer, true, 0);
    auto scBuf   = getBusBuffer (buffer, true, 1);
    const int numCh      = mainBuf.getNumChannels();
    const int numSamples = buffer.getNumSamples();

    auto* scBus = getBus (true, 1);
    const bool hasSidechain = scBus != nullptr && scBus->isEnabled() && scBuf.getNumChannels() > 0;
    const int  scCh = juce::jmin (scBuf.getNumChannels(), 2);

    const float depth     = apvts.getRawParameterValue ("depth")->load() / 100.0f;
    const int   mode      = (int) apvts.getRawParameterValue ("mode")->load();
    const bool  firstDuck = true;   // el primer cycle de cada golpe siempre se silencia
    const float phaseDeg  = apvts.getRawParameterValue ("polarity")->load();       // 0 = Positive, 180 = Negative
    const float keyGain   = juce::Decibels::decibelsToGain (apvts.getRawParameterValue ("keygain")->load());
    const float listenTarget = apvts.getRawParameterValue ("listen")->load() > 0.5f ? 1.0f : 0.0f;
    const bool  bypassed  = apvts.getRawParameterValue ("bypass")->load() > 0.5f;
    const float outGain   = juce::Decibels::decibelsToGain (apvts.getRawParameterValue ("output")->load());

    // Valores fijos internos
    const float hys          = juce::Decibels::decibelsToGain (-90.0f);   // solo para detectar cycles (practicamente sin threshold)
    const int   resetSamples = (int) (0.06 * currentSampleRate);          // 60 ms sin señal = reinicio
    const float bypassCoef   = std::exp (-1.0f / (0.005f * (float) currentSampleRate));   // 5 ms
    const int   histSize     = (int) activeHistory.size();
    const float smoothCoef   = std::exp (-1.0f / (0.0015f * (float) currentSampleRate)); // 1.5 ms

    for (int s = 0; s < numSamples; ++s)
    {
        // Señal del sidechain (mono) con su signo
        float sc = 0.0f;
        if (hasSidechain)
        {
            for (int c = 0; c < scCh; ++c)
                sc += scBuf.getReadPointer (c)[s];
            sc /= (float) scCh;
        }
        sc *= keyGain;                     // Key Input
        const float subSigned = sc;

        // Peak del cycle en curso
        cyclePeak = juce::jmax (cyclePeak, std::abs (sc));

        // Detector de cycles (cruce ascendente = inicio de un cycle)
        if (! wasPositive && subSigned > hys)
        {
            wasPositive = true;
            cycleOn   = nextDuck;
            nextDuck  = ! nextDuck;
            prevPeak  = cyclePeak;          // peak del cycle que acaba de terminar
            cyclePeak = std::abs (sc);

            // Periodo del sidechain (para el desplazamiento de fase)
            if (samplesSinceRise > (int) (0.002 * currentSampleRate) && samplesSinceRise < resetSamples)
                periodSamples = 0.5f * periodSamples + 0.5f * (float) samplesSinceRise;
            samplesSinceRise = 0;
        }
        else if (wasPositive && subSigned < -hys)
        {
            wasPositive = false;
        }

        ++samplesSinceRise;

        // Sin señal en el sidechain: reinicia, cada golpe arranca igual
        if (std::abs (subSigned) < hys) ++quietCount; else quietCount = 0;
        if (quietCount > resetSamples)
        {
            cycleOn = false;
            nextDuck = firstDuck;
            wasPositive = false;
            cyclePeak = 0.0f;
            prevPeak = 0.0f;
            quietCount = resetSamples;
        }

        // Cantidad de efecto: sigue la amplitud del sidechain.
        // 0 dBFS (1.0) = Depth completo, hacia -inf dB = sin efecto.
        const float amount = juce::jmin (1.0f, juce::jmax (prevPeak, cyclePeak));
        const bool  active = (mode == 1) ? cycleOn : wasPositive;   // 1 = Whole (cycle entero), 0 = Half

        // Polarity: desplaza donde empieza la ventana de silence dentro del waveform.
        // 0 deg = empieza en el cruce ascendente (Positive), 180 deg = medio cycle despues (Negative).
        activeHistory[(size_t) histPos] = active ? 1 : 0;
        const int delay = juce::jlimit (0, histSize - 1, (int) std::lround (phaseDeg / 360.0f * periodSamples));
        const bool delayedActive = activeHistory[(size_t) ((histPos - delay + histSize) % histSize)] != 0;
        histPos = (histPos + 1) % histSize;

        const float target = delayedActive ? amount : 0.0f;

        // Bypass del Depth (con fade de 5 ms para evitar clicks)
        const float bypassTarget = bypassed ? 0.0f : 1.0f;
        bypassState = bypassTarget + bypassCoef * (bypassState - bypassTarget);

        controlState = target + smoothCoef * (controlState - target);

        // Ganancia: solo atenua (maximo 1.0), nunca amplifica
        const float duckGain = 1.0f - depth * bypassState * controlState;
        const float gain = duckGain * outGain;


        listenState = listenTarget + bypassCoef * (listenState - listenTarget);   // fade de 5 ms

        for (int ch = 0; ch < numCh; ++ch)
        {
            float* d = mainBuf.getWritePointer (ch);
            const float key = hasSidechain ? scBuf.getReadPointer (juce::jmin (ch, scCh - 1))[s] * keyGain : 0.0f;
            d[s] = d[s] * gain * (1.0f - listenState) + key * listenState;
        }
    }
}

juce::AudioProcessorEditor* PhantomAsymProcessor::createEditor()
{
    return new juce::GenericAudioProcessorEditor (*this);
}

void PhantomAsymProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void PhantomAsymProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new PhantomAsymProcessor();
}
