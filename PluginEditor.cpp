#include "PluginEditor.h"
#include "NineHandsDefaultSize.h"
#include "NineHandsStatusText.h"
#include "BinaryData.h"

using namespace juce;
using namespace LmixUi;
using LmixMenu::fnt;

//==============================================================================
namespace
{
    double nowS() { return Time::getMillisecondCounterHiRes() * 0.001; }
    String dbStr (double d) { return (d > 0.0049 ? "+" : "") + String (d, std::abs (d) < 10.0 ? 2 : 1) + " dB"; }
    String hzStr (double f) { return f >= 1000.0 ? String (f / 1000.0, f >= 10000.0 ? 1 : 2) + " kHz" : String ((int) std::round (f)) + " Hz"; }

    // lumière d'ambiance de la façade : source sur la diagonale haut gauche (45°)
    void paintFacade (Graphics& g, Rectangle<float> r)
    {
        g.setColour (col::bg); g.fillRect (r);
        const float d = r.getWidth() + r.getHeight();
        ColourGradient lit (Colours::white.withAlpha (0.045f), r.getX(), r.getY(), Colours::black.withAlpha (0.12f), r.getX() + d * 0.5f, r.getY() + d * 0.5f, false);
        g.setGradientFill (lit); g.fillRect (r);
        const float c = d * 0.36f;
        ColourGradient sat (Colours::transparentWhite, r.getX() + c * 0.5f - d * 0.08f, r.getY() + c * 0.5f - d * 0.08f,
                            Colours::transparentWhite, r.getX() + c * 0.5f + d * 0.08f, r.getY() + c * 0.5f + d * 0.08f, false);
        sat.addColour (0.5, Colours::white.withAlpha (0.03f));
        g.setGradientFill (sat); g.fillRect (r);
    }

    void paintPanel (Graphics& g, Rectangle<float> r, float s, Colour top = col::panelTop, Colour bottom = col::panelBottom)
    {
        g.setColour (Colours::black.withAlpha (0.35f));
        g.fillRoundedRectangle (r.translated (1.5f * s, 2.0f * s), 7.0f * s);
        ColourGradient bg (top, r.getX(), r.getY(), bottom, r.getRight(), r.getBottom(), false);
        g.setGradientFill (bg);
        g.fillRoundedRectangle (r, 7.0f * s);
        g.setColour (col::panelBorder);
        g.drawRoundedRectangle (r.reduced (0.5f), 7.0f * s, 1.0f);
        g.setColour (Colours::white.withAlpha (0.04f));
        g.drawLine (r.getX() + 8 * s, r.getY() + 1.0f, r.getRight() - 8 * s, r.getY() + 1.0f, 1.0f);
    }

    // pastille / bouton segmenté
    void paintChip (Graphics& g, Rectangle<float> r, const String& t, bool on, float s, const Font& f, float glow, bool dimText = false)
    {
        g.setColour (col::segBg);
        g.fillRoundedRectangle (r, 4.0f * s);
        if (on)
        {
            g.setColour (col::accent.withAlpha (0.10f * glow)); g.fillRoundedRectangle (r.expanded (2.5f * s), 6.0f * s);
            g.setColour (Colour (0xff2b1012)); g.fillRoundedRectangle (r, 4.0f * s);
        }
        g.setColour (on ? col::accent : col::segBorder);
        g.drawRoundedRectangle (r.reduced (0.5f), 4.0f * s, 1.0f);
        g.setColour (on ? Colours::white : dimText ? col::dim : Colour (0xffc3cad3));
        g.setFont (f);
        g.drawFittedText (t, r.toNearestInt(), Justification::centred, 1, 0.8f);
    }

    // contour d'un trait pointillé (à remplir)
    Path dashed (const Path& p, float dash, float gap, float thickness)
    {
        Path out; const float d[2] = { dash, gap };
        PathStrokeType (thickness).createDashedStroke (out, p, d, 2);
        return out;
    }
}

//==============================================================================
//  Potard (même construction que QMix : couronne, chapeau, marqueur et ombre solidaires)
//==============================================================================
Knob::Knob (LmixEditor& e, String l) : label (std::move (l)), ed (e) 
{
    ed.pills.add (this, [this] { return pill(); }, [this] { return ed.font (1, pillFont()); });
}

Knob::~Knob() { ed.pills.remove (this); }

void Knob::attach (RangedAudioParameter* p, bool bip)
{
    param = p; bipolar = bip;
    att = std::make_unique<ParameterAttachment> (*p, [this] (float v) { norm = param->convertTo0to1 (v); repaint(); });
    att->sendInitialUpdate();
    shownAngle = prevAngle = angleOf (norm);
}

float Knob::angleOf (float v) const { return MathConstants<float>::pi * (-0.75f + 1.5f * v); }

Point<float> Knob::centre() const
{
    const float s = ed.sc();
    const float d = jmin ((float) getWidth(), (float) getHeight() - 13.0f * s);
    return { getWidth() * 0.5f, d * 0.5f + (1.0f - ringScale) * d * 0.25f };
}

float Knob::ringRadius() const
{
    const float s = ed.sc();
    const float d = jmin ((float) getWidth(), (float) getHeight() - 13.0f * s);
    return (d * 0.5f - jmax (2.0f * s, d * 0.04f) - 1.0f * s) * ringScale;
}

String Knob::valueText() const
{
    if (param == nullptr) return {};
    return format ? format (param->convertFrom0to1 (norm)) : param->getCurrentValueAsText();
}

void Knob::touchPill() { lastActive = Time::getMillisecondCounterHiRes() * 0.001; }

float Knob::pillAlpha() const
{
    // visible pendant le réglage, puis s'efface en fondu ; revient au clic ou dès que la valeur bouge
   #ifdef NHRACK_SHOW_PILLS
    return 1.0f;                 // outil de captures : pastilles toujours visibles
   #endif
    const double idle = Time::getMillisecondCounterHiRes() * 0.001 - lastActive;
    return (float) jlimit (0.0, 1.0, 1.0 - (idle - 2.5) / 0.35);
}

void Knob::tick()
{
    if (std::abs (norm - lastNorm) > 1.0e-6f) { if (lastNorm >= 0.0f) touchPill(); lastNorm = norm; }
    shownAngle = angleOf (norm);
    const float step = MathConstants<float>::twoPi / 40.0f;
    const float t = std::abs (shownAngle - prevAngle) / step;
    if (std::abs (shownAngle - prevAngle) > 1.0e-5f) blurDir = shownAngle > prevAngle ? -1.0f : 1.0f;
    prevAngle = shownAngle;
    const float lvl = jlimit (0.0f, 1.6f, (t - 0.15f) / 0.6f);
    const float nb = lvl > blur ? lvl : blur * 0.6f;
    if (std::abs (nb - blur) > 0.002f) { blur = nb < 0.01f ? 0.0f : nb; repaint(); }
    // face d'alu anodisé précalculée à la résolution physique de l'écran (jamais dans paint)
    const int px = jmax (8, (int) std::ceil (2.0f * nhrack::faceRadius (ringRadius()) * Component::getApproximateScaleFactorForComponent (this)));
    if (px != facePx) { facePx = px; face = nhrack::makeAnodisedFace (px); repaint(); }
}

void Knob::paint (Graphics& g)
{
    const float s = ed.sc();
    const bool en = enabledLook;
    nhrack::Look k;
    k.c = centre(); k.ring = ringRadius(); k.angle = shownAngle;
    k.a0 = angleOf (0.0f); k.a1 = angleOf (1.0f); k.from = bipolar ? 0.0f : k.a0;
    k.s = s; k.glow = ed.glow(); k.alpha = en ? 1.0f : 0.35f; k.blur = blur; k.blurDir = blurDir;
    k.colour = colour; k.face = &face;
    nhrack::paintKnob (g, k);
    // nom sous le potard (la valeur est dans la pastille au bout de l'anneau)
    g.setFont (ed.font (2, 8.4f * s).withExtraKerningFactor (0.14f));
    g.setColour ((col::dim).withMultipliedAlpha (k.alpha));
    g.drawText (label, labelBox(), Justification::centred, false);
}

Rectangle<float> Knob::labelBox() const
{
    const float s = ed.sc();
    const float ly = jmin ((float) getHeight() - 12.0f * s, centre().y + ringRadius() + 7.0f * s);
    return Rectangle<float> (-20.0f * s, ly, (float) getWidth() + 40.0f * s, 11.0f * s);
}

float Knob::pillFont() const
{
    const float s = ed.sc();
    return jlimit (valueFont, valueFont * 1.35f, ringRadius() / s * 0.19f) * s;
}

nhrack::PillOverlay::Pill Knob::pill() const
{
    nhrack::PillOverlay::Pill p;
    if (param == nullptr) return p;
    const float s = ed.sc();
    p.text = valueText();
    const Font f = ed.font (1, pillFont());
    p.r = nhrack::pillRect (centre(), ringRadius(), angleOf (norm), p.text, f, s);
    { const float rr = ringRadius() * 0.92f; p.ringBox = Rectangle<float> (rr * 2, rr * 2).withCentre (centre()); }
    {
        const auto lb = labelBox();
        const float lw = GlyphArrangement::getStringWidth (ed.font (2, 8.4f * s).withExtraKerningFactor (0.14f), label) + 4.0f * s;
        if (lw > 5.0f * s) p.labelBox = Rectangle<float> (lw, 9.0f * s).withCentre ({ lb.getCentreX(), lb.getY() + 5.5f * s });
    }
    p.c = centre(); p.ring = ringRadius(); p.angle = angleOf (norm); p.gap = 3.5f * s;
    p.colour = colour;
    p.alpha = pillAlpha() * (((enabledLook)) ? 1.0f : 0.35f);
    p.visible = p.alpha > 0.001f;
    return p;
}

