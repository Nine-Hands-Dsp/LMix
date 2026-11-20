#pragma once
#include <juce_gui_extra/juce_gui_extra.h>
#include "PluginProcessor.h"
#include "LmixMenu.h"
#include "WindowMinSize.h"
#include "NhRackKnob.h"

class LmixEditor;

namespace LmixUi
{
    using namespace juce;
    constexpr int kW = 960, kH = 540;            // taille par défaut, annoncée « 60 % »
    constexpr int kMaxW = 2400, kMaxH = 1350;    // 150 % (100 % = 1600 x 900)

    // thème Graphite (famille QMix), accent rouge
    namespace col
    {
        const Colour bg (0xff0c0f13), panelTop (0xff12161c), panelBottom (0xff0d1014), panelBorder (0xff1f2630), screen (0xff06080b),
                     screenBorder (0xff1f2630), grid (0xff141a21), label (0xff5d6675), text (0xffe9eef4), dim (0xff8d96a4),
                     accent (0xffff4b4e), accentHi (0xffff7375), face (0xff050608), pillBorder (0xff2a313b), segBg (0xff0a0d10),
                     segBorder (0xff2a313b), track (0xff1d232c), marker (0xfff4f7fb), capLight (0xff3d444e), capDark (0xff15191e),
                     crownLight (0xff5b636e), crownDark (0xff14181d), green (0xff3fbf9f), amber (0xffe8c45a);
    }

    //------------------------------------------------------------------ potard vectoriel (lumière en haut à gauche, 45°)
    // couronne crantée, chapeau, marqueur et ombre tournent ensemble ; flou de mouvement de la couronne selon la vitesse
    class Knob : public Component
    {
    public:
        Knob (LmixEditor& e, String labelText);
        ~Knob() override;
        void attach (RangedAudioParameter* p, bool bipolar);
        void paint (Graphics&) override;
        void mouseDown (const MouseEvent&) override;
        void mouseDrag (const MouseEvent&) override;
        void mouseUp (const MouseEvent&) override;
        void mouseDoubleClick (const MouseEvent&) override;
        void mouseWheelMove (const MouseEvent&, const MouseWheelDetails&) override;
        void tick();                              // 30 Hz : flou de mouvement
        Point<float> centre() const;
        float ringRadius() const;
        String valueText() const;
        Rectangle<float> labelBox() const;
        float pillFont() const;
        float pillAlpha() const;
        void touchPill();
        nhrack::PillOverlay::Pill pill() const;
        float ringScale = 1.0f;   // < 1 : potards serrés, laisse de la place à la pastille
        Colour colour { col::accent };
        RangedAudioParameter* param = nullptr;
        String label;
        bool bipolar = false, enabledLook = true;
        float valueFont = 7.8f;
        std::function<String (float)> format;
        std::function<void()> extraMenu;          // menu clic droit propre (sinon MIDI)
    private:
        LmixEditor& ed;
        std::unique_ptr<ParameterAttachment> att;
        float norm = 0.0f, dragStart = 0.0f, shownAngle = 0.0f, prevAngle = 0.0f, blur = 0.0f, blurDir = 1.0f;
        Image face;               // face d'alu anodisé précalculée (timer)
        int facePx = 0;
        double lastActive = -100.0;   // dernière action (clic, molette, valeur qui bouge)
        float lastNorm = -1.0f;
        bool dragging = false;
        float angleOf (float v) const;
    };

    //------------------------------------------------------------------ écran : historique défilant + bandes du mode multibande
    class Display : public Component
    {
    public:
        explicit Display (LmixEditor& e);
        void paint (Graphics&) override;
        void mouseDown (const MouseEvent&) override;
        void mouseDrag (const MouseEvent&) override;
        void mouseUp (const MouseEvent&) override;
        void mouseMove (const MouseEvent&) override;
        void tick();                               // 30 Hz
        static constexpr int kRing = LmixProcessor::kHistRate * 12 + 8;
    private:
        LmixEditor& ed;
        std::vector<LmixProcessor::HistPoint> ring = std::vector<LmixProcessor::HistPoint> ((size_t) kRing, { -120.0f, -120.0f, 0.0f });
        std::vector<LmixProcessor::HistPoint> tmp = std::vector<LmixProcessor::HistPoint> ((size_t) LmixProcessor::kFifo);
        int ringPos = 0;
        bool frozen = false;
        int dragXo = -1, hoverXo = -1;
        Rectangle<float> chip (int i) const;
        Rectangle<float> histArea() const;
        Rectangle<float> bandArea() const;
        float yOf (float db) const;
        float xOfHz (double hz) const;
        double hzAtX (float x) const;
        void paintHistory (Graphics&);
        void paintBands (Graphics&);
    };

