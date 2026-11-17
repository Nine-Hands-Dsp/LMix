#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace juce;

//==============================================================================
static NormalisableRange<float> logRange (float lo, float hi, float step = 0.0f)
{
    NormalisableRange<float> r (lo, hi,
        [] (float a, float b, float v) { return a * std::pow (b / a, v); },
        [] (float a, float b, float x) { return std::log (x / a) / std::log (b / a); });
    if (step > 0) r.interval = step;
    return r;
}

static String hzText (float v, int)
{
    if (v >= 1000.0f) return String (v / 1000.0f, v >= 10000.0f ? 1 : 2) + " kHz";
    return String ((int) std::round (v)) + " Hz";
}

AudioProcessorValueTreeState::ParameterLayout LmixProcessor::createLayout()
{
    AudioProcessorValueTreeState::ParameterLayout l;
    const auto dB = AudioParameterFloatAttributes().withLabel ("dB");
    const auto pc = AudioParameterFloatAttributes().withLabel ("%");
    const auto ms = AudioParameterFloatAttributes().withLabel ("ms");
    StringArray styles; for (int s = 0; s < lmix::numStyles; ++s) styles.add (lmix::styleName (s));

    l.add (std::make_unique<AudioParameterBool>   (ParameterID { "power", 1 }, "Power", true));
    l.add (std::make_unique<AudioParameterFloat>  (ParameterID { "gain", 1 }, "Gain", NormalisableRange<float> (-12.0f, 30.0f, 0.01f), 0.0f, dB));
    l.add (std::make_unique<AudioParameterFloat>  (ParameterID { "ceiling", 1 }, "Ceiling", NormalisableRange<float> (-30.0f, 0.0f, 0.01f, 2.2f), -1.0f, dB));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "style", 1 }, "Style", styles, (int) lmix::modern));
    l.add (std::make_unique<AudioParameterFloat>  (ParameterID { "attack", 1 }, "Attack", NormalisableRange<float> (0.0f, 100.0f, 0.1f), 50.0f, pc));
    l.add (std::make_unique<AudioParameterFloat>  (ParameterID { "release", 1 }, "Release", logRange (1.0f, 1000.0f, 0.1f), 100.0f, ms));
    l.add (std::make_unique<AudioParameterBool>   (ParameterID { "autoRel", 1 }, "Auto Release", true));
    l.add (std::make_unique<AudioParameterFloat>  (ParameterID { "lookahead", 1 }, "Lookahead", NormalisableRange<float> (0.0f, 5.0f, 0.01f), 2.0f, ms));
    l.add (std::make_unique<AudioParameterFloat>  (ParameterID { "linkTrans", 1 }, "Link Transients", NormalisableRange<float> (0.0f, 100.0f, 0.1f), 100.0f, pc));
    l.add (std::make_unique<AudioParameterFloat>  (ParameterID { "linkRel", 1 }, "Link Release", NormalisableRange<float> (0.0f, 100.0f, 0.1f), 50.0f, pc));
    l.add (std::make_unique<AudioParameterFloat>  (ParameterID { "transient", 1 }, "Transient", NormalisableRange<float> (0.0f, 100.0f, 0.1f), 0.0f, pc));
    l.add (std::make_unique<AudioParameterFloat>  (ParameterID { "enhance", 1 }, "Enhance", NormalisableRange<float> (0.0f, 100.0f, 0.1f), 0.0f, pc));
    l.add (std::make_unique<AudioParameterFloat>  (ParameterID { "clip", 1 }, "Clip", NormalisableRange<float> (0.0f, 100.0f, 0.1f), 0.0f, pc));
    l.add (std::make_unique<AudioParameterBool>   (ParameterID { "truePeak", 1 }, "True Peak", true));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "os", 1 }, "Oversampling", StringArray { "1x", "2x", "4x", "8x", "16x", "32x" }, 2));
    l.add (std::make_unique<AudioParameterBool>   (ParameterID { "unity", 1 }, "Unity Gain", false));
    l.add (std::make_unique<AudioParameterBool>   (ParameterID { "delta", 1 }, "Delta", false));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "dither", 1 }, "Dither", StringArray { "Off", "16 bit", "24 bit" }, 0));
    l.add (std::make_unique<AudioParameterBool>   (ParameterID { "shaping", 1 }, "Noise Shaping", true));
    l.add (std::make_unique<AudioParameterBool>   (ParameterID { "dc", 1 }, "DC Filter", true));
    l.add (std::make_unique<AudioParameterBool>   (ParameterID { "mb", 1 }, "Multiband", false));
    l.add (std::make_unique<AudioParameterFloat>  (ParameterID { "xo1", 1 }, "Crossover Low", logRange (40.0f, 600.0f), 120.0f,
                                                   AudioParameterFloatAttributes().withStringFromValueFunction (hzText)));
    l.add (std::make_unique<AudioParameterFloat>  (ParameterID { "xo2", 1 }, "Crossover Mid", logRange (300.0f, 4000.0f), 1000.0f,
                                                   AudioParameterFloatAttributes().withStringFromValueFunction (hzText)));
    l.add (std::make_unique<AudioParameterFloat>  (ParameterID { "xo3", 1 }, "Crossover High", logRange (2000.0f, 16000.0f), 6000.0f,
                                                   AudioParameterFloatAttributes().withStringFromValueFunction (hzText)));
    return l;
}