void Knob::mouseDown (const MouseEvent& e)
{
    touchPill();
    if (param == nullptr) return;
    if (e.mods.isPopupMenu()) { if (extraMenu) extraMenu(); else ed.showParamMidiMenu (param, param->getName (64)); return; }
    dragging = true; dragStart = norm;
    att->beginGesture();
    e.source.enableUnboundedMouseMovement (true);
}

void Knob::mouseDrag (const MouseEvent& e)
{
    if (! dragging || param == nullptr) return;
    const float k = e.mods.isShiftDown() ? 0.0008f : 0.0045f;
    const float v = jlimit (0.0f, 1.0f, dragStart - (float) e.getDistanceFromDragStartY() * k / jmax (0.6f, ed.sc()));
    att->setValueAsPartOfGesture (param->convertFrom0to1 (v));
}

void Knob::mouseUp (const MouseEvent& e)
{
    touchPill();
    if (! dragging) return;
    dragging = false;
    att->endGesture();
    e.source.enableUnboundedMouseMovement (false);
}

void Knob::mouseDoubleClick (const MouseEvent&)
{
    touchPill();
    if (param != nullptr) att->setValueAsCompleteGesture (param->convertFrom0to1 (param->getDefaultValue()));
}

void Knob::mouseWheelMove (const MouseEvent& e, const MouseWheelDetails& w)
{
    touchPill();
    if (param == nullptr) return;
    const float step = (e.mods.isShiftDown() ? 0.005f : 0.03f) * (w.deltaY >= 0 ? 1.0f : -1.0f) * jmin (1.0f, std::abs (w.deltaY) * 4.0f);
    att->setValueAsCompleteGesture (param->convertFrom0to1 (jlimit (0.0f, 1.0f, norm + step)));
}

//==============================================================================
//  Écran : historique défilant (entrée, sortie, réduction de gain) + bandes du multibande
//==============================================================================
Display::Display (LmixEditor& e) : ed (e) {}

Rectangle<float> Display::chip (int i) const
{
    const float s = ed.sc();
    static const float w[4] = { 24, 24, 26, 40 };
    float x = (float) getWidth() - 29.0f * s;
    for (int k = 3; k >= i; --k) x -= (w[k] + 3.0f) * s;
    return { x + 3.0f * s, 12.0f * s, w[i] * s, 14.0f * s };
}

Rectangle<float> Display::histArea() const
{
    auto r = getLocalBounds().toFloat();
    if (ed.proc.prm.mb->load() > 0.5f) r = r.withHeight (r.getHeight() * 0.62f);
    return r;
}

Rectangle<float> Display::bandArea() const
{
    const float s = ed.sc();
    auto r = getLocalBounds().toFloat();
    const float top = r.getHeight() * 0.62f;
    return Rectangle<float> (8.0f * s, top + 4.0f * s, r.getWidth() - 16.0f * s, r.getHeight() - top - 12.0f * s);
}

float Display::yOf (float db) const
{
    const float s = ed.sc();
    const auto a = histArea();
    const bool mb = ed.proc.prm.mb->load() > 0.5f;
    const float top = a.getY() + (mb ? 30.0f : 39.0f) * s, bottom = a.getBottom() - (mb ? 6.0f : 16.0f) * s;
    return top + jlimit (0.0f, 30.0f, -db) / 24.0f * (bottom - top);
}

float Display::xOfHz (double hz) const
{
    const auto b = bandArea();
    return b.getX() + b.getWidth() * (float) (std::log (hz / 20.0) / std::log (1000.0));
}

double Display::hzAtX (float x) const
{
    const auto b = bandArea();
    return 20.0 * std::pow (1000.0, jlimit (0.0, 1.0, (double) ((x - b.getX()) / b.getWidth())));
}

void Display::tick()
{
    const int n = ed.proc.pullHistory (tmp.data(), (int) tmp.size());
    if (! frozen)
        for (int i = 0; i < n; ++i) { ring[(size_t) ringPos] = tmp[(size_t) i]; ringPos = (ringPos + 1) % kRing; }
    if ((n > 0 && ! frozen) || ed.proc.prm.mb->load() > 0.5f) ed.askRepaint (getBoundsInParent());
}

void Display::paint (Graphics& g)
{
    const float s = ed.sc();
    const auto r = getLocalBounds().toFloat();
    g.setColour (col::screen);
    g.fillRoundedRectangle (r, 7.0f * s);
    paintHistory (g);
    if (ed.proc.prm.mb->load() > 0.5f) paintBands (g);
    // chips de durée + gel
    static const char* names[4] = { "3 s", "6 s", "12 s", "FREEZE" };
    for (int i = 0; i < 4; ++i)
        paintChip (g, chip (i), names[i], i < 3 ? ed.proc.historyIdx.load() == i : frozen, s, ed.font (1, 8.0f * s), ed.glow(), true);
    // bord + ombre intérieure
    g.setColour (col::screenBorder);
    g.drawRoundedRectangle (r.reduced (0.5f), 7.0f * s, 1.0f);
    g.setColour (Colours::black.withAlpha (0.5f));
    g.drawRoundedRectangle (r.reduced (1.5f), 6.5f * s, 1.5f);
}

void Display::paintHistory (Graphics& g)
{
    const float s = ed.sc();
    const auto a = histArea();
    const bool mb = ed.proc.prm.mb->load() > 0.5f;
    const float plotR = a.getRight() - 40.0f * s;
    const float x0 = a.getX() + 2.0f * s;
    // grille
    for (int db = 0; db >= -24; db -= (mb ? 6 : 3))
    {
        const float y = yOf ((float) db);
        g.setColour (col::grid);
        g.drawHorizontalLine ((int) y, x0, plotR);
        g.setColour (col::label.withAlpha (0.85f));
        g.setFont (ed.font (0, 7.6f * s));
        g.drawText (db == 0 ? "0" : String (db), Rectangle<float> (plotR + 4.0f * s, y - 6.0f * s, 30.0f * s, 12.0f * s), Justification::centredLeft, false);
    }
    for (float x = x0; x < plotR; x += 62.5f * s) { g.setColour (col::grid); g.drawVerticalLine ((int) x, yOf (0.0f), a.getBottom()); }

    // points de l'historique
    static const int secs[3] = { 3, 6, 12 };
    const int N = secs[jlimit (0, 2, ed.proc.historyIdx.load())] * LmixProcessor::kHistRate;
    const float w = plotR - x0;
    const int cols = jmax (2, (int) (w / (1.0f * s)));
    Path pin, pout, pgr, lout, lgr;
    const float yb = a.getBottom();
    const float grTop = a.getY() + 2.0f * s, grK = (mb ? 3.0f : 4.5f) * s;
    pin.startNewSubPath (x0, yb); pout.startNewSubPath (x0, yb); pgr.startNewSubPath (x0, grTop);
    for (int cIdx = 0; cIdx < cols; ++cIdx)
    {
        const int i0 = N - 1 - (int) ((int64) (cols - cIdx) * N / cols), i1 = N - 1 - (int) ((int64) (cols - cIdx - 1) * N / cols);
        float vin = -120, vout = -120, vgr = 0;
        for (int i = jmax (0, i0 + 1); i <= i1; ++i)
        {
            const auto& p = ring[(size_t) ((ringPos - N + i + 2 * kRing) % kRing)];
            vin = jmax (vin, p.inDb); vout = jmax (vout, p.outDb); vgr = jmax (vgr, p.grDb);
        }
        const float x = x0 + w * (float) cIdx / (float) (cols - 1);
        pin.lineTo (x, jmin (yb, yOf (jmin (vin, 2.0f))));
        const float yo = jmin (yb, yOf (vout));
        pout.lineTo (x, yo);
        if (cIdx == 0) lout.startNewSubPath (x, yo); else lout.lineTo (x, yo);
        const float yg = grTop + jmin (vgr, 14.0f) * grK;
        pgr.lineTo (x, yg);
        if (cIdx == 0) lgr.startNewSubPath (x, yg); else lgr.lineTo (x, yg);
    }
    pin.lineTo (plotR, yb); pin.closeSubPath();
    pout.lineTo (plotR, yb); pout.closeSubPath();
    pgr.lineTo (plotR, grTop); pgr.closeSubPath();

    g.saveState();
    g.reduceClipRegion (a.reduced (1.0f).toNearestInt());
    g.setGradientFill (ColourGradient (Colour (0x666b7483), 0, yOf (0.0f), Colour (0x105d6675), 0, yb, false));
    g.fillPath (pin);
    {
        ColourGradient go (Colour (0xaad7dee8), 0, yOf (0.0f), Colour (0x128d96a4), 0, yb, false);
        go.addColour (0.45, Colour (0x558d96a4));
        g.setGradientFill (go);
        g.fillPath (pout);
        g.setColour (Colour (0xffeef3f8));
        g.strokePath (lout, PathStrokeType (1.2f * s));
    }
    // plafond (pointillés)
    {
        const float yc = yOf (ed.proc.prm.ceiling->load());
        Path pl; pl.startNewSubPath (x0, yc); pl.lineTo (plotR, yc);
        g.setColour (col::accent.withAlpha (0.8f));
        g.fillPath (dashed (pl, 3.5f * s, 3.0f * s, 1.3f * s));
    }
    // réduction de gain (rouge, depuis le haut)
    g.setGradientFill (ColourGradient (col::accent.withAlpha (0.88f), 0, grTop, col::accent.withAlpha (0.19f), 0, grTop + 30.0f * s, false));
    g.fillPath (pgr);
    g.setColour (col::accent.withAlpha (0.22f * ed.glow()));
    g.strokePath (lgr, PathStrokeType (4.0f * s));
    g.setColour (col::accentHi);
    g.strokePath (lgr, PathStrokeType (1.4f * s));
    // fondu à droite (arrivée des points)
    g.setGradientFill (ColourGradient (col::screen.withAlpha (0.0f), plotR - 45.0f * s, 0, col::screen, plotR, 0, false));
    g.fillRect (Rectangle<float> (plotR - 45.0f * s, a.getY(), 45.0f * s, a.getHeight()));
    g.restoreState();

    // légende
    const float ly = a.getBottom() - 12.0f * s;
    g.setFont (ed.font (2, 7.4f * s).withExtraKerningFactor (0.14f));
    float lx = a.getX() + 11.0f * s;
    {
        Path d; d.startNewSubPath (lx, ly); d.lineTo (lx + 11.0f * s, ly);
        g.setColour (col::accent); g.fillPath (dashed (d, 3.0f * s, 2.0f * s, 1.3f * s));
        g.drawText ("CEILING  " + String (ed.proc.prm.ceiling->load(), 1) + (ed.proc.prm.truePeak->load() > 0.5f ? " dBTP" : " dBFS"),
                    Rectangle<float> (lx + 15.0f * s, ly - 6.0f * s, 120.0f * s, 12.0f * s), Justification::centredLeft, false);
        lx += 110.0f * s;
    }
    const std::pair<const char*, Colour> items[3] = { { "GAIN REDUCTION", col::accent }, { "OUTPUT", Colour (0xffd7dee8) }, { "INPUT", col::label } };
    for (auto& it : items)
    {
        g.setColour (it.second);
        g.fillRoundedRectangle (Rectangle<float> (lx, ly - 2.5f * s, 7.0f * s, 5.0f * s), 1.0f * s);
        g.setColour (Colour (0xffc9d0d9));
        const float tw = GlyphArrangement::getStringWidth (g.getCurrentFont(), it.first);
        g.drawText (it.first, Rectangle<float> (lx + 11.0f * s, ly - 6.0f * s, tw + 4.0f, 12.0f * s), Justification::centredLeft, false);
        lx += tw + 28.0f * s;
    }
}

