// Vérifications du moteur + captures de l'interface de LMix (hors plugin) : tools/render
#include "PluginEditor.h"
#include <random>
using namespace juce;

//------------------------------------------------------------------ matière de test : batterie, basse, nappe, charlestons (stéréo)
struct TestMusic
{
    double fs; std::mt19937 rng { 11 }; std::normal_distribution<double> nd { 0.0, 1.0 };
    double t = 0, bassPh = 0, padPh[3] {}, snLp = 0, hatHp = 0, hatX = 0, bLp = 0;
    explicit TestMusic (double f) : fs (f) {}
    void next (float& L, float& R)
    {
        const double bpm = 124.0, beat = t * bpm / 60.0, b = std::fmod (beat, 1.0);
        const int bar16 = (int) std::floor (beat * 4.0) % 16;
        // grosse caisse
        const double kick = std::exp (-b * 9.0) * std::sin (2 * MathConstants<double>::pi * (48.0 * b + 140.0 * (1 - std::exp (-b * 30.0)) / 30.0)) * 0.9;
        // caisse claire (temps 2 et 4)
        const int beatN = (int) std::floor (beat) % 4;
        double sn = 0;
        if (beatN == 1 || beatN == 3) { snLp += 0.35 * (nd (rng) - snLp); sn = (snLp * 0.9 + std::sin (2 * MathConstants<double>::pi * 190 * b) * 0.4) * std::exp (-b * 14.0) * 0.8; }
        // charlestons (doubles croches)
        const double h16 = std::fmod (beat * 4.0, 1.0);
        const double w = nd (rng); const double hp = w - hatX; hatX = w; hatHp = 0.6 * hatHp + 0.4 * hp;
        const double hat = hatHp * std::exp (-h16 * 30.0) * (bar16 % 2 ? 0.18 : 0.28);
        // basse
        static const double notes[4] = { 41.2, 41.2, 49.0, 36.7 };
        bassPh += notes[(int) std::floor (beat / 4.0) % 4] / fs; bassPh -= std::floor (bassPh);
        const double saw = 2.0 * bassPh - 1.0; bLp += 0.08 * (saw - bLp);
        const double bass = bLp * 0.55 * (0.7 + 0.3 * std::exp (-h16 * 4.0));
        // nappe
        double pad = 0; static const double pf[3] = { 220.0, 277.2, 329.6 };
        for (int k = 0; k < 3; ++k) { padPh[k] += pf[k] / fs; pad += std::sin (2 * MathConstants<double>::pi * padPh[k]) * 0.06; }
        const double m = kick + sn + bass + pad;
        L = (float) (m + hat * 1.0 + pad * 0.3); R = (float) (m + hat * 0.6 - pad * 0.3);
        t += 1.0 / fs;
    }
};

static double ituTP (const std::vector<float>& x)
{
    double m = 0; float xs[12];
    for (size_t n = 12; n < x.size(); ++n) { for (int k = 0; k < 12; ++k) xs[k] = x[n - 11 + k]; m = jmax (m, (double) std::abs (x[n]), lmix::ItuTruePeak::peakBetween (xs)); }
    return m;
}
static double ownTP (const std::vector<float>& x)
{
    lmix::TruePeak4x tp; double m = 0; float xs[24];
    for (size_t n = 24; n < x.size(); ++n) { for (int k = 0; k < 24; ++k) xs[k] = x[n - 23 + k]; m = jmax (m, (double) std::abs (x[n]), tp.peakBetween (xs)); }
    return m;
}


//------------------------------------------------------------------ analyse de qualité (tools/render analyse)
static double thdN (const std::vector<float>& y, double fs, double f0, double fmax = 20000.0)
{
    const int order = 16, N = 1 << order;
    dsp::FFT fft (order);
    std::vector<float> buf ((size_t) N * 2, 0.0f);
    const size_t off = y.size() - (size_t) N;
    for (int n = 0; n < N; ++n)
    {
        const double w = 0.35875 - 0.48829 * std::cos (2 * MathConstants<double>::pi * n / N) + 0.14128 * std::cos (4 * MathConstants<double>::pi * n / N) - 0.01168 * std::cos (6 * MathConstants<double>::pi * n / N);
        buf[(size_t) n] = (float) (y[off + (size_t) n] * w);
    }
    fft.performFrequencyOnlyForwardTransform (buf.data());
    const double binHz = fs / N; const int k0 = (int) std::round (f0 / binHz);
    double sig = 0, rest = 0;
    for (int k = (int) (15.0 / binHz); k < (int) (fmax / binHz); ++k)
    {
        const double p = (double) buf[(size_t) k] * buf[(size_t) k];
        if (std::abs (k - k0) <= 6) sig += p; else rest += p;
    }
    return 10 * std::log10 (rest / sig + 1e-30);
}