//==============================================================================
LmixProcessor::LmixProcessor()
    : AudioProcessor (BusesProperties().withInput ("Input", AudioChannelSet::stereo(), true)
                                       .withOutput ("Output", AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "LMIX", createLayout())
{
    auto g = [this] (const char* id) { return apvts.getRawParameterValue (id); };
    prm = { g ("power"), g ("gain"), g ("ceiling"), g ("style"), g ("attack"), g ("release"), g ("autoRel"), g ("lookahead"), g ("linkTrans"), g ("linkRel"),
            g ("transient"), g ("enhance"), g ("clip"), g ("truePeak"), g ("os"), g ("unity"), g ("delta"), g ("dither"), g ("shaping"), g ("dc"), g ("mb"),
            g ("xo1"), g ("xo2"), g ("xo3") };

    for (int i = 0; i < 6; ++i)
        osObj[i] = std::make_unique<dsp::Oversampling<float>> (2, (size_t) i, dsp::Oversampling<float>::filterHalfBandFIREquiripple, true, true);
    for (auto& v : inPeak) v = 0.0f;
    for (auto& v : outPeak) v = 0.0f;
    for (auto& v : bandGr) v = 0.0f;
    for (int i = 0; i < 128; ++i) { ccMap[i] = -1; ccMin[i] = 127; ccMax[i] = 0; }
    for (int i = 0; i < kMaxParams; ++i) { pendingValue[i] = 0.0f; pendingSet[i] = false; }
    jassert (getParameters().size() <= kMaxParams);
    startTimerHz (60);
}

LmixProcessor::~LmixProcessor() { stopTimer(); }

bool LmixProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto in = l.getMainInputChannelSet(), out = l.getMainOutputChannelSet();
    if (in != out) return false;
    return out == AudioChannelSet::mono() || out == AudioChannelSet::stereo();
}

int LmixProcessor::latencyFor (int osIdx) const
{
    const int osLat = (int) std::lround (osObj[jlimit (0, 5, osIdx)]->getLatencyInSamples());
    return (int) std::lround (0.002 * sr) + osLat + Dbase + 2 * lmix::SafetyLimiter::W + jmax (2, (int) std::lround (0.001 * sr)) - 1;
}

void LmixProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    sr = sampleRate;
    currentRate = sampleRate;
    maxBlock = jmax (64, samplesPerBlock);
    for (auto& o : osObj) { o->initProcessing ((size_t) maxBlock); o->reset(); }
    Dbase = (int) std::lround (0.005 * sr);                                  // avance maximale : 5 ms
    mbStage.prepare (sr, (int) std::lround (0.002 * sr));
    mbStage.setCrossovers (prm.xo1->load(), prm.xo2->load(), prm.xo3->load());
    safety.prepare (sr);
    main.prepare (sr * 32.0, 32, Dbase * 32);                                // réserve la mémoire pour 32x (aucune allocation ensuite)
    int maxLat = 0;
    for (int i = 0; i < 6; ++i) maxLat = jmax (maxLat, latencyFor (i));
    for (auto& d : refDl) d.prepare (maxLat + 8);
    for (auto& d : dryDl) d.prepare (maxLat + 8);
    work.setSize (2, maxBlock);
    ref.setSize (2, maxBlock);
    grBuf.assign ((size_t) maxBlock, 0.0f);
    loud.prepare (sr);
    dither.reset();
    gainSm = Decibels::decibelsToGain ((double) prm.gain->load());
    powerSm = prm.power->load() > 0.5f ? 1.0 : 0.0;
    unitySm = prm.unity->load() > 0.5f ? 1.0 : 0.0;
    deltaSm = prm.delta->load() > 0.5f ? 1.0 : 0.0;
    for (int c = 0; c < 2; ++c) { dcX[c] = dcY[c] = 0; }
    enhShort = enhLong = 1e-9; enhGainDb = 0;
    histCount = 0;
    activeOs = -1;
    configure ((int) prm.os->load());
    setLatencySamples (latencyNow.load());
}

void LmixProcessor::configure (int osIdx)
{
    activeOs = jlimit (0, 5, osIdx);
    const int f = osFactor (activeOs);
    osObj[activeOs]->reset();
    main.prepare (sr * f, f, Dbase * f);                                     // capacité déjà réservée : pas d'allocation
    safety.reset();
    mbStage.reset();
    const int osLat = (int) std::lround (osObj[activeOs]->getLatencyInSamples());
    const int lat = latencyFor (activeOs);
    for (auto& d : refDl) { d.clear(); d.setDelay (osLat + Dbase + safety.delay()); }
    for (auto& d : dryDl) { d.clear(); d.setDelay (lat); }
    latencyNow = lat;
    jassert (lat == mbStage.latency() + osLat + Dbase + safety.delay());
}

//==============================================================================
int LmixProcessor::pullHistory (HistPoint* dst, int maxN)
{
    int s1, n1, s2, n2;
    histFifo.prepareToRead (jmin (maxN, histFifo.getNumReady()), s1, n1, s2, n2);
    for (int i = 0; i < n1; ++i) dst[i] = histData[(size_t) (s1 + i)];
    for (int i = 0; i < n2; ++i) dst[n1 + i] = histData[(size_t) (s2 + i)];
    histFifo.finishedRead (n1 + n2);
    return n1 + n2;
}