void Display::paintBands (Graphics& g)
{
    const float s = ed.sc();
    const auto b = bandArea();
    g.setColour (Colour (0xff0b0e12));
    g.fillRoundedRectangle (b, 5.0f * s);
    g.setColour (col::panelBorder);
    g.drawRoundedRectangle (b.reduced (0.5f), 5.0f * s, 1.0f);
    // graduations en fréquence
    g.setFont (ed.font (0, 7.2f * s));
    for (double f : { 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0 })
    {
        const float x = xOfHz (f);
        g.setColour (col::grid);
        g.drawVerticalLine ((int) x, b.getY() + 2.0f, b.getBottom() - 12.0f * s);
        g.setColour (col::label);
        g.drawText (f >= 1000 ? String ((int) (f / 1000)) + "k" : String ((int) f), Rectangle<float> (x - 15 * s, b.getBottom() - 11.0f * s, 30 * s, 10 * s), Justification::centred, false);
    }
    const double xo[3] = { ed.proc.prm.xo1->load(), ed.proc.prm.xo2->load(), ed.proc.prm.xo3->load() };
    const float edges[5] = { b.getX(), xOfHz (xo[0]), xOfHz (xo[1]), xOfHz (xo[2]), b.getRight() };
    static const char* names[4] = { "LOW", "LOW MID", "HIGH MID", "HIGH" };
    const float barTop = b.getY() + 4.0f * s, barH = b.getHeight() - 30.0f * s;
    for (int k = 0; k < 4; ++k)
    {
        const auto band = Rectangle<float> (edges[k] + 2.0f * s, barTop, edges[k + 1] - edges[k] - 4.0f * s, barH);
        const float gr = ed.proc.bandGr[k].load();
        const float h = jlimit (0.0f, 1.0f, gr / 12.0f) * barH;
        g.setGradientFill (ColourGradient (col::accent.withAlpha (0.85f), 0, barTop, col::accent.withAlpha (0.25f), 0, barTop + barH, false));
        g.fillRect (band.withHeight (h));
        g.setColour (col::accentHi);
        g.drawHorizontalLine ((int) (barTop + h), band.getX(), band.getRight());
        g.setColour (col::dim);
        g.setFont (ed.font (2, 7.6f * s).withExtraKerningFactor (0.12f));
        g.drawText (names[k], band.withTrimmedTop (barH * 0.45f).withHeight (12.0f * s), Justification::centred, false);
        g.setColour (col::text);
        g.setFont (ed.font (1, 9.0f * s));
        g.drawText (gr > 0.05f ? "-" + String (gr, 1) + " dB" : String ("0.0 dB"), band.withTrimmedTop (barH * 0.45f + 11.0f * s).withHeight (12.0f * s), Justification::centred, false);
    }
    // poignées des fréquences de coupure
    for (int k = 0; k < 3; ++k)
    {
        const float x = xOfHz (xo[k]);
        const bool hi = dragXo == k || hoverXo == k;
        g.setColour ((hi ? Colours::white : col::dim).withAlpha (0.9f));
        g.drawLine (x, b.getY() + 3.0f * s, x, b.getBottom() - 13.0f * s, hi ? 1.6f * s : 1.0f * s);
        const auto hr = Rectangle<float> (34.0f * s, 12.0f * s).withCentre ({ x, b.getY() + 10.0f * s });
        g.setColour (Colour (0xff12161b));
        g.fillRoundedRectangle (hr, 6.0f * s);
        g.setColour (hi ? col::accent : col::segBorder);
        g.drawRoundedRectangle (hr, 6.0f * s, 1.0f);
        g.setColour (col::text);
        g.setFont (ed.font (1, 7.4f * s));
        g.drawText (hzStr (xo[k]), hr, Justification::centred, false);
    }
}

void Display::mouseMove (const MouseEvent& e)
{
    int h = -1;
    if (ed.proc.prm.mb->load() > 0.5f && bandArea().contains (e.position))
    {
        const double xo[3] = { ed.proc.prm.xo1->load(), ed.proc.prm.xo2->load(), ed.proc.prm.xo3->load() };
        for (int k = 0; k < 3; ++k) if (std::abs (e.position.x - xOfHz (xo[k])) < 8.0f * ed.sc()) h = k;
    }
    if (h != hoverXo) { hoverXo = h; setMouseCursor (h >= 0 ? MouseCursor::LeftRightResizeCursor : MouseCursor::NormalCursor); repaint(); }
}

void Display::mouseDown (const MouseEvent& e)
{
    if (e.mods.isPopupMenu()) { ed.showMainMenu(); return; }
    for (int i = 0; i < 4; ++i)
        if (chip (i).expanded (2.0f).contains (e.position))
        {
            if (i < 3) ed.proc.historyIdx = i; else frozen = ! frozen;
            ed.showStatus (i < 3 ? "HISTORY  " + String (i == 0 ? 3 : i == 1 ? 6 : 12) + " S" : String (frozen ? "DISPLAY FROZEN" : "DISPLAY RUNNING"));
            repaint();
            return;
        }
    if (hoverXo >= 0)
    {
        dragXo = hoverXo;
        static const char* ids[3] = { "xo1", "xo2", "xo3" };
        if (auto* p = ed.proc.apvts.getParameter (ids[dragXo])) p->beginChangeGesture();
    }
}

void Display::mouseDrag (const MouseEvent& e)
{
    if (dragXo < 0) return;
    static const char* ids[3] = { "xo1", "xo2", "xo3" };
    if (auto* p = ed.proc.apvts.getParameter (ids[dragXo]))
    {
        double hz = hzAtX (e.position.x);
        const double lo = dragXo > 0 ? ed.proc.paramValue (ids[dragXo - 1]) * 1.25 : 20.0;
        const double hi = dragXo < 2 ? ed.proc.paramValue (ids[dragXo + 1]) / 1.25 : 20000.0;
        hz = jlimit (lo, hi, hz);
        p->setValueNotifyingHost (p->convertTo0to1 ((float) hz));
        ed.proc.presetDirty = true;
    }
    repaint();
}

void Display::mouseUp (const MouseEvent&)
{
    static const char* ids[3] = { "xo1", "xo2", "xo3" };
    if (dragXo >= 0) if (auto* p = ed.proc.apvts.getParameter (ids[dragXo])) p->endChangeGesture();
    dragXo = -1;
}

//==============================================================================
//  Vumètres, sonie, réduction de gain
//==============================================================================
Meters::Meters (LmixEditor& e) : ed (e) {}

Rectangle<float> Meters::loudRect() const   { const float s = ed.sc(); return { 75 * s, 18 * s, 100 * s, 172 * s }; }
Rectangle<float> Meters::targetRect() const { const float s = ed.sc(); return { 75 * s, 196 * s, 99 * s, 17 * s }; }
Rectangle<float> Meters::grRect() const     { const float s = ed.sc(); return { 8 * s, 235 * s, 166 * s, 66 * s }; }

void Meters::tick()
{
    const double t = nowS();
    auto upd = [&] (float& lv, float& hd, double& ht, std::atomic<float>& src)
    {
        const float db = (float) lmix::gainToDb (src.exchange (0.0f));
        lv = db > lv ? db : jmax (db, lv - 0.75f);                    // ~22 dB/s
        if (db >= hd || t - ht > 1.5) { hd = db; ht = t; }
    };
    for (int c = 0; c < 2; ++c) { upd (lvIn[c], hold[c], holdT[c], ed.proc.inPeak[c]); upd (lvOut[c], hold[2 + c], holdT[2 + c], ed.proc.outPeak[c]); }
    shownGr = ed.proc.grNow.load();
    grHist[(size_t) grPos] = shownGr; grPos = (grPos + 1) % (int) grHist.size();
    if (++slow >= 6)
    {
        slow = 0;
        lmix::Loudness::snapshot (ed.proc.loud.hist, histI);
        lmix::Loudness::snapshot (ed.proc.loud.histS, histS);
        integrated = lmix::Loudness::integrated (histI);
        lra = lmix::Loudness::range (histS);
    }
    ed.askRepaint (getBoundsInParent());
}

