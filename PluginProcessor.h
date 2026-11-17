#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <atomic>
#include "LimiterDsp.h"

//==============================================================================
//  LMix (NINE HANDS | DSP) : limiteur de mastering true peak
//==============================================================================
class LmixProcessor : public juce::AudioProcessor, private juce::Timer
{
public:
    LmixProcessor();
    ~LmixProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override;
    int getCurrentProgram() override { return juce::jmax (0, currentPreset.load()); }
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //------------------------------------------------------------------ paramètres
    juce::AudioProcessorValueTreeState apvts;
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    struct P
    {
        std::atomic<float> *power, *gain, *ceiling, *style, *attack, *release, *autoRel, *lookahead, *linkTrans, *linkRel,
                           *transient, *enhance, *clip, *truePeak, *os, *unity, *delta, *dither, *shaping, *dc, *mb, *xo1, *xo2, *xo3;
    } prm {};
    static int osFactor (int idx) { return 1 << juce::jlimit (0, 5, idx); }
    float paramValue (const char* id) const { return apvts.getRawParameterValue (id)->load(); }
    void setParam (const char* id, float realValue);         // thread de messages uniquement

    //------------------------------------------------------------------ mesures (thread audio -> interface, sans verrou)
    struct HistPoint { float inDb, outDb, grDb; };
    static constexpr int kHistRate = 200;                    // points par seconde
    static constexpr int kFifo = 4096;
    juce::AbstractFifo histFifo { kFifo };
    std::array<HistPoint, kFifo> histData {};
    int pullHistory (HistPoint* dst, int maxN);

    std::atomic<float> inPeak[2], outPeak[2];               // crête depuis la dernière lecture (linéaire)
    std::atomic<float> grNow { 0.0f }, grMax { 0.0f };      // dB, positifs
    std::atomic<float> bandGr[lmix::MultibandStage::kBands];
    lmix::Loudness loud;                                    // momentané / court terme / histogrammes (lus par l'interface)
    std::atomic<float> momentary { -200.0f }, shortTerm { -200.0f }, truePeakMax { 0.0f };
    std::atomic<bool> resetMeters { false };
    std::atomic<double> currentRate { 48000.0 };
    std::atomic<int> latencyNow { 0 };

    //------------------------------------------------------------------ réglages de l'interface (enregistrés avec le projet)
    std::atomic<int> uiSizeIdx { -1 };       // -1 = taille par défaut (60 %)
    std::atomic<int> glowIdx { 2 };          // 25 / 50 / 75 / 100 %
    std::atomic<int> historyIdx { 1 };       // 3 / 6 / 12 s
    std::atomic<int> targetIdx { 0 };        // cible de sonie
    std::atomic<int> currentPreset { 0 };
    std::atomic<bool> presetDirty { false };
    juce::String presetName { "Default" };

    struct Target { const char* name; const char* shortName; float lufs; float tp; };
    static const std::vector<Target>& targets();

    //------------------------------------------------------------------ presets
    struct FactoryPreset { const char* name; std::vector<std::pair<const char*, float>> values; };
    static const std::vector<FactoryPreset>& factoryPresets();
    void loadFactoryPreset (int index);
    juce::ValueTree savePresetTree() const;
    void loadPresetTree (const juce::ValueTree& t, const juce::String& name);

    //------------------------------------------------------------------ MIDI Learn (aucun CC par défaut)
    static constexpr int kMaxParams = 64;
    std::atomic<int> learnParam { -1 };
    std::atomic<int> ccMap[128];
    std::atomic<int> lastLearnedCc { -1 };
    int  paramIndexOf (const juce::String& id) const;
    int  ccForParam (int paramIndex) const;
    void clearMidiFor (int paramIndex);
    std::atomic<int> midiActivity { 0 };

    int latencyFor (int osIdx) const;

private:
    void timerCallback() override;          // file MIDI -> setValueNotifyingHost ; latence (thread de messages)
    std::atomic<float> pendingValue[kMaxParams];
    std::atomic<bool>  pendingSet[kMaxParams];
    std::atomic<int>   ccMin[128], ccMax[128];

    void configure (int osIdx);             // (re)construit la chaîne pour un facteur de suréchantillonnage
    std::unique_ptr<juce::dsp::Oversampling<float>> osObj[6];
    int activeOs = -1;
    double sr = 48000.0;
    int maxBlock = 512, Dbase = 240, fadeIn = 0;

    lmix::MultibandStage mbStage;
    lmix::MainLimiter main;
    lmix::SafetyLimiter safety;
    lmix::Dither dither;
    lmix::Delay refDl[2], dryDl[2];
    juce::AudioBuffer<float> work, ref;
    std::vector<float> grBuf;
    double gainSm = 1.0, ceilSm = 0.891, powerSm = 1.0, unitySm = 0.0, deltaSm = 0.0;
    // filtre DC, ENHANCE
    double dcX[2] {}, dcY[2] {}, enhShort = 0, enhLong = 0, enhGainDb = 0;
    int histCount = 0;
    float histIn = 0, histOut = 0, histGr = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LmixProcessor)
};