void LmixProcessor::processBlock (AudioBuffer<float>& buffer, MidiBuffer& midi)
{
    ScopedNoDenormals noDenormals;
    const int numCh = jmin (2, buffer.getNumChannels());
    const int n = buffer.getNumSamples();
    for (int c = numCh; c < buffer.getNumChannels(); ++c) buffer.clear (c, 0, n);
    if (n == 0 || numCh == 0) return;
    if (n > maxBlock)                                           // bloc plus grand qu'annoncé : découpé, jamais d'allocation
    {
        for (int s0 = 0; s0 < n; s0 += maxBlock)
        {
            AudioBuffer<float> part (buffer.getArrayOfWritePointers(), buffer.getNumChannels(), s0, jmin (maxBlock, n - s0));
            processBlock (part, midi);
        }
        return;
    }

    //-------------------------------------------------------------- MIDI : Learn + CC appris (file vers le thread de messages)
    for (const auto meta : midi)
    {
        const auto m = meta.getMessage();
        if (! m.isController()) continue;
        const int cc = m.getControllerNumber(), v = m.getControllerValue();
        const int lp = learnParam.load();
        if (lp >= 0)
        {
            for (auto& x : ccMap) if (x.load() == lp) x = -1;
            ccMap[cc] = lp; learnParam = -1; lastLearnedCc = cc;
            ccMin[cc] = v; ccMax[cc] = v;
        }
        const int pi = ccMap[cc].load();
        if (pi < 0 || pi >= kMaxParams) continue;
        if (v < ccMin[cc].load()) ccMin[cc] = v;
        if (v > ccMax[cc].load()) ccMax[cc] = v;
        const int lo = ccMin[cc].load(), hi = ccMax[cc].load();
        float x = (float) v / 127.0f;
        if (hi - lo >= 64) x = jlimit (0.0f, 1.0f, ((float) (v - lo) - 1.0f) / jmax (1.0f, (float) (hi - lo) - 2.0f));
        pendingValue[pi] = x;
        pendingSet[pi] = true;
        midiActivity.fetch_add (1);
    }
    midi.clear();

    //-------------------------------------------------------------- réglages du bloc
    const int osIdx = jlimit (0, 5, (int) prm.os->load());
    if (osIdx != activeOs) { configure (osIdx); fadeIn = (int) (0.03 * sr); }
    if (resetMeters.exchange (false)) { loud.reset(); grMax = 0.0f; truePeakMax = 0.0f; }
    mbStage.setCrossovers (prm.xo1->load(), prm.xo2->load(), prm.xo3->load());

    const double ceilLin = Decibels::decibelsToGain ((double) prm.ceiling->load());
    lmix::MainParams mp;
    mp.ceilingLin = ceilLin;
    mp.style = jlimit (0, lmix::numStyles - 1, (int) prm.style->load());
    mp.attack = prm.attack->load() * 0.01;
    mp.releaseMs = prm.release->load();
    mp.autoRelease = prm.autoRel->load() > 0.5f;
    mp.lookaheadMs = prm.lookahead->load();
    mp.linkTrans = prm.linkTrans->load() * 0.01;
    mp.linkRel = prm.linkRel->load() * 0.01;
    mp.transient = prm.transient->load() * 0.01;
    mp.clip = prm.clip->load() * 0.01;
    const bool tp = prm.truePeak->load() > 0.5f, dcOn = prm.dc->load() > 0.5f, mbOn = prm.mb->load() > 0.5f;
    const double enhance = prm.enhance->load() * 0.01;
    const double gainT = Decibels::decibelsToGain ((double) prm.gain->load());
    const double aSm = 1.0 - std::exp (-1.0 / (0.02 * sr));
    const double dcR = 1.0 - 2.0 * lmix::kPi * 5.0 / sr;
    const double eS = 1.0 - std::exp (-1.0 / (0.05 * sr)), eL = 1.0 - std::exp (-1.0 / (3.0 * sr));
    const double eUp = 1.0 - std::exp (-1.0 / (0.15 * sr)), eDn = 1.0 - std::exp (-1.0 / (0.03 * sr));

    const float* inL = buffer.getReadPointer (0);
    const float* inR = numCh > 1 ? buffer.getReadPointer (1) : inL;
    float* wL = work.getWritePointer (0); float* wR = work.getWritePointer (1);
    float* rL = ref.getWritePointer (0);  float* rR = ref.getWritePointer (1);
    float inPk[2] = { 0, 0 };

    //-------------------------------------------------------------- 1. taux de base : GAIN, filtre DC, ENHANCE, multibande
    for (int i = 0; i < n; ++i)
    {
        gainSm += (gainT - gainSm) * aSm;
        double x[2] = { inL[i] * gainSm, inR[i] * gainSm };
        if (dcOn)
            for (int c = 0; c < 2; ++c) { const double y = x[c] - dcX[c] + dcR * dcY[c]; dcX[c] = x[c]; dcY[c] = y; x[c] = y; }
        // ENHANCE : remonte les passages faibles par rapport au niveau à long terme (compression vers le haut)
        {
            const double p = 0.5 * (x[0] * x[0] + x[1] * x[1]);
            enhShort += (p - enhShort) * eS; enhLong += (p - enhLong) * eL;
            double target = 0.0;
            if (enhance > 0.0005 && enhShort > 1e-6)                    // jamais sur le silence (-60 dBFS)
                target = enhance * jlimit (0.0, 6.0, (10.0 * std::log10 ((enhLong + 1e-12) / (enhShort + 1e-12)) - 2.0) * 0.5);
            enhGainDb += (target - enhGainDb) * (target > enhGainDb ? eUp : eDn);
            if (enhGainDb > 1e-4) { const double g = std::exp (0.11512925 * enhGainDb); x[0] *= g; x[1] *= g; }
        }
        double refv[2] = { x[0], x[1] };
        mbStage.process (x[0], x[1], refv[0], refv[1], ceilLin, mbOn);
        wL[i] = (float) x[0]; wR[i] = (float) x[1];
        rL[i] = (float) refv[0]; rR[i] = (float) refv[1];
        inPk[0] = jmax (inPk[0], std::abs (wL[i])); inPk[1] = jmax (inPk[1], std::abs (wR[i]));
    }
    for (int b = 0; b < lmix::MultibandStage::kBands; ++b) bandGr[b] = (float) mbStage.bandGrDb[b];

    //-------------------------------------------------------------- 2. suréchantillonné : écrêteur + limiteur principal
    float* grBase = grBuf.data();
    const int f = osFactor (activeOs);
    {
        float* chans[2] = { wL, wR };
        dsp::AudioBlock<float> blk (chans, 2, (size_t) n);
        auto up = osObj[activeOs]->processSamplesUp (blk);
        float* uL = up.getChannelPointer (0); float* uR = up.getChannelPointer (1);
        const int nu = (int) up.getNumSamples();
        int k = 0;
        for (int j = 0; j < nu; ++j)
        {
            double a = uL[j], b = uR[j];
            main.process (a, b, mp);
            uL[j] = (float) a; uR[j] = (float) b;
            if ((j + 1) % f == 0 && k < n) grBase[k++] = (float) -lmix::gainToDb (main.takeMinGain());
        }
        osObj[activeOs]->processSamplesDown (blk);
    }

    //-------------------------------------------------------------- 3. taux de base : sécurité true peak, mesures, écoute, dither, bypass
    const double powerT = prm.power->load() > 0.5f ? 1.0 : 0.0, unityT = prm.unity->load() > 0.5f ? 1.0 : 0.0, deltaT = prm.delta->load() > 0.5f ? 1.0 : 0.0;
    const int ditherIdx = (int) prm.dither->load();
    const int bits = ditherIdx == 1 ? 16 : 24;
    const bool shaping = prm.shaping->load() > 0.5f;
    const int hop = jmax (1, (int) std::lround (sr / kHistRate));
    const int fadeLen = jmax (1, (int) (0.03 * sr));
    float outPk[2] = { 0, 0 };
    float grBlockMax = 0;
    float* oL = buffer.getWritePointer (0);
    float* oR = numCh > 1 ? buffer.getWritePointer (1) : nullptr;
    for (int i = 0; i < n; ++i)
    {
        double y[2] = { wL[i], wR[i] };
        safety.process (y[0], y[1], ceilLin, tp);
        const double rf[2] = { refDl[0].push (rL[i]), refDl[1].push (rR[i]) };
        // mesures (sortie limitée, avant écoute / dither)
        if (numCh > 1) loud.process (y[0], y[1]); else loud.process (y[0], 0.0);
        outPk[0] = jmax (outPk[0], (float) std::abs (y[0])); outPk[1] = jmax (outPk[1], (float) std::abs (y[1]));
        const float gr = grBase[i] + (float) safety.grOut;
        grBlockMax = jmax (grBlockMax, gr);
        histIn = jmax (histIn, (float) jmax (std::abs (rf[0]), std::abs (rf[1])));   // avant limitation, aligné sur la sortie
        histOut = jmax (histOut, (float) jmax (std::abs (y[0]), std::abs (y[1])));
        histGr = jmax (histGr, gr);
        if (++histCount >= hop)
        {
            int s1, n1, s2, n2;
            histFifo.prepareToWrite (1, s1, n1, s2, n2);
            if (n1 > 0) histData[(size_t) s1] = { (float) lmix::gainToDb (histIn), (float) lmix::gainToDb (histOut), histGr };
            histFifo.finishedWrite (n1);
            histCount = 0; histIn = histOut = histGr = 0.0f;
        }

        // écoute : DELTA (ce que le limiteur retire) et UNITY (gain compensé)
        unitySm += (unityT - unitySm) * aSm;
        deltaSm += (deltaT - deltaSm) * aSm;
        powerSm += (powerT - powerSm) * aSm;
        const double comp = 1.0 + unitySm * (1.0 / jmax (1.0e-3, gainSm) - 1.0);
        double fade = 1.0;
        if (fadeIn > 0) { fade = 1.0 - (double) fadeIn / fadeLen; --fadeIn; }
        for (int c = 0; c < numCh; ++c)
        {
            double v = y[c] + deltaSm * ((rf[c] - y[c]) - y[c]);
            v *= comp;
            if (ditherIdx > 0 && deltaSm < 0.5) v = dither.process (v, c, bits, shaping);
            const double dryv = dryDl[c].push (c == 0 ? inL[i] : inR[i]);
            const double outv = fade * (dryv + powerSm * (v - dryv));
            (c == 0 ? oL : oR)[i] = (float) outv;
        }
        if (numCh == 1) dryDl[1].push (inL[i]);
    }

    //-------------------------------------------------------------- vers l'interface
    auto maxAtomic = [] (std::atomic<float>& a, float v) { float o = a.load(); while (v > o && ! a.compare_exchange_weak (o, v)) {} };
    maxAtomic (inPeak[0], inPk[0]); maxAtomic (inPeak[1], numCh > 1 ? inPk[1] : inPk[0]);
    maxAtomic (outPeak[0], outPk[0]); maxAtomic (outPeak[1], numCh > 1 ? outPk[1] : outPk[0]);
    grNow = grBlockMax;
    maxAtomic (grMax, grBlockMax);
    momentary = (float) loud.momentary; shortTerm = (float) loud.shortTerm; truePeakMax = (float) loud.tpMax;
}