void Meters::paint (Graphics& g)
{
    const float s = ed.sc();
    const auto r = getLocalBounds().toFloat();
    paintPanel (g, r, s, Colour (0xff0b0e12), Colour (0xff090b0f));
    auto title = [&] (const String& t, float x, float y)
    {
        g.setFont (ed.font (2, 7.6f * s).withExtraKerningFactor (0.18f));
        g.setColour (col::dim);
        g.drawText (t, Rectangle<float> (x * s, y * s, 120 * s, 10 * s), Justification::centredLeft, false);
    };
    title ("IN", 8, 8); title ("OUT", 35, 8); title ("LOUDNESS", 75, 8);

    // barres (échelle linéaire en dB, -60 .. 0)
    const float top = 22 * s, h = 186 * s;
    auto yDb = [&] (float db) { return top + jlimit (0.0f, 1.0f, -db / 60.0f) * h; };
    auto bar = [&] (float x, float lv, float hd, bool outBar)
    {
        const auto br = Rectangle<float> (x * s, top, 8.0f * s, h);
        g.setColour (col::face); g.fillRoundedRectangle (br, 1.5f * s);
        g.setColour (Colour (0xff1d232c)); g.drawRoundedRectangle (br, 1.5f * s, 1.0f);
        const float y = yDb (lv);
        if (lv > -60.0f)
        {
            auto fill = br.withTop (y).reduced (1.0f, 0.0f).withTrimmedBottom (1.0f);
            if (outBar) g.setGradientFill (ColourGradient (Colour (0xffc9d3df), 0, top, Colour (0xff4a525d), 0, top + h, false));
            else
            {
                ColourGradient cg (col::accent, 0, top, Colour (0xff2f6f6a), 0, top + h, false);
                cg.addColour (0.1, col::amber); cg.addColour (0.3, col::green);
                g.setGradientFill (cg);
            }
            g.fillRect (fill);
        }
        g.setColour (Colours::white.withAlpha (0.8f));
        if (hd > -60.0f) g.fillRect (Rectangle<float> (x * s + 1.0f, yDb (hd) - 0.5f * s, 8.0f * s - 2.0f, 1.2f * s));
    };
    bar (8, lvIn[0], hold[0], false); bar (18, lvIn[1], hold[1], false);
    bar (35, lvOut[0], hold[2], true); bar (45, lvOut[1], hold[3], true);
    // plafond sur les barres de sortie
    g.setColour (col::accent);
    g.fillRect (Rectangle<float> (33 * s, yDb (ed.proc.prm.ceiling->load()) - 0.7f * s, 22 * s, 1.4f * s));
    g.setFont (ed.font (0, 6.8f * s));
    g.setColour (col::label);
    for (int db : { 0, -6, -12, -18, -24, -36, -48, -60 })
        g.drawText (String (db), Rectangle<float> (55 * s, yDb ((float) db) - 5 * s, 16 * s, 10 * s), Justification::centredRight, false);
    // valeurs numériques sous les barres
    g.setFont (ed.font (3, 8.6f * s));
    g.setColour (col::text);
    auto pk = [] (float a, float b) { const float m = jmax (a, b); return m <= -60.0f ? String ("-inf") : (m > 0.05f ? "+" : "") + String (m, 1); };
    g.drawText (pk (hold[0], hold[1]), Rectangle<float> (4 * s, top + h + 4 * s, 26 * s, 11 * s), Justification::centred, false);
    g.drawText (pk (hold[2], hold[3]), Rectangle<float> (31 * s, top + h + 4 * s, 26 * s, 11 * s), Justification::centred, false);

    // sonie
    const auto& tg = LmixProcessor::targets()[(size_t) jlimit (0, (int) LmixProcessor::targets().size() - 1, ed.proc.targetIdx.load())];
    const double tpDb = lmix::gainToDb (ed.proc.truePeakMax.load());
    struct Row { const char* k; double v; const char* u; Colour c; };
    auto lu = [] (double v) { return v; };
    const double I = integrated;
    Colour iCol = col::text;
    if (I > -199) iCol = I > tg.lufs + 1.0 ? col::accentHi : I >= tg.lufs - 1.0 ? col::green : col::text;
    const Row rows[6] = {
        { "INTEGRATED", lu (I), "LUFS", iCol },
        { "SHORT-TERM", ed.proc.shortTerm.load(), "LUFS", col::text },
        { "MOMENTARY", ed.proc.momentary.load(), "LUFS", col::text },
        { "RANGE (LRA)", lra, "LU", col::text },
        { "TRUE PEAK", tpDb, "dBTP", tpDb > tg.tp + 0.001 ? col::accentHi : col::text },
        { "PLR", I > -199 ? tpDb - I : -300.0, "dB", col::text } };
    for (int i = 0; i < 6; ++i)
    {
        const float y = (22 + 28 * i) * s;
        g.setFont (ed.font (2, 6.9f * s).withExtraKerningFactor (0.14f));
        g.setColour (col::dim);
        g.drawText (rows[i].k, Rectangle<float> (75 * s, y + 2 * s, 100 * s, 9 * s), Justification::centredLeft, false);
        const bool valid = (i == 3) ? (lra > 0.0 || I > -199) : rows[i].v > -199;
        const String vt = valid ? String (rows[i].v, 1) : String ("-.-");
        g.setFont (ed.font (3, 13.0f * s));
        g.setColour (rows[i].c);
        const float uw = 22 * s;
        g.drawText (vt, Rectangle<float> (75 * s, y + 9 * s, 100 * s - uw - 2 * s, 16 * s), Justification::centredRight, false);
        g.setFont (ed.font (1, 6.6f * s));
        g.setColour (col::dim);
        g.drawText (rows[i].u, Rectangle<float> (175 * s - uw, y + 13 * s, uw, 10 * s), Justification::centredLeft, false);
        if (i < 5) { g.setColour (Colour (0xff1a2028)); g.drawHorizontalLine ((int) (y + 27 * s), 75 * s, 174 * s); }
    }
    // cible
    {
        const auto tr = targetRect();
        g.setColour (Colour (0xff12161b)); g.fillRoundedRectangle (tr, 4 * s);
        g.setColour (col::segBorder); g.drawRoundedRectangle (tr.reduced (0.5f), 4 * s, 1.0f);
        g.setFont (ed.font (2, 7.6f * s));
        g.setColour (col::text);
        g.drawText ("TARGET", tr.withTrimmedLeft (7 * s), Justification::centredLeft, false);
        g.setFont (ed.font (0, 7.6f * s));
        g.setColour (col::dim);
        g.drawText (tg.shortName, tr.withTrimmedRight (17 * s), Justification::centredRight, false);
        Path tri; const float cx = tr.getRight() - 10 * s, cy = tr.getCentreY();
        tri.addTriangle (cx - 3 * s, cy - 1.5f * s, cx + 3 * s, cy - 1.5f * s, cx, cy + 2.2f * s);
        g.fillPath (tri);
    }
    // réduction de gain : historique + maximum
    title ("GAIN REDUCTION", 75, 221);
    {
        const auto gr = grRect();
        g.setColour (col::screen); g.fillRoundedRectangle (gr, 5 * s);
        g.setColour (Colour (0xff1d232c)); g.drawRoundedRectangle (gr.reduced (0.5f), 5 * s, 1.0f);
        Path p; const int n = (int) grHist.size();
        p.startNewSubPath (gr.getX() + 1, gr.getY() + 3 * s);
        Path l;
        for (int i = 0; i < n; ++i)
        {
            const float v = grHist[(size_t) ((grPos + i) % n)];
            const float x = gr.getX() + 1 + (gr.getWidth() - 2) * (float) i / (float) (n - 1);
            const float y = gr.getY() + 3 * s + jmin (v, 12.0f) * 3.4f * s;
            p.lineTo (x, y);
            if (i == 0) l.startNewSubPath (x, y); else l.lineTo (x, y);
        }
        p.lineTo (gr.getRight() - 1, gr.getY() + 3 * s); p.closeSubPath();
        g.setColour (col::accent.withAlpha (0.22f)); g.fillPath (p);
        g.setColour (col::accentHi); g.strokePath (l, PathStrokeType (1.2f * s));
        const float mx = ed.proc.grMax.load();
        g.setFont (ed.font (3, 13.5f * s));
        g.setColour (col::text);
        g.drawText (mx > 0.05f ? "-" + String (mx, 1) : String ("0.0"), Rectangle<float> (gr.getX(), gr.getBottom() - 19 * s, gr.getWidth() - 40 * s, 16 * s), Justification::centredRight, false);
        g.setFont (ed.font (1, 6.6f * s).withExtraKerningFactor (0.14f));
        g.setColour (col::dim);
        g.drawText ("dB MAX", Rectangle<float> (gr.getRight() - 38 * s, gr.getBottom() - 16 * s, 34 * s, 10 * s), Justification::centredLeft, false);
    }
}

void Meters::mouseDown (const MouseEvent& e)
{
    if (e.mods.isPopupMenu()) { ed.showMainMenu(); return; }
    if (targetRect().contains (e.position)) { ed.showTargetMenu(); return; }
    if (loudRect().contains (e.position)) { ed.proc.resetMeters = true; ed.showStatus ("LOUDNESS RESET"); return; }
    if (grRect().contains (e.position)) { ed.proc.grMax = 0.0f; ed.showStatus ("GAIN REDUCTION MAX RESET"); return; }
}