static void setPa (LmixProcessor& p, const char* id, float v) { auto* q = p.apvts.getParameter (id); q->setValueNotifyingHost (q->convertTo0to1 (v)); }

static std::vector<float> runSine (int style, int os, double f, double overDb, double fs, int B, double secs = 4.0)
{
    LmixProcessor p; p.setPlayConfigDetails (2, 2, fs, B);
    setPa (p, "style", (float) style); setPa (p, "os", (float) os); setPa (p, "ceiling", -1.0f);
    p.prepareToPlay (fs, B);
    MidiBuffer midi; AudioBuffer<float> buf (2, B); std::vector<float> out;
    const double a = Decibels::decibelsToGain (-1.0 + overDb); double ph = 0;
    for (int k = 0; k < (int) (secs * fs / B); ++k)
    {
        for (int i = 0; i < B; ++i) { const float v = (float) (a * std::sin (ph)); ph += 2 * MathConstants<double>::pi * f / fs; buf.setSample (0, i, v); buf.setSample (1, i, v); }
        p.processBlock (buf, midi);
        for (int i = 0; i < B; ++i) out.push_back (buf.getSample (0, i));
    }
    return out;
}

static void analyse (bool quick = false)
{
    const double fs = 48000.0; const int B = 512;
    if (! quick) {
    std::cout << "== THD+N, sinus 6 dB au-dessus du plafond, 4x ==\n";
    for (int st = 0; st < lmix::numStyles; ++st)
    {
        std::cout << "  " << String (lmix::styleName (st)).paddedRight (' ', 11);
        for (double f : { 40.0, 100.0, 1000.0 }) std::cout << "  " << f << " Hz " << String (thdN (runSine (st, 2, f, 6.0, fs, B), fs, f), 1) << " dB";
        std::cout << "\n";
    }
    std::cout << "== Repliement : sinus 9 kHz +6 dB (Modern), THD+N 20 Hz-20 kHz selon OS ==\n ";
    for (int os = 0; os < 6; ++os) std::cout << "  " << LmixProcessor::osFactor (os) << "x " << String (thdN (runSine (6, os, 9000.0, 6.0, fs, B), fs, 9000.0), 1);
    std::cout << " dB\n";
    }
    std::cout << "== Musique +10 dB : activite de la securite, multibande, CPU ==\n";
    for (int os : { 0, 2, 3, 5 })
        for (int mb : { 0, 1 })
        for (int stl : { 6, 0 })
        {
            if (stl == 0 && (mb == 1 || os == 5)) continue;
            if (quick && (os != 2 || stl != 6)) continue;
            LmixProcessor p; p.setPlayConfigDetails (2, 2, fs, B);
            setPa (p, "gain", 10.0f); setPa (p, "os", (float) os); setPa (p, "mb", (float) mb); setPa (p, "style", (float) stl);
            p.prepareToPlay (fs, B);
            TestMusic tm (fs); MidiBuffer midi; AudioBuffer<float> buf (2, B);
            // énergie au-dessus de 3 kHz : entrée (x gain) / sortie
            double eiH = 0, eoH = 0, eiA = 0, eoA = 0; double hpI = 0, hpO = 0, xi1 = 0, xo1 = 0;
            const double aHp = std::exp (-2 * MathConstants<double>::pi * 3000.0 / fs);
            std::vector<float> dly ((size_t) p.getLatencySamples() + 1, 0.0f); size_t dp = 0;
            double cpu = 0; const int blocks = (int) (10 * fs / B); double bgr[4] {}, mgr = 0; int nb = 0;
            for (int k = 0; k < blocks; ++k)
            {
                float in[B];
                for (int i = 0; i < B; ++i) { float a, b; tm.next (a, b); buf.setSample (0, i, a); buf.setSample (1, i, b); in[i] = a * 3.1623f; }
                const double t0 = Time::getMillisecondCounterHiRes();
                p.processBlock (buf, midi);
                cpu += Time::getMillisecondCounterHiRes() - t0;
                if (k > 40) { for (int q = 0; q < 4; ++q) bgr[q] += p.bandGr[q].load(); mgr += p.grNow.load(); ++nb; }
                for (int i = 0; i < B; ++i)
                {
                    const float xi = dly[dp]; dly[dp] = in[i]; dp = (dp + 1) % dly.size();
                    const double yo = buf.getSample (0, i);
                    hpI = aHp * (hpI + xi - xi1); xi1 = xi; hpO = aHp * (hpO + yo - xo1); xo1 = yo;
                    if (k > 40) { eiH += hpI * hpI; eoH += hpO * hpO; eiA += xi * xi; eoA += yo * yo; }
                }
            }
            uint32_t h[lmix::Loudness::kBins]; lmix::Loudness::snapshot (p.loud.hist, h);
            std::cout << "  " << String (lmix::styleName (stl)).paddedRight (' ', 11) << LmixProcessor::osFactor (os) << "x MB " << mb << "  securite active " << String (100.0 * p.statSafetyActive / jmax ((int64_t) 1, p.statSamples), 2)
                      << " % (> 0,1 dB " << String (100.0 * p.statSafety01 / jmax ((int64_t) 1, p.statSamples), 2) << " %, moy " << String (p.statSafetySum / jmax ((int64_t) 1, p.statSafetyActive), 2) << ", max " << String (p.statSafetyMax, 2) << " dB)  sonie " << String (lmix::Loudness::integrated (h), 2)
                      << " LUFS  reduction totale " << String (10 * std::log10 (eoA / eiA), 2) << " dB, aigus > 3 kHz " << String (10 * std::log10 (eoH / eiH), 2)
                      << " dB  CPU " << String (cpu / 100.0, 1) << " %\n";
            if (mb) std::cout << "      bandes " << String (bgr[0] / nb, 1) << " / " << String (bgr[1] / nb, 1) << " / " << String (bgr[2] / nb, 1) << " / " << String (bgr[3] / nb, 1) << " dB, limiteur " << String (mgr / nb, 1) << " dB\n";
            else std::cout << "      limiteur " << String (mgr / nb, 1) << " dB\n";
        }
}