//==============================================================================
void LmixProcessor::timerCallback()
{
    auto& params = getParameters();
    for (int i = 0; i < params.size() && i < kMaxParams; ++i)
        if (pendingSet[i].exchange (false))
        {
            auto* p = params[i];
            p->beginChangeGesture();
            p->setValueNotifyingHost (pendingValue[i].load());
            p->endChangeGesture();
        }
    if (latencyNow.load() != getLatencySamples()) setLatencySamples (latencyNow.load());
}

void LmixProcessor::setParam (const char* id, float realValue)
{
    if (auto* p = apvts.getParameter (id))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 (realValue));
        p->endChangeGesture();
    }
}

int LmixProcessor::paramIndexOf (const String& id) const
{
    auto& params = getParameters();
    for (int i = 0; i < params.size(); ++i)
        if (auto* wp = dynamic_cast<AudioProcessorParameterWithID*> (params[i]))
            if (wp->paramID == id) return i;
    return -1;
}

int LmixProcessor::ccForParam (int pi) const
{
    for (int c = 0; c < 128; ++c) if (ccMap[c].load() == pi) return c;
    return -1;
}

void LmixProcessor::clearMidiFor (int pi)
{
    for (auto& x : ccMap) if (x.load() == pi) x = -1;
    if (learnParam.load() == pi) learnParam = -1;
}