//==============================================================================
//  Fenêtre
//==============================================================================
LmixEditor::LmixEditor (LmixProcessor& p)
    : AudioProcessorEditor (p), proc (p), display (*this), meters (*this),
      gainKnob (*this, "GAIN"), ceilKnob (*this, "CEILING"), attackKnob (*this, "ATTACK"), releaseKnob (*this, "RELEASE"), lookKnob (*this, "LOOKAHEAD"),
      linkTKnob (*this, "LINK TRANS."), linkRKnob (*this, "LINK REL."), transKnob (*this, "TRANSIENT"), enhKnob (*this, "ENHANCE"), clipKnob (*this, "CLIP")
{
    fonts.reg  = Typeface::createSystemTypefaceFor (BinaryData::PoppinsRegular_ttf, BinaryData::PoppinsRegular_ttfSize);
    fonts.med  = Typeface::createSystemTypefaceFor (BinaryData::PoppinsMedium_ttf, BinaryData::PoppinsMedium_ttfSize);
    fonts.bold = Typeface::createSystemTypefaceFor (BinaryData::PoppinsBold_ttf, BinaryData::PoppinsBold_ttfSize);
    mono       = Typeface::createSystemTypefaceFor (BinaryData::IBMPlexMonoSemiBold_ttf, BinaryData::IBMPlexMonoSemiBold_ttfSize);
    menuLnf.f = fonts;
    logo = Drawable::createFromImageData (BinaryData::NineHandsLogoWhite_svg, BinaryData::NineHandsLogoWhite_svgSize);

    setOpaque (true);
    addAndMakeVisible (display);
    addAndMakeVisible (meters);
    auto& ap = proc.apvts;
    gainKnob.attach (ap.getParameter ("gain"), false);       gainKnob.format = [] (float v) { return dbStr (v); };
    ceilKnob.attach (ap.getParameter ("ceiling"), false);    ceilKnob.format = [this] (float v) { return String (v, 1) + (proc.prm.truePeak->load() > 0.5f ? " dBTP" : " dBFS"); };
    attackKnob.attach (ap.getParameter ("attack"), false);   attackKnob.format = [] (float v) { return String ((int) std::round (v)) + " %"; };
    releaseKnob.attach (ap.getParameter ("release"), false);
    releaseKnob.format = [this] (float v) { return (proc.prm.autoRel->load() > 0.5f ? String ("Auto ") + String::charToString (0xB7) + " " : String()) + String (v, v < 10 ? 1 : 0) + " ms"; };
    releaseKnob.extraMenu = [this] { showReleaseMenu(); };
    lookKnob.attach (ap.getParameter ("lookahead"), false);  lookKnob.format = [] (float v) { return String (v, 1) + " ms"; };
    linkTKnob.attach (ap.getParameter ("linkTrans"), false); linkTKnob.format = [] (float v) { return String ((int) std::round (v)) + " %"; };
    linkRKnob.attach (ap.getParameter ("linkRel"), false);   linkRKnob.format = [] (float v) { return String ((int) std::round (v)) + " %"; };
    transKnob.attach (ap.getParameter ("transient"), false); transKnob.format = [] (float v) { return String ((int) std::round (v)) + " %"; };
    enhKnob.attach (ap.getParameter ("enhance"), false);     enhKnob.format = [] (float v) { return String ((int) std::round (v)) + " %"; };
    clipKnob.attach (ap.getParameter ("clip"), false);       clipKnob.format = [] (float v) { return String ((int) std::round (v)) + " %"; };
    for (auto* k : { &gainKnob, &ceilKnob, &attackKnob, &releaseKnob, &lookKnob, &linkTKnob, &linkRKnob, &transKnob, &enhKnob, &clipKnob })
        addAndMakeVisible (k);
    addAndMakeVisible (pills);

    setResizable (true, true);
    setResizeLimits (kW, kH, kMaxW, kMaxH);
    getConstrainer()->setFixedAspectRatio ((double) kW / kH);
    setScaleIndex (nhsize::openIndex ("LMix", proc.uiSizeIdx.load(), 3));   // taille de l'instance, sinon taille par défaut choisie

    for (auto* prm : proc.getParameters()) lastVals.push_back (prm->getValue());
    powerGlow = proc.prm.power->load() > 0.5f ? 1.0f : 0.0f;
    startTimerHz (30);
}

LmixEditor::~LmixEditor() { stopTimer(); hostLock.detach(); }

Font LmixEditor::font (int w, float h) const
{
    if (w == 3) return fnt (mono, h);
    return fnt (w == 0 ? fonts.reg : w == 1 ? fonts.med : fonts.bold, h);
}

void LmixEditor::setScaleIndex (int idx)
{
    static const int ws[3] = { kW, 1200, 1600 };
    setSize (ws[jlimit (0, 2, idx)], ws[jlimit (0, 2, idx)] * kH / kW);
}

void LmixEditor::resized()
{
    const float s = sc();
    auto R = [s] (float x, float y, float w, float h) { return Rectangle<float> (x * s, y * s, w * s, h * s).toNearestInt(); };
    display.setBounds (R (10, 47, 750, 310));
    meters.setBounds (R (768, 47, 182, 310));
    gainKnob.setBounds (R (37, 392, 100, 112));
    ceilKnob.setBounds (R (826, 384, 70, 84));
    int i = 0;
    for (auto* k : { &attackKnob, &releaseKnob, &lookKnob, &linkTKnob, &linkRKnob, &transKnob, &enhKnob, &clipKnob })
    { k->ringScale = 0.8f; k->setBounds (R (190.0f + 70.0f * (float) i++, 418, 66, 72)); }
    gainKnob.ringScale = 0.88f; ceilKnob.ringScale = 0.86f;
    pills.setBounds (getLocalBounds());
    pills.toFront (false);
    hostLock.setMinimumSize (kW, kH);
}

// ------------------------------------------------------------------ zones de la façade
Rectangle<float> LmixEditor::pill() const     { const float s = sc(); return Rectangle<float> (236 * s, 24 * s).withCentre ({ 480 * s, 23 * s }); }
Rectangle<float> LmixEditor::pillPrev() const { const auto p = pill(); return p.withWidth (p.getHeight() * 0.9f); }
Rectangle<float> LmixEditor::pillNext() const { const auto p = pill(); return p.withTrimmedLeft (p.getWidth() - p.getHeight() * 0.9f); }
Rectangle<float> LmixEditor::powerRect() const { const float s = sc(); return Rectangle<float> (24 * s, 24 * s).withCentre ({ 930 * s, 23 * s }); }
Rectangle<float> LmixEditor::autoChip() const { const float s = sc(); return { (260 + 13) * s, 494 * s, 40 * s, 13 * s }; }
Rectangle<float> LmixEditor::outChip (int i) const
{
    const float s = sc();
    return { (780.0f + 53.0f * (float) (i % 3)) * s, (474.0f + 23.0f * (float) (i / 3)) * s, 50.0f * s, 19.0f * s };
}
Rectangle<float> LmixEditor::styleChip (int i) const
{
    const float s = sc();
    const auto f = font (1, 10.0f * s);
    float x = 196.0f * s;
    for (int k = 0; k < i; ++k) x += GlyphArrangement::getStringWidth (f, lmix::styleName (k)) + 17.0f * s;
    return { x, 388.0f * s, GlyphArrangement::getStringWidth (f, lmix::styleName (i)) + 14.0f * s, 19.0f * s };
}