    //------------------------------------------------------------------ vumètres, sonie, réduction de gain
    class Meters : public Component
    {
    public:
        explicit Meters (LmixEditor& e);
        void paint (Graphics&) override;
        void mouseDown (const MouseEvent&) override;
        void tick();                               // 30 Hz
        Rectangle<float> targetRect() const;
        Rectangle<float> loudRect() const;
        Rectangle<float> grRect() const;
        double integrated = -200, lra = 0;
    private:
        LmixEditor& ed;
        float lvIn[2] {}, lvOut[2] {}, hold[4] {}; double holdT[4] {};
        float shownGr = 0;
        std::vector<float> grHist = std::vector<float> (180, 0.0f);   // 6 s à 30 Hz
        int grPos = 0;
        uint32_t histI[lmix::Loudness::kBins] {}, histS[lmix::Loudness::kBins] {};
        int slow = 0;
    };
}

//==============================================================================
class LmixEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit LmixEditor (LmixProcessor&);
    ~LmixEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void parentHierarchyChanged() override;
    void visibilityChanged() override;

    LmixProcessor& proc;
    LmixMenu::Fonts fonts;
    LmixMenu::MenuLnf menuLnf;
    juce::Typeface::Ptr mono;
    juce::Font font (int weight, float h) const;   // 0 regular, 1 medium, 2 bold, 3 chiffres
    float sc() const { return (float) getWidth() / (float) LmixUi::kW; }
    float glow() const { return 0.25f * (float) (juce::jlimit (0, 3, proc.glowIdx.load()) + 1); }

    void showStatus (const juce::String& s);
    void showMainMenu();
    void showParamMidiMenu (juce::RangedAudioParameter* p, const juce::String& title);
    void showReleaseMenu();
    void showOsMenu();
    void showDitherMenu();
    void showTargetMenu();
    void showStyleMenu();
    juce::PopupMenu::Options menuAt() const;
    void askRepaint (juce::Rectangle<int> r);
    bool toggleParam (const char* id);

    LmixUi::Display display;
    LmixUi::Meters meters;
    nhrack::PillOverlay pills;                        // pastilles de valeur, au-dessus de tout (déclaré avant les potards)
    LmixUi::Knob gainKnob, ceilKnob, attackKnob, releaseKnob, lookKnob, linkTKnob, linkRKnob, transKnob, enhKnob, clipKnob;

private:
    friend int main (int, char**);                       // outil de captures (tools/render)
    void timerCallback() override;
    void setScaleIndex (int idx);
    void useSoftwareRenderer();
    void trackHostWindow();
    void prevNextPreset (int dir);
    void savePreset();
    void loadPreset();

    std::unique_ptr<juce::Drawable> logo;
    wiga::WindowMinSize hostLock;
    void* lastPeer = nullptr;
    double lastRendererCheck = 0, lastPaint = 0, pendingSince = 0;
    std::unique_ptr<juce::FileChooser> chooser;

    // barre de presets / ligne d'état (même pastille)
    juce::String status;
    juce::String lastStatusDrawn;      // texte affiché au dernier dessin (redessin quand il change)
    double statusTime = -10.0;
    float statusAlpha = 0.0f;
    juce::Rectangle<float> pill() const, pillPrev() const, pillNext() const, powerRect() const;
    juce::Rectangle<float> styleChip (int i) const, outChip (int i) const, autoChip() const;
    int hoverZone = 0;
    std::vector<float> lastVals;
    float powerGlow = 1.0f;
    int lastGlow = -1, lastLatency = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LmixEditor)
};