//==============================================================================
const std::vector<LmixProcessor::Target>& LmixProcessor::targets()
{
    static const std::vector<Target> t = {
        { "Spotify / Tidal  -14 LUFS", "Spotify -14", -14.0f, -1.0f },
        { "Apple Music  -16 LUFS", "Apple -16", -16.0f, -1.0f },
        { "YouTube  -14 LUFS", "YouTube -14", -14.0f, -1.0f },
        { "Amazon Music  -14 LUFS", "Amazon -14", -14.0f, -2.0f },
        { "Deezer  -15 LUFS", "Deezer -15", -15.0f, -1.0f },
        { "SoundCloud  -14 LUFS", "SoundCloud -14", -14.0f, -1.0f },
        { "CD / Club  -9 LUFS", "CD / Club -9", -9.0f, -0.3f },
        { "EBU R128 broadcast  -23 LUFS", "EBU R128 -23", -23.0f, -1.0f },
        { "ATSC A/85  -24 LKFS", "ATSC -24", -24.0f, -2.0f },
    };
    return t;
}

const std::vector<LmixProcessor::FactoryPreset>& LmixProcessor::factoryPresets()
{
    static const std::vector<FactoryPreset> p = {
        { "Default", {} },
        { "Modern Master", { { "style", 6 }, { "gain", 6.0f }, { "release", 120.0f }, { "transient", 30.0f }, { "clip", 15.0f }, { "os", 3 } } },
        { "Transparent Master", { { "style", 0 }, { "gain", 3.0f }, { "release", 250.0f }, { "lookahead", 3.0f } } },
        { "Streaming -14 LUFS", { { "style", 0 }, { "gain", 4.0f }, { "release", 180.0f }, { "lookahead", 3.0f }, { "ceiling", -1.0f } } },
        { "Club Loud", { { "style", 3 }, { "gain", 9.0f }, { "ceiling", -0.3f }, { "release", 60.0f }, { "clip", 30.0f }, { "os", 3 } } },
        { "Metal Master (7/8 strings)", { { "style", 6 }, { "gain", 6.0f }, { "mb", 1 }, { "xo1", 110.0f }, { "xo2", 900.0f }, { "xo3", 5500.0f },
                                          { "transient", 40.0f }, { "release", 90.0f }, { "clip", 10.0f }, { "os", 3 } } },
        { "Bass Control (Multiband)", { { "style", 2 }, { "gain", 4.0f }, { "mb", 1 }, { "xo1", 150.0f } } },
        { "Punchy Drum Bus", { { "style", 1 }, { "gain", 5.0f }, { "transient", 60.0f }, { "lookahead", 1.0f }, { "release", 80.0f }, { "truePeak", 0 }, { "os", 1 } } },
        { "Mix Bus Glue", { { "style", 4 }, { "gain", 3.0f }, { "release", 300.0f }, { "attack", 60.0f }, { "ceiling", -0.5f } } },
        { "Aggressive EDM", { { "style", 7 }, { "gain", 10.0f }, { "clip", 40.0f }, { "release", 40.0f }, { "ceiling", -0.3f }, { "os", 3 } } },
        { "Enhanced Detail", { { "style", 0 }, { "gain", 4.0f }, { "enhance", 40.0f } } },
        { "Broadcast EBU R128", { { "style", 5 }, { "lookahead", 5.0f }, { "release", 300.0f } } },
        { "Safety Limiter", { { "style", 5 }, { "ceiling", -0.3f }, { "os", 1 } } },
    };
    return p;
}