void LmixEditor::paint (Graphics& g)
{
    lastPaint = nowS(); pendingSince = 0;
    const float s = sc();
    g.fillAll (col::bg);
    paintFacade (g, getLocalBounds().toFloat());

    // en-tête : logo NINE HANDS | DSP + nom
    if (auto* lg = logo.get())
        lg->drawWithin (g, Rectangle<float> (18 * s, 17 * s, 132 * s, 12 * s), RectanglePlacement::xLeft | RectanglePlacement::yMid, 0.92f);
    g.setColour (col::text.withAlpha (0.18f));
    g.fillRect (Rectangle<float> (162 * s, 14 * s, 1.0f, 18 * s));
    g.setColour (col::text);
    g.setFont (font (2, 18 * s));
    g.drawText ("LMix", Rectangle<float> (174 * s, 12 * s, 70 * s, 22 * s), Justification::centredLeft, false);
    g.setColour (col::dim);
    g.setFont (font (1, 8.0f * s).withExtraKerningFactor (0.2f));
    g.drawText ("MASTERING LIMITER", Rectangle<float> (228 * s, 15 * s, 150 * s, 18 * s), Justification::centredLeft, false);

    // pastille presets / ligne d'état
    {
        const auto p = pill();
        g.setColour (Colours::white.withAlpha (0.06f));
        g.fillRoundedRectangle (p.translated (0, 1.0f), p.getHeight() * 0.5f);
        g.setColour (col::pillBorder.darker (0.3f));
        g.fillRoundedRectangle (p.expanded (1.0f), p.getHeight() * 0.5f + 1.0f);
        g.setColour (col::face);
        g.fillRoundedRectangle (p, p.getHeight() * 0.5f);
        g.setColour (col::pillBorder);
        g.drawRoundedRectangle (p.reduced (0.5f), p.getHeight() * 0.5f, 1.0f);
        const float a = 1.0f - statusAlpha;
        g.setFont (font (1, 12.0f * s));
        if (a > 0.01f)
        {
            g.setColour (col::text.withAlpha (a));
            g.drawFittedText (proc.presetName + (proc.presetDirty.load() ? " *" : ""), p.reduced (p.getHeight() * 0.95f, 0).toNearestInt(), Justification::centred, 1, 0.8f);
            for (int side = 0; side < 2; ++side)
            {
                const auto r = side == 0 ? pillPrev() : pillNext();
                const bool hl = hoverZone == (side == 0 ? 1 : 2);
                Path t;
                const float cx = r.getCentreX() + (side == 0 ? 2.0f : -2.0f) * s, cy = r.getCentreY(), h = 5.5f * 0.548f * s * 1.75f;
                if (side == 0) t.addTriangle (cx + h * 0.6f, cy - h, cx + h * 0.6f, cy + h, cx - h * 0.8f, cy);
                else           t.addTriangle (cx - h * 0.6f, cy - h, cx - h * 0.6f, cy + h, cx + h * 0.8f, cy);
                g.setColour ((hl ? Colours::white : col::text.withAlpha (0.6f)).withMultipliedAlpha (a));
                g.fillPath (t);
            }
        }
        if (statusAlpha > 0.01f)
        {
            g.setColour (col::accentHi.withAlpha (statusAlpha));
            nhstatus::draw (g, status, p.reduced (p.getHeight() * 0.5f, 0), Justification::centred);   // chiffres à chasse fixe
        }
    }

    // latence + true peak
    {
        const int lat = proc.latencyNow.load();
        const String l = "LATENCY " + String (1000.0 * lat / jmax (1.0, proc.currentRate.load()), 1) + " MS  " + String::charToString (0xB7) + "  ";
        const bool tp = proc.prm.truePeak->load() > 0.5f;
        const String t = tp ? "TRUE PEAK" : "SAMPLE PEAK";
        const auto f = font (1, 7.6f * s).withExtraKerningFactor (0.2f);
        const float wT = GlyphArrangement::getStringWidth (f, t), wL = GlyphArrangement::getStringWidth (f, l);
        const float xr = 900 * s;
        g.setFont (f);
        g.setColour (col::dim);
        g.drawText (l, Rectangle<float> (xr - wT - wL - 2, 15 * s, wL + 2, 16 * s), Justification::centredRight, false);
        g.setColour (tp ? col::accent : col::dim);
        g.drawText (t, Rectangle<float> (xr - wT - 1, 15 * s, wT + 2, 16 * s), Justification::centredLeft, false);
    }

    // interrupteur général
    {
        const auto r = powerRect();
        if (powerGlow > 0.01f)
        {
            ColourGradient halo (col::accent.withAlpha (0.35f * glow() * powerGlow), r.getCentreX(), r.getCentreY(), col::accent.withAlpha (0.0f), r.getCentreX() + r.getWidth(), r.getCentreY(), true);
            g.setGradientFill (halo);
            g.fillEllipse (r.expanded (r.getWidth() * 0.5f));
        }
        g.setColour (Colour (0xff161b22)); g.fillEllipse (r);
        g.setColour (col::pillBorder); g.drawEllipse (r.reduced (0.5f), 1.0f);
        LmixMenu::drawIco (g, LmixMenu::icoBypass, r.reduced (5.5f * s), col::accent.interpolatedWith (col::dim.withAlpha (0.6f), 1.0f - powerGlow));
    }

    // panneaux du bas
    paintPanel (g, Rectangle<float> (10 * s, 365 * s, 165 * s, 165 * s), s);
    paintPanel (g, Rectangle<float> (183 * s, 365 * s, 577 * s, 165 * s), s);
    paintPanel (g, Rectangle<float> (768 * s, 365 * s, 182 * s, 165 * s), s);
    g.setFont (font (2, 8.0f * s).withExtraKerningFactor (0.2f));
    g.setColour (col::dim);
    g.drawText ("INPUT", Rectangle<float> (22 * s, 372 * s, 100 * s, 12 * s), Justification::centredLeft, false);
    g.drawText ("STYLE", Rectangle<float> (196 * s, 372 * s, 100 * s, 12 * s), Justification::centredLeft, false);
    g.drawText ("OUTPUT", Rectangle<float> (780 * s, 372 * s, 100 * s, 12 * s), Justification::centredLeft, false);

    // styles
    const int st = (int) proc.prm.style->load();
    for (int i = 0; i < lmix::numStyles; ++i)
        paintChip (g, styleChip (i), lmix::styleName (i), st == i, s, font (1, 10.0f * s), glow());
    {
        const auto last = styleChip (lmix::numStyles - 1);
        const auto hr = Rectangle<float> (last.getRight() + 12 * s, 384 * s, 748 * s - last.getRight() - 12 * s, 28 * s);
        g.setFont (font (0, 8.2f * s));
        g.setColour (col::dim);
        g.drawFittedText (lmix::styleHint (st), hr.toNearestInt(), Justification::centredLeft, 2, 0.9f);
    }
    // bouton AUTO du relâchement
    paintChip (g, autoChip(), "AUTO", proc.prm.autoRel->load() > 0.5f, s, font (1, 7.4f * s), glow(), true);

    // sortie : true peak, suréchantillonnage, unity, delta, dither, multibande
    const int di = (int) proc.prm.dither->load();
    const String outT[6] = { "TRUE PEAK", "OS " + String (LmixProcessor::osFactor ((int) proc.prm.os->load())) + "x", "UNITY",
                             "DELTA", di == 0 ? String ("DITHER") : String ("DITHER ") + (di == 1 ? "16" : "24"), "MULTIBAND" };
    const bool outOn[6] = { proc.prm.truePeak->load() > 0.5f, (int) proc.prm.os->load() > 0, proc.prm.unity->load() > 0.5f,
                            proc.prm.delta->load() > 0.5f, di > 0, proc.prm.mb->load() > 0.5f };
    for (int i = 0; i < 6; ++i) paintChip (g, outChip (i), outT[i], outOn[i], s, font (1, 7.6f * s).withExtraKerningFactor (0.04f), glow());
}

void LmixEditor::mouseMove (const MouseEvent& e)
{
    const int z = pillPrev().contains (e.position) ? 1 : pillNext().contains (e.position) ? 2 : 0;
    if (z != hoverZone) { hoverZone = z; repaint (pill().expanded (4).toNearestInt()); }
}

bool LmixEditor::toggleParam (const char* id)
{
    auto* p = proc.apvts.getParameter (id);
    const bool on = p->getValue() < 0.5f;
    p->beginChangeGesture(); p->setValueNotifyingHost (on ? 1.0f : 0.0f); p->endChangeGesture();
    return on;
}

void LmixEditor::mouseDown (const MouseEvent& e)
{
    const auto p = e.position;
    static const char* outIds[6] = { "truePeak", "os", "unity", "delta", "dither", "mb" };
    if (e.mods.isPopupMenu())
    {
        for (int i = 0; i < lmix::numStyles; ++i) if (styleChip (i).contains (p)) { showStyleMenu(); return; }
        for (int i = 0; i < 6; ++i) if (outChip (i).contains (p))
        {
            if (i == 1) showOsMenu(); else if (i == 4) showDitherMenu();
            else showParamMidiMenu (proc.apvts.getParameter (outIds[i]), proc.apvts.getParameter (outIds[i])->getName (40));
            return;
        }
        if (autoChip().contains (p)) { showReleaseMenu(); return; }
        showMainMenu();
        return;
    }
    if (pill().contains (p))
    {
        if (statusAlpha > 0.05f) { statusTime = -10.0; return; }
        if (pillPrev().contains (p)) prevNextPreset (-1);
        else if (pillNext().contains (p)) prevNextPreset (1);
        else
        {
            PopupMenu m; m.setLookAndFeel (&menuLnf);
            LmixMenu::addHeader (m, fonts, LmixMenu::icoPreset, col::accent, "Presets", proc.presetName);
            const auto& fp = LmixProcessor::factoryPresets();
            for (int i = 0; i < (int) fp.size(); ++i)
                m.addItem (i + 1, fp[(size_t) i].name, true, proc.currentPreset.load() == i && ! proc.presetDirty.load());
            m.addSeparator();
            m.addItem (900, "Save preset...");
            m.addItem (901, "Load preset...");
            m.showMenuAsync (PopupMenu::Options().withTargetScreenArea (localAreaToGlobal (pill().toNearestInt())), [this] (int r)
            {
                if (r >= 1 && r < 900) proc.loadFactoryPreset (r - 1);
                else if (r == 900) savePreset();
                else if (r == 901) loadPreset();
                repaint();
            });
        }
        return;
    }
    if (powerRect().contains (p)) { toggleParam ("power"); return; }
    for (int i = 0; i < lmix::numStyles; ++i)
        if (styleChip (i).contains (p)) { proc.setParam ("style", (float) i); proc.presetDirty = true; repaint(); return; }
    if (autoChip().contains (p)) { toggleParam ("autoRel"); proc.presetDirty = true; releaseKnob.repaint(); repaint(); return; }
    for (int i = 0; i < 6; ++i)
        if (outChip (i).contains (p))
        {
            if (i == 1) showOsMenu();
            else if (i == 4) showDitherMenu();
            else { toggleParam (outIds[i]); if (i == 0) ceilKnob.repaint(); if (i == 5) display.repaint(); }
            repaint();
            return;
        }
}

void LmixEditor::prevNextPreset (int dir)
{
    const int n = (int) LmixProcessor::factoryPresets().size();
    const int cur = proc.currentPreset.load();
    proc.loadFactoryPreset (((cur < 0 ? 0 : cur) + dir + n) % n);
    repaint();
}

static File presetFolder()
{
    auto f = File::getSpecialLocation (File::userDocumentsDirectory).getChildFile ("Nine Hands DSP").getChildFile ("LMix");
    f.createDirectory();
    return f;
}