static void snap (Component& c, const String& name, float scale)
{
    auto img = c.createComponentSnapshot (c.getLocalBounds(), true, scale);
    File out = File::getCurrentWorkingDirectory().getChildFile (name);
    out.deleteFile();
    FileOutputStream os (out); PNGImageFormat png; png.writeImageToStream (img, os);
    std::cout << "ecrit " << out.getFullPathName() << " (" << img.getWidth() << "x" << img.getHeight() << ")\n";
}

static void setP (LmixProcessor& p, const char* id, float v) { auto* q = p.apvts.getParameter (id); q->setValueNotifyingHost (q->convertTo0to1 (v)); }

int main (int argc, char** argv)
{
    ScopedJuceInitialiser_GUI init;
    if (argc > 1 && String (argv[1]) == "analyse") { analyse(); return 0; }
    if (argc > 1 && String (argv[1]) == "mb") { analyse (true); return 0; }
    const bool shotsOnly = argc > 1;
    const double fs = 48000.0; const int B = 512;
    MidiBuffer midi;

    //-------------------------------------------------------------- 1. plafond garanti (plugin complet), par suréchantillonnage et style
    std::cout << "== Plafond (-1.0 dBTP, gain +14 dB) ==\n";
    if (! shotsOnly)
    for (int os : { 0, 2, 3, 5 })
        for (int st : { 0, 1, 3, 6, 7 })
        {
            LmixProcessor p; p.setPlayConfigDetails (2, 2, fs, B);
            setP (p, "gain", 14.0f); setP (p, "ceiling", -1.0f); setP (p, "style", (float) st); setP (p, "os", (float) os);
            if (st == 7) setP (p, "clip", 50.0f);
            if (st == 1) setP (p, "mb", 1.0f);
            p.prepareToPlay (fs, B);
            TestMusic tm (fs); std::vector<float> oL, oR; AudioBuffer<float> buf (2, B);
            const int blocks = (int) (8.0 * fs / B);
            const double t0 = Time::getMillisecondCounterHiRes();
            for (int k = 0; k < blocks; ++k)
            {
                for (int i = 0; i < B; ++i) { float a, b; tm.next (a, b); buf.setSample (0, i, a); buf.setSample (1, i, b); }
                p.processBlock (buf, midi);
                if (k > 20) for (int i = 0; i < B; ++i) { oL.push_back (buf.getSample (0, i)); oR.push_back (buf.getSample (1, i)); }
            }
            const double cpu = (Time::getMillisecondCounterHiRes() - t0) / (8.0 * 1000.0) * 100.0;
            double sp = 0; for (size_t i = 0; i < oL.size(); ++i) sp = jmax (sp, (double) std::abs (oL[i]), (double) std::abs (oR[i]));
            uint32_t h[lmix::Loudness::kBins]; lmix::Loudness::snapshot (p.loud.hist, h);
            std::cout << "  " << LmixProcessor::osFactor (os) << "x  " << String (lmix::styleName (st)).paddedRight (' ', 11) << (st == 1 ? " +MB" : "    ")
                      << "  crete " << String (lmix::gainToDb (sp), 3) << "  TP BS.1770 " << String (lmix::gainToDb (jmax (ituTP (oL), ituTP (oR))), 3)
                      << "  TP 8x " << String (lmix::gainToDb (jmax (ownTP (oL), ownTP (oR))), 3) << "  sonie " << String (lmix::Loudness::integrated (h), 1)
                      << " LUFS  latence " << p.getLatencySamples() << "  CPU " << String (cpu, 1) << " %\n";
        }

    //-------------------------------------------------------------- 2. latence : bypass = entrée retardée de la latence annoncée ; DELTA sans réduction
    {
        LmixProcessor p; p.setPlayConfigDetails (2, 2, fs, B);
        setP (p, "os", 2.0f); setP (p, "power", 0.0f);
        p.prepareToPlay (fs, B);
        AudioBuffer<float> warm (2, B); for (int k = 0; k < 20; ++k) { warm.clear(); p.processBlock (warm, midi); }
        AudioBuffer<float> imp (2, B * 8); imp.clear(); imp.setSample (0, 100, 1.0f);
        for (int s0 = 0; s0 < B * 8; s0 += B) { AudioBuffer<float> blk (imp.getArrayOfWritePointers(), 2, s0, B); p.processBlock (blk, midi); }
        int pk = 0; for (int i = 0; i < B * 8; ++i) if (std::abs (imp.getSample (0, i)) > std::abs (imp.getSample (0, pk))) pk = i;
        std::cout << "Bypass : impulsion ressortie a " << pk - 100 << " echantillons (latence annoncee " << p.getLatencySamples() << ")\n";
        // chemin traité, niveau bas (aucune réduction) : sinus 1 kHz -> même retard, même niveau
        setP (p, "power", 1.0f);
        for (int k = 0; k < 20; ++k) { warm.clear(); p.processBlock (warm, midi); }
        AudioBuffer<float> x (2, B * 40); double in2 = 0, outE = 0, diff = 0;
        for (int i = 0; i < B * 40; ++i) { const float v = (float) (0.1 * std::sin (2 * MathConstants<double>::pi * 1000.0 * i / fs)); x.setSample (0, i, v); x.setSample (1, i, v); }
        AudioBuffer<float> y; y.makeCopyOf (x);
        for (int s0 = 0; s0 < B * 40; s0 += B) { AudioBuffer<float> blk (y.getArrayOfWritePointers(), 2, s0, B); p.processBlock (blk, midi); }
        const int L = p.getLatencySamples();
        for (int i = B * 20; i < B * 40; ++i) { const double a = x.getSample (0, i - L), b = y.getSample (0, i); in2 += a * a; outE += b * b; diff += (a - b) * (a - b); }
        std::cout << "Traitement sans reduction : gain " << String (10 * std::log10 (outE / in2), 3) << " dB, ecart a l'entree retardee " << String (10 * std::log10 (diff / in2 + 1e-30), 1) << " dB\n";
        setP (p, "delta", 1.0f);
        for (int k = 0; k < 20; ++k) { warm.clear(); p.processBlock (warm, midi); }
        AudioBuffer<float> z; z.makeCopyOf (x); double dz = 0;
        for (int s0 = 0; s0 < B * 40; s0 += B) { AudioBuffer<float> blk (z.getArrayOfWritePointers(), 2, s0, B); p.processBlock (blk, midi); }
        for (int i = B * 20; i < B * 40; ++i) dz += z.getSample (0, i) * z.getSample (0, i);
        std::cout << "DELTA sans reduction : residu " << String (10 * std::log10 (dz / in2 + 1e-30), 1) << " dB\n";
    }

    //-------------------------------------------------------------- 3. sonie : sinus 997 Hz -23 dBFS stéréo -> -23 LUFS
    {
        LmixProcessor p; p.setPlayConfigDetails (2, 2, fs, B); setP (p, "ceiling", 0.0f); p.prepareToPlay (fs, B);
        AudioBuffer<float> x (2, B); double ph = 0;
        for (int k = 0; k < (int) (12 * fs / B); ++k)
        {
            for (int i = 0; i < B; ++i) { const float v = (float) (Decibels::decibelsToGain (-23.0) * std::sin (ph)); ph += 2 * MathConstants<double>::pi * 997.0 / fs; x.setSample (0, i, v); x.setSample (1, i, v); }
            p.processBlock (x, midi);
        }
        uint32_t h[lmix::Loudness::kBins]; lmix::Loudness::snapshot (p.loud.hist, h);
        std::cout << "Sonie : sinus 997 Hz -23 dBFS -> integree " << String (lmix::Loudness::integrated (h), 2) << " LUFS, court terme " << String (p.shortTerm.load(), 2) << "\n";
    }

    //-------------------------------------------------------------- latences
    for (double f : { 44100.0, 48000.0, 96000.0 })
    {
        LmixProcessor p; p.setPlayConfigDetails (2, 2, f, B); p.prepareToPlay (f, B);
        std::cout << "Latence a " << f << " Hz :";
        for (int i = 0; i < 6; ++i) std::cout << "  " << LmixProcessor::osFactor (i) << "x " << p.latencyFor (i) << " (" << String (1000.0 * p.latencyFor (i) / f, 1) << " ms)";
        std::cout << "\n";
    }

    //-------------------------------------------------------------- 4. captures
    auto runWith = [&] (LmixProcessor& p, LmixEditor& ed, double secs)
    {
        TestMusic tm (fs); AudioBuffer<float> buf (2, B);
        const int blocks = (int) (secs * fs / B);
        for (int k = 0; k < blocks; ++k)
        {
            for (int i = 0; i < B; ++i) { float a, b; tm.next (a, b); buf.setSample (0, i, a); buf.setSample (1, i, b); }
            p.processBlock (buf, midi);
            if (k % 3 == 0) ed.timerCallback();
        }
        for (int i = 0; i < 4; ++i) ed.timerCallback();
    };
    {
        LmixProcessor p; p.setPlayConfigDetails (2, 2, fs, B);
        p.loadFactoryPreset (1);                                      // Modern Master
        setP (p, "gain", 3.5f);
        p.prepareToPlay (fs, B);
        LmixEditor ed (p); ed.setSize (960, 540);
        runWith (p, ed, 14.0);
        ed.statusTime = -10; ed.statusAlpha = 0;
        snap (ed, "lmix_overview.png", 2.0f);
        ed.showStatus ("GAIN  +3.50 dB"); ed.statusAlpha = 1.0f;
        snap (ed, "lmix_status.png", 2.0f);
    }
    {
        LmixProcessor p; p.setPlayConfigDetails (2, 2, fs, B);
        p.loadFactoryPreset (5);                                      // Metal Master (multibande)
        setP (p, "gain", 5.0f);
        p.prepareToPlay (fs, B);
        LmixEditor ed (p); ed.setSize (960, 540);
        runWith (p, ed, 10.0);
        ed.statusTime = -10; ed.statusAlpha = 0;
        snap (ed, "lmix_multiband.png", 2.0f);
    }
    return 0;
}