int LmixProcessor::getNumPrograms() { return (int) factoryPresets().size(); }
const String LmixProcessor::getProgramName (int i) { return isPositiveAndBelow (i, (int) factoryPresets().size()) ? String (factoryPresets()[(size_t) i].name) : String(); }
void LmixProcessor::setCurrentProgram (int i) { if (isPositiveAndBelow (i, getNumPrograms())) loadFactoryPreset (i); }

// les presets ne touchent ni à Power, ni à l'écoute (Unity, Delta), ni au dither
static bool isPresetParam (const String& id) { return id != "power" && id != "unity" && id != "delta" && id != "dither" && id != "shaping"; }

void LmixProcessor::loadFactoryPreset (int index)
{
    const auto& fp = factoryPresets()[(size_t) jlimit (0, (int) factoryPresets().size() - 1, index)];
    for (auto* p : getParameters())
        if (auto* wp = dynamic_cast<RangedAudioParameter*> (p))
            if (isPresetParam (wp->paramID))
                wp->setValueNotifyingHost (wp->getDefaultValue());
    for (const auto& kv : fp.values)
        if (auto* p = apvts.getParameter (kv.first))
            p->setValueNotifyingHost (p->convertTo0to1 (kv.second));
    currentPreset = index;
    presetName = fp.name;
    presetDirty = false;
}