void LmixEditor::savePreset()
{
    chooser = std::make_unique<FileChooser> ("Save LMix preset", presetFolder().getChildFile (proc.presetName + ".lmix"), "*.lmix");
    chooser->launchAsync (FileBrowserComponent::saveMode | FileBrowserComponent::canSelectFiles | FileBrowserComponent::warnAboutOverwriting,
                          [this] (const FileChooser& fc)
    {
        auto f = fc.getResult();
        if (f == File()) return;
        f = f.withFileExtension ("lmix");
        if (auto xml = proc.savePresetTree().createXml()) xml->writeTo (f);
        proc.presetName = f.getFileNameWithoutExtension();
        proc.presetDirty = false; proc.currentPreset = -1;
        repaint();
    });
}

void LmixEditor::loadPreset()
{
    chooser = std::make_unique<FileChooser> ("Load LMix preset", presetFolder(), "*.lmix");
    chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles, [this] (const FileChooser& fc)
    {
        const auto f = fc.getResult();
        if (! f.existsAsFile()) return;
        if (auto xml = XmlDocument::parse (f))
            proc.loadPresetTree (ValueTree::fromXml (*xml), f.getFileNameWithoutExtension());
        repaint();
    });
}

void LmixEditor::showStatus (const String& s) { status = s; statusTime = nowS(); }

// ------------------------------------------------------------------ menus (toujours ouverts là où on clique)
PopupMenu::Options LmixEditor::menuAt() const { return PopupMenu::Options().withMousePosition(); }

void LmixEditor::showParamMidiMenu (RangedAudioParameter* p, const String& title)
{
    if (p == nullptr) return;
    const int pi = proc.paramIndexOf (p->paramID);
    if (pi < 0) return;
    const int cc = proc.ccForParam (pi);
    PopupMenu m; m.setLookAndFeel (&menuLnf);
    LmixMenu::addHeader (m, fonts, LmixMenu::icoMidi, col::accent, title, cc >= 0 ? "MIDI CC " + String (cc) : String ("No MIDI CC"));
    m.addItem (1, "MIDI Learn", true, proc.learnParam.load() == pi);
    if (cc >= 0) m.addItem (2, "Clear MIDI");
    if (proc.learnParam.load() >= 0) m.addItem (3, "Cancel MIDI learn");
    m.showMenuAsync (menuAt(), [this, pi] (int r)
    {
        if (r == 1) proc.learnParam = pi;
        else if (r == 2) proc.clearMidiFor (pi);
        else if (r == 3) proc.learnParam = -1;
    });
}

void LmixEditor::showReleaseMenu()
{
    auto* p = proc.apvts.getParameter ("release");
    const int pi = proc.paramIndexOf ("release"), pa = proc.paramIndexOf ("autoRel");
    PopupMenu m; m.setLookAndFeel (&menuLnf);
    LmixMenu::addHeader (m, fonts, LmixMenu::icoMidi, col::accent, "Release", p->getCurrentValueAsText() + " ms");
    m.addItem (10, "Auto release (program dependent)", true, proc.prm.autoRel->load() > 0.5f);
    m.addSeparator();
    PopupMenu midi;
    midi.addItem (1, "MIDI Learn: Release" + (proc.ccForParam (pi) >= 0 ? "  (CC " + String (proc.ccForParam (pi)) + ")" : String()), true, proc.learnParam.load() == pi);
    midi.addItem (2, "MIDI Learn: Auto release" + (proc.ccForParam (pa) >= 0 ? "  (CC " + String (proc.ccForParam (pa)) + ")" : String()), true, proc.learnParam.load() == pa);
    if (proc.ccForParam (pi) >= 0 || proc.ccForParam (pa) >= 0) midi.addItem (3, "Clear MIDI");
    if (proc.learnParam.load() >= 0) midi.addItem (4, "Cancel MIDI learn");
    m.addSubMenu ("MIDI", midi);
    m.showMenuAsync (menuAt(), [this, pi, pa] (int r)
    {
        if (r == 10) { toggleParam ("autoRel"); proc.presetDirty = true; releaseKnob.repaint(); repaint(); }
        else if (r == 1) proc.learnParam = pi;
        else if (r == 2) proc.learnParam = pa;
        else if (r == 3) { proc.clearMidiFor (pi); proc.clearMidiFor (pa); }
        else if (r == 4) proc.learnParam = -1;
    });
}

void LmixEditor::showStyleMenu()
{
    const int pi = proc.paramIndexOf ("style");
    PopupMenu m; m.setLookAndFeel (&menuLnf);
    LmixMenu::addHeader (m, fonts, LmixMenu::icoShape, col::accent, "Style", lmix::styleName ((int) proc.prm.style->load()));
    for (int i = 0; i < lmix::numStyles; ++i) m.addItem (100 + i, lmix::styleName (i), true, (int) proc.prm.style->load() == i);
    m.addSeparator();
    PopupMenu midi;
    midi.addItem (1, "MIDI Learn" + (proc.ccForParam (pi) >= 0 ? "  (CC " + String (proc.ccForParam (pi)) + ")" : String()), true, proc.learnParam.load() == pi);
    if (proc.ccForParam (pi) >= 0) midi.addItem (2, "Clear MIDI");
    m.addSubMenu ("MIDI", midi);
    m.showMenuAsync (menuAt(), [this, pi] (int r)
    {
        if (r >= 100) { proc.setParam ("style", (float) (r - 100)); proc.presetDirty = true; repaint(); }
        else if (r == 1) proc.learnParam = pi;
        else if (r == 2) proc.clearMidiFor (pi);
    });
}

void LmixEditor::showOsMenu()
{
    PopupMenu m; m.setLookAndFeel (&menuLnf);
    LmixMenu::addHeader (m, fonts, LmixMenu::icoSlope, col::accent, "Oversampling", "Linear phase, latency changes");
    const int cur = (int) proc.prm.os->load();
    const double fs = jmax (1.0, proc.currentRate.load());
    for (int i = 0; i < 6; ++i)
        m.addItem (100 + i, String (LmixProcessor::osFactor (i)) + "x   (" + String (1000.0 * proc.latencyFor (i) / fs, 1) + " ms)", true, cur == i);
    m.showMenuAsync (menuAt(), [this] (int r) { if (r >= 100) { proc.setParam ("os", (float) (r - 100)); proc.presetDirty = true; repaint(); } });
}

void LmixEditor::showDitherMenu()
{
    PopupMenu m; m.setLookAndFeel (&menuLnf);
    LmixMenu::addHeader (m, fonts, LmixMenu::icoRange, col::accent, "Dither", "TPDF, last stage");
    const int cur = (int) proc.prm.dither->load();
    m.addItem (100, "Off", true, cur == 0);
    m.addItem (101, "16 bit (CD)", true, cur == 1);
    m.addItem (102, "24 bit", true, cur == 2);
    m.addSeparator();
    m.addItem (110, "Noise shaping", true, proc.prm.shaping->load() > 0.5f);
    m.showMenuAsync (menuAt(), [this] (int r)
    {
        if (r >= 100 && r < 103) proc.setParam ("dither", (float) (r - 100));
        else if (r == 110) toggleParam ("shaping");
        repaint();
    });
}

void LmixEditor::showTargetMenu()
{
    PopupMenu m; m.setLookAndFeel (&menuLnf);
    LmixMenu::addHeader (m, fonts, LmixMenu::icoAnalyzer, col::accent, "Loudness target", "Integrated " + (meters.integrated > -199 ? String (meters.integrated, 1) + " LUFS" : String ("not measured yet")));
    const auto& t = LmixProcessor::targets();
    for (int i = 0; i < (int) t.size(); ++i) m.addItem (100 + i, t[(size_t) i].name, true, proc.targetIdx.load() == i);
    m.addSeparator();
    m.addItem (200, "Match gain to target", meters.integrated > -199);
    m.addItem (201, "Reset loudness meters");
    m.showMenuAsync (menuAt(), [this] (int r)
    {
        if (r >= 100 && r < 200) proc.targetIdx = r - 100;
        else if (r == 200 && meters.integrated > -199)
        {
            const auto& tg = LmixProcessor::targets()[(size_t) proc.targetIdx.load()];
            const float ng = jlimit (-12.0f, 30.0f, proc.prm.gain->load() + (float) (tg.lufs - meters.integrated));
            proc.setParam ("gain", ng);
            proc.resetMeters = true;
            showStatus ("GAIN " + dbStr (ng) + "  (TARGET " + String (tg.lufs, 0) + " LUFS)");
        }
        else if (r == 201) { proc.resetMeters = true; showStatus ("LOUDNESS RESET"); }
        repaint();
    });
}

void LmixEditor::showMainMenu()
{
    PopupMenu m; m.setLookAndFeel (&menuLnf);
    LmixMenu::addHeader (m, fonts, LmixMenu::icoAnalyzer, col::accent, "LMix", "Mastering limiter");
    PopupMenu pres, stl, os, dith, tgt, hist, midi, size, gl;
    const auto& fp = LmixProcessor::factoryPresets();
    for (int i = 0; i < (int) fp.size(); ++i) pres.addItem (1 + i, fp[(size_t) i].name, true, proc.currentPreset.load() == i && ! proc.presetDirty.load());
    pres.addSeparator();
    pres.addItem (90, "Save preset...");
    pres.addItem (91, "Load preset...");
    for (int i = 0; i < lmix::numStyles; ++i) stl.addItem (100 + i, lmix::styleName (i), true, (int) proc.prm.style->load() == i);
    for (int i = 0; i < 6; ++i) os.addItem (110 + i, String (LmixProcessor::osFactor (i)) + "x", true, (int) proc.prm.os->load() == i);
    static const char* dn[3] = { "Off", "16 bit (CD)", "24 bit" };
    for (int i = 0; i < 3; ++i) dith.addItem (120 + i, dn[i], true, (int) proc.prm.dither->load() == i);
    dith.addSeparator();
    dith.addItem (123, "Noise shaping", true, proc.prm.shaping->load() > 0.5f);
    const auto& t = LmixProcessor::targets();
    for (int i = 0; i < (int) t.size(); ++i) tgt.addItem (130 + i, t[(size_t) i].name, true, proc.targetIdx.load() == i);
    for (int i = 0; i < 3; ++i) hist.addItem (150 + i, String (i == 0 ? 3 : i == 1 ? 6 : 12) + " seconds", true, proc.historyIdx.load() == i);
    midi.addItem (160, "Right-click a knob or button to learn", false, false);
    if (proc.learnParam.load() >= 0) midi.addItem (161, "Cancel MIDI learn");
    midi.addItem (162, "Clear all MIDI");
    static const char* sz[3] = { "60 %", "75 %", "100 %" };
    const int defSz = nhsize::defaultIndex ("LMix", 3);
    const int cur = nhsize::openIndex ("LMix", proc.uiSizeIdx.load(), 3);
    PopupMenu defSizes;
    for (int i = 0; i < 3; ++i)
    {
        size.addItem (170 + i, String (sz[i]) + (i == defSz ? "  (default)" : ""), true, cur == i);
        defSizes.addItem (nhsize::idDefault + i, sz[i], true, i == defSz);
    }
    size.addSeparator();
    size.addSubMenu ("Default size", defSizes);     // taille des nouvelles fenêtres, commune à toutes les instances
    for (int i = 0; i < 4; ++i) gl.addItem (180 + i, String ((i + 1) * 25) + " %", true, proc.glowIdx.load() == i);
    m.addSubMenu ("Presets", pres);
    m.addSubMenu ("Style", stl);
    m.addSubMenu ("Oversampling", os);
    m.addItem (140, "True peak limiting", true, proc.prm.truePeak->load() > 0.5f);
    m.addItem (141, "Multiband", true, proc.prm.mb->load() > 0.5f);
    m.addItem (142, "DC offset filter", true, proc.prm.dc->load() > 0.5f);
    m.addSubMenu ("Dither", dith);
    m.addSeparator();
    m.addSubMenu ("Loudness target", tgt);
    m.addItem (143, "Reset loudness meters");
    m.addSubMenu ("History length", hist);
    m.addSeparator();
    m.addSubMenu ("MIDI", midi);
    m.addSubMenu ("Plugin size", size);
    m.addSubMenu ("Glow intensity", gl);
    m.showMenuAsync (menuAt(), [this] (int r)
    {
        if (r >= 1 && r < 90) proc.loadFactoryPreset (r - 1);
        else if (r == 90) savePreset();
        else if (r == 91) loadPreset();
        else if (r >= 100 && r < 108) { proc.setParam ("style", (float) (r - 100)); proc.presetDirty = true; }
        else if (r >= 110 && r < 116) { proc.setParam ("os", (float) (r - 110)); proc.presetDirty = true; }
        else if (r >= 120 && r < 123) proc.setParam ("dither", (float) (r - 120));
        else if (r == 123) toggleParam ("shaping");
        else if (r >= 130 && r < 140) proc.targetIdx = r - 130;
        else if (r == 140) toggleParam ("truePeak");
        else if (r == 141) toggleParam ("mb");
        else if (r == 142) toggleParam ("dc");
        else if (r == 143) { proc.resetMeters = true; showStatus ("LOUDNESS RESET"); }
        else if (r >= 150 && r < 153) proc.historyIdx = r - 150;
        else if (r == 161) proc.learnParam = -1;
        else if (r == 162) for (auto& x : proc.ccMap) x = -1;
        else if (r >= 170 && r < 173) { proc.uiSizeIdx = r - 170; setScaleIndex (r - 170); }
        else if (r >= nhsize::idDefault && r < nhsize::idDefault + 3) { nhsize::setDefaultIndex ("LMix", r - nhsize::idDefault); proc.uiSizeIdx = -1; setScaleIndex (r - nhsize::idDefault); }
        else if (r >= 180 && r < 184) proc.glowIdx = r - 180;
        repaint(); display.repaint(); meters.repaint();
    });
}

// ------------------------------------------------------------------ anti-gel v2
void LmixEditor::useSoftwareRenderer()
{
    auto* peer = getPeer();
    if ((void*) peer != lastPeer) { lastPeer = (void*) peer; repaint(); }
    lastRendererCheck = nowS();
   #if JUCE_WINDOWS
    if (peer != nullptr)
    {
        int sw = peer->getAvailableRenderingEngines().indexOf ("Software Renderer");
        if (sw < 0) sw = 0;
        if (peer->getCurrentRenderingEngine() != sw) peer->setCurrentRenderingEngine (sw);
    }
   #endif
}

void LmixEditor::trackHostWindow()
{
    if (auto* peer = getPeer())
    {
        void* handle = peer->getNativeHandle();
        if (handle != nullptr && ! hostLock.isAttachedTo (handle))
        {
            Component::SafePointer<Component> top (&peer->getComponent());
            hostLock.attach (handle, [top] { return top != nullptr ? top->getWidth() : 0; });
            hostLock.setMinimumSize (kW, kH);
            hostLock.setMaximumSize (kMaxW, kMaxH);
            hostLock.enforceNow();
        }
        if (getWidth() < kW - 2) hostLock.enforceNow();
    }
    else hostLock.detach();
}

void LmixEditor::parentHierarchyChanged() { useSoftwareRenderer(); trackHostWindow(); }
void LmixEditor::visibilityChanged()      { useSoftwareRenderer(); if (isShowing()) repaint(); }

void LmixEditor::askRepaint (Rectangle<int> r)
{
    if (pendingSince <= 0.0) pendingSince = nowS();
    repaint (r);
}

// ------------------------------------------------------------------ 30 Hz
void LmixEditor::timerCallback()
{
    const double t = nowS();
    if ((void*) getPeer() != lastPeer || t - lastRendererCheck > 1.0) { useSoftwareRenderer(); trackHostWindow(); }
    if (pendingSince > 0.0 && t - pendingSince > 1.5 && isShowing())                 // chien de garde
    {
        lastPeer = nullptr; useSoftwareRenderer(); repaint(); pendingSince = 0.0;
    }

    // changements de réglages (souris, automation, MIDI) -> ligne d'état
    auto& params = proc.getParameters();
    bool facade = false;
    for (int i = 0; i < params.size() && i < (int) lastVals.size(); ++i)
    {
        const float v = params[i]->getValue();
        if (std::abs (v - lastVals[(size_t) i]) > 1.0e-5f)
        {
            lastVals[(size_t) i] = v;
            auto* rp = dynamic_cast<RangedAudioParameter*> (params[i]);
            if (rp == nullptr) continue;
            const String id = rp->paramID;
            String txt = rp->getName (40).toUpperCase() + "  ";
            if (dynamic_cast<AudioParameterBool*> (rp) != nullptr) txt << (v > 0.5f ? "ON" : "OFF");
            else if (id == "gain") txt << dbStr (rp->convertFrom0to1 (v));
            else txt << rp->getCurrentValueAsText() << (rp->getLabel().isNotEmpty() ? " " + rp->getLabel() : String());
            if (id == "power") txt = v > 0.5f ? "LMIX ON" : "LMIX BYPASSED";
            showStatus (txt);
            if (id != "gain" && id != "attack" && id != "release" && id != "lookahead" && id != "linkTrans" && id != "linkRel"
                && id != "transient" && id != "enhance" && id != "clip") facade = true;
            if (id != "power") proc.presetDirty = true;
            if (id == "truePeak") ceilKnob.repaint();
            if (id == "autoRel") releaseKnob.repaint();
        }
    }
    if (proc.glowIdx.load() != lastGlow) { lastGlow = proc.glowIdx.load(); facade = true; }
    if (proc.latencyNow.load() != lastLatency) { lastLatency = proc.latencyNow.load(); facade = true; }
    if (facade) askRepaint (getLocalBounds());

    // MIDI Learn
    const int lp = proc.learnParam.load();
    if (lp >= 0) showStatus ("MIDI LEARN  " + proc.getParameters()[lp]->getName (40));
    const int ll = proc.lastLearnedCc.exchange (-1);
    if (ll >= 0)
    {
        const int pi = proc.ccMap[ll].load();
        if (pi >= 0) showStatus ("CC " + String (ll) + "  " + proc.getParameters()[pi]->getName (40));
    }

    // fondus : ligne d'état et interrupteur
    const float target = (t - statusTime < 2.5) ? 1.0f : 0.0f;
    const float na = target > statusAlpha ? jmin (target, statusAlpha + 0.28f) : jmax (target, statusAlpha - 0.11f);   // immobile une fois arrivé (sinon 100 / 89 % à chaque image = scintillement)
    if (std::abs (na - statusAlpha) > 0.001f || (na > 0.0f && status != lastStatusDrawn)) { statusAlpha = na; lastStatusDrawn = status; askRepaint (pill().expanded (4).toNearestInt()); }
    const bool pw = proc.prm.power->load() > 0.5f;
    const float pg = pw ? jmin (1.0f, powerGlow + 0.45f) : jmax (0.0f, powerGlow - 0.28f);
    if (std::abs (pg - powerGlow) > 0.001f) { powerGlow = pg; askRepaint (powerRect().expanded (powerRect().getWidth()).toNearestInt()); }

    display.tick();
    meters.tick();
    for (auto* k : { &gainKnob, &ceilKnob, &attackKnob, &releaseKnob, &lookKnob, &linkTKnob, &linkRKnob, &transKnob, &enhKnob, &clipKnob }) k->tick();
    pills.update();
}