ValueTree LmixProcessor::savePresetTree() const
{
    ValueTree t ("LMIXPRESET");
    for (auto* p : getParameters())
        if (auto* wp = dynamic_cast<AudioProcessorParameterWithID*> (p))
            if (isPresetParam (wp->paramID))
                t.setProperty (wp->paramID, p->getValue(), nullptr);
    return t;
}

void LmixProcessor::loadPresetTree (const ValueTree& t, const String& name)
{
    for (auto* p : getParameters())
        if (auto* wp = dynamic_cast<AudioProcessorParameterWithID*> (p))
            if (isPresetParam (wp->paramID) && t.hasProperty (wp->paramID))
                p->setValueNotifyingHost ((float) t[wp->paramID]);
    presetName = name;
    currentPreset = -1;
    presetDirty = false;
}

//==============================================================================
void LmixProcessor::getStateInformation (MemoryBlock& dest)
{
    auto state = apvts.copyState();
    state.setProperty ("uiSize", uiSizeIdx.load(), nullptr);
    state.setProperty ("glow", glowIdx.load(), nullptr);
    state.setProperty ("history", historyIdx.load(), nullptr);
    state.setProperty ("target", targetIdx.load(), nullptr);
    state.setProperty ("preset", currentPreset.load(), nullptr);
    state.setProperty ("presetName", presetName, nullptr);
    state.setProperty ("presetDirty", presetDirty.load(), nullptr);
    String map;
    for (int c = 0; c < 128; ++c)
        if (ccMap[c].load() >= 0)
            if (auto* wp = dynamic_cast<AudioProcessorParameterWithID*> (getParameters()[ccMap[c].load()]))
                map << c << "=" << wp->paramID << ";";
    state.setProperty ("midiMap", map, nullptr);
    if (auto xml = state.createXml()) copyXmlToBinary (*xml, dest);
}

void LmixProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
    {
        auto t = ValueTree::fromXml (*xml);
        if (! t.isValid()) return;
        apvts.replaceState (t);
        uiSizeIdx    = (int) t.getProperty ("uiSize", -1);
        glowIdx      = (int) t.getProperty ("glow", 2);
        historyIdx   = jlimit (0, 2, (int) t.getProperty ("history", 1));
        targetIdx    = jlimit (0, (int) targets().size() - 1, (int) t.getProperty ("target", 0));
        currentPreset = (int) t.getProperty ("preset", 0);
        presetName   = t.getProperty ("presetName", "Default").toString();
        presetDirty  = (bool) t.getProperty ("presetDirty", false);
        for (auto& x : ccMap) x = -1;
        for (auto& e : StringArray::fromTokens (t.getProperty ("midiMap").toString(), ";", ""))
        {
            const int c = e.upToFirstOccurrenceOf ("=", false, false).getIntValue();
            const int pi = paramIndexOf (e.fromFirstOccurrenceOf ("=", false, false));
            if (isPositiveAndBelow (c, 128) && pi >= 0) ccMap[c] = pi;
        }
    }
}

AudioProcessorEditor* LmixProcessor::createEditor() { return new LmixEditor (*this); }
AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new LmixProcessor(); }
