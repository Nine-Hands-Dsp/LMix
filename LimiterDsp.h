#pragma once
#include <cmath>
#include <vector>
#include <array>
#include <algorithm>
#include <cstdint>
#include <atomic>

//==============================================================================
//  LMix (NINE HANDS | DSP) : moteur du limiteur, sans dépendance à JUCE
//
//  Chaîne (par échantillon) :
//    GAIN -> filtre DC -> ENHANCE (compression vers le haut des détails faibles)
//    -> étage multibande (option, 4 bandes Linkwitz-Riley, réduction pondérée par bande)
//    -> suréchantillonnage (dans le processeur) -> écrêteur doux (CLIP)
//    -> limiteur principal à deux enveloppes (rapide + lente) avec avance (lookahead)
//    -> sous-échantillonnage -> étage de sécurité true peak (crêtes entre échantillons, 4x)
//
//  Garantie de plafond : chaque étage « avance » suit la même construction :
//    gain requis r[p] -> minimum glissant sur H échantillons -> deux moyennes glissantes
//    (étendue totale S) -> le gain appliqué à un échantillon ne dépasse jamais le gain requis
//    par aucun échantillon voisin (à W près). Voir LookaheadGain.
//==============================================================================
namespace lmix
{
    constexpr double kPi = 3.14159265358979323846;
    inline double dbToGain (double d) { return std::pow (10.0, d * 0.05); }
    inline double gainToDb (double g) { return 20.0 * std::log10 (std::max (g, 1.0e-12)); }

    //------------------------------------------------------------------ styles de limitation
    enum Style { transparent = 0, punch, dynamic, loud, bus, safe, modern, aggressive, numStyles };
    inline const char* styleName (int s)
    {
        static const char* n[numStyles] = { "Transparent", "Punch", "Dynamic", "Loud", "Bus", "Safe", "Modern", "Aggressive" };
        return n[std::clamp (s, 0, numStyles - 1)];
    }
    inline const char* styleHint (int s)
    {
        static const char* n[numStyles] = {
            "Cleanest, slow leveling, no added colour",
            "Fast recovery after each hit, keeps the kick and snare",
            "Soft knee, keeps the most dynamics",
            "Fast, dense and loud, light internal clipping",
            "Glue for buses and stems, smooth program release",
            "Slow and safe, minimal distortion",
            "Loud and clear modern master, transient aware",
            "Hard, upfront and saturated" };
        return n[std::clamp (s, 0, numStyles - 1)];
    }

    struct StyleSpec
    {
        double kneeDb;        // genou doux (dB) autour du plafond
        double slowAmt;       // part de la réduction confiée à l'enveloppe lente (nivellement)
        double slowAtkMs;     // attaque de l'enveloppe lente (x ATTACK)
        double slowRelMul;    // relâchement lent = RELEASE x slowRelMul
        double fastRelMul;    // relâchement rapide = RELEASE x fastRelMul
        double clipAdd;       // écrêtage interne (0..1), ajouté au potard CLIP
        double transAdd;      // détection de transitoires interne (0..1), ajoutée au potard TRANSIENT
    };
    inline StyleSpec styleSpec (int s)
    {
        static const StyleSpec t[numStyles] = {
            //  knee  slow  sAtk  sRel  fRel  clip  trans
            { 0.0, 0.90, 60.0, 9.0, 0.45, 0.00, 0.00 },  // Transparent
            { 0.0, 0.50, 25.0, 4.0, 0.15, 0.00, 0.60 },  // Punch
            { 3.0, 0.30, 40.0, 6.0, 0.30, 0.00, 0.25 },  // Dynamic
            { 0.0, 0.70, 15.0, 2.5, 0.08, 0.30, 0.10 },  // Loud
            { 2.0, 0.80, 30.0, 3.0, 0.60, 0.00, 0.00 },  // Bus
            { 0.0, 1.00, 80.0, 12.0, 1.20, 0.00, 0.00 }, // Safe
            { 0.0, 0.75, 20.0, 3.5, 0.12, 0.20, 0.40 },  // Modern
            { 0.0, 0.40, 8.0, 1.5, 0.05, 0.50, 0.20 },   // Aggressive
        };
        return t[std::clamp (s, 0, numStyles - 1)];
    }

    //------------------------------------------------------------------ minimum glissant (file monotone, taille fixe)
    class SlidingMin
    {
    public:
        void prepare (int maxLen) { cap = maxLen + 2; idx.assign ((size_t) cap, 0); val.assign ((size_t) cap, 1.0); reset(); }
        void reset() { head = tail = 0; count = 0; t = 0; }
        void setLength (int n) { len = std::max (1, n); }
        double push (double v)
        {
            while (count > 0 && val[(size_t) back()] >= v) { tail = (tail - 1 + cap) % cap; --count; }
            idx[(size_t) tail] = t; val[(size_t) tail] = v; tail = (tail + 1) % cap; ++count;
            while (count > 0 && idx[(size_t) head] <= t - len) { head = (head + 1) % cap; --count; }
            ++t;
            return val[(size_t) head];
        }
    private:
        int back() const { return (tail - 1 + cap) % cap; }
        std::vector<int64_t> idx; std::vector<double> val;
        int cap = 2, head = 0, tail = 0, count = 0, len = 1;
        int64_t t = 0;
    };

    //------------------------------------------------------------------ moyenne glissante (somme courante, recalée régulièrement)
    class BoxAverage
    {
    public:
        void prepare (int maxLen) { buf.assign ((size_t) maxLen + 1, 1.0); setLength (1, 1.0); }
        void setLength (int n, double fill)
        {
            len = std::clamp (n, 1, (int) buf.size());
            std::fill (buf.begin(), buf.begin() + len, fill);
            sum = fill * len; pos = 0; sinceExact = 0;
        }
        double push (double v)
        {
            sum += v - buf[(size_t) pos];
            buf[(size_t) pos] = v;
            if (++pos >= len) pos = 0;
            if (++sinceExact >= 8192) { sum = 0; for (int i = 0; i < len; ++i) sum += buf[(size_t) i]; sinceExact = 0; }
            return sum / len;
        }
    private:
        std::vector<double> buf; double sum = 1.0; int len = 1, pos = 0, sinceExact = 0;
    };

    //------------------------------------------------------------------ gain « avance » garanti (voir la démonstration en tête)
    //  r : gain requis (<= 1) de l'échantillon de détection p.
    //  W : étalement de la requête sur les voisins (crêtes entre échantillons), S : étendue du lissage.
    //  Retard de l'audio par rapport au flux de détection : D = 2W + S - 1 ; minimum sur H = 2W + S.
    class LookaheadGain
    {
    public:
        void prepare (int maxS, int maxW) { mn.prepare (2 * maxW + maxS + 2); b1.prepare (maxS + 2); b2.prepare (maxS + 2); setup (1, 0, 1.0); }
        void setup (int S_, int W_, double fill)
        {
            S = std::max (1, S_); W = std::max (0, W_);
            mn.reset(); mn.setLength (2 * W + S);
            const int a = (S + 1) / 2, b = S + 1 - a;                       // a + b - 1 = S
            b1.setLength (std::max (1, a), fill); b2.setLength (std::max (1, b), fill);
            for (int i = 0; i < 2 * W + S; ++i) mn.push (fill);
        }
        int delay() const { return 2 * W + S - 1; }
        double push (double r) { return b2.push (b1.push (mn.push (r))); }
    private:
        SlidingMin mn; BoxAverage b1, b2; int S = 1, W = 0;
    };

    //------------------------------------------------------------------ ligne à retard
    class Delay
    {
    public:
        void prepare (int maxDelay) { buf.assign ((size_t) maxDelay + 1, 0.0f); pos = 0; d = 0; }
        void clear() { std::fill (buf.begin(), buf.end(), 0.0f); }
        void setDelay (int n) { d = std::clamp (n, 0, (int) buf.size() - 1); }
        float push (float x)
        {
            buf[(size_t) pos] = x;
            int r = pos - d; if (r < 0) r += (int) buf.size();
            const float y = buf[(size_t) r];
            if (++pos >= (int) buf.size()) pos = 0;
            return y;
        }
        // lecture à un retard quelconque de l'échantillon écrit en dernier (0 = dernier)
        float tap (int k) const { int r = pos - 1 - k; while (r < 0) r += (int) buf.size(); return buf[(size_t) r]; }
        void write (float x) { buf[(size_t) pos] = x; if (++pos >= (int) buf.size()) pos = 0; }
    private:
        std::vector<float> buf; int pos = 0, d = 0;
    };

    //------------------------------------------------------------------ interpolateur 4x (crêtes entre échantillons, BS.1770 annexe 2 : 4x, 48 coefficients)
    struct TruePeak4x       // nom historique : interpolateur 8 positions entre deux échantillons
    {
        static constexpr int kPh = 8, kTaps = 24;    // 192 coefficients (le BS.1770 en a 48 : on mesure plus finement)
        float h[kPh][kTaps] {};
        TruePeak4x()
        {
            // sinus cardinal fenêtré (Kaiser, beta 9), coupure exactement à la moitié de fs d'origine ;
            // positions entre xs[11] et xs[12] : 1/16, 3/16, ... 15/16
            constexpr int N = kPh * kTaps;
            const double beta = 9.0;
            auto bessel0 = [] (double x) { double s = 1, t = 1; for (int k = 1; k < 50; ++k) { t *= (x / (2.0 * k)) * (x / (2.0 * k)); s += t; } return s; };
            for (int ph = 0; ph < kPh; ++ph)
            {
                double sum = 0, tmp[kTaps];
                for (int k = 0; k < kTaps; ++k)
                {
                    const int n = kPh * k + ph;
                    const double t = (n - (N - 1) * 0.5) / kPh;
                    const double s = std::abs (t) < 1e-12 ? 1.0 : std::sin (kPi * t) / (kPi * t);
                    const double arg = 2.0 * n / (N - 1) - 1.0;
                    tmp[k] = s * bessel0 (beta * std::sqrt (std::max (0.0, 1.0 - arg * arg))) / bessel0 (beta);
                    sum += tmp[k];
                }
                for (int k = 0; k < kTaps; ++k) h[ph][k] = (float) (tmp[k] / sum);
            }
        }
        // crête maximale entre xs[11] et xs[12] ; xs = 24 échantillons, du plus ancien au plus récent
        double peakBetween (const float* xs) const
        {
            float m = 0;
            for (int ph = 0; ph < kPh; ++ph)
            {
                float acc = 0;
                for (int k = 0; k < kTaps; ++k) acc += h[ph][k] * xs[kTaps - 1 - k];
                m = std::max (m, std::abs (acc));
            }
            return m;
        }
    };

    //------------------------------------------------------------------ filtre de mesure true peak de l'ITU-R BS.1770-4 (annexe 2 : 4x, 48 coefficients)
    struct ItuTruePeak
    {
        static constexpr int kTaps = 12;
        // crête maximale entre xs[5] et xs[6] ; xs = 12 échantillons, du plus ancien au plus récent
        static double peakBetween (const float* xs)
        {
            static const float h[4][kTaps] = {
                { 0.0017089843750f, 0.0109863281250f, -0.0196533203125f, 0.0332031250000f, -0.0594482421875f, 0.1373291015625f, 0.9721679687500f, -0.1022949218750f, 0.0476074218750f, -0.0266113281250f, 0.0148925781250f, -0.0083007812500f },
                { -0.0291748046875f, 0.0292968750000f, -0.0517578125000f, 0.0891113281250f, -0.1665039062500f, 0.4650878906250f, 0.7797851562500f, -0.2003173828125f, 0.1015625000000f, -0.0582275390625f, 0.0330810546875f, -0.0189208984375f },
                { -0.0189208984375f, 0.0330810546875f, -0.0582275390625f, 0.1015625000000f, -0.2003173828125f, 0.7797851562500f, 0.4650878906250f, -0.1665039062500f, 0.0891113281250f, -0.0517578125000f, 0.0292968750000f, -0.0291748046875f },
                { -0.0083007812500f, 0.0148925781250f, -0.0266113281250f, 0.0476074218750f, -0.1022949218750f, 0.9721679687500f, 0.1373291015625f, -0.0594482421875f, 0.0332031250000f, -0.0196533203125f, 0.0109863281250f, 0.0017089843750f } };
            float m = 0;
            for (int ph = 0; ph < 4; ++ph)
            {
                float acc = 0;
                for (int k = 0; k < kTaps; ++k) acc += h[ph][k] * xs[kTaps - 1 - k];
                m = std::max (m, std::abs (acc));
            }
            return m;
        }
    };

    //------------------------------------------------------------------ filtre d'état variable (TPT) d'ordre 2, Butterworth
    struct Svf
    {
        double g = 0, k = std::sqrt (2.0), a1 = 0, a2 = 0, a3 = 0;
        double ic1[2] {}, ic2[2] {};
        void set (double fc, double fs, double q = 0.70710678)
        {
            g = std::tan (kPi * std::min (fc, fs * 0.49) / fs); k = 1.0 / q;
            a1 = 1.0 / (1.0 + g * (g + k)); a2 = g * a1; a3 = g * a2;
        }
        void reset() { ic1[0] = ic1[1] = ic2[0] = ic2[1] = 0; }
        // renvoie passe-bas, passe-haut
        void tick (double x, int ch, double& lp, double& hp)
        {
            const double v3 = x - ic2[ch];
            const double v1 = a1 * ic1[ch] + a2 * v3;
            const double v2 = ic2[ch] + a2 * ic1[ch] + a3 * v3;
            ic1[ch] = 2 * v1 - ic1[ch]; ic2[ch] = 2 * v2 - ic2[ch];
            lp = v2; hp = x - k * v1 - v2;
        }
        double allpass (double x, int ch) { double lp, hp; tick (x, ch, lp, hp); return lp - k * (x - lp - hp) + hp; }
    };

    // Linkwitz-Riley 24 dB/oct = deux Butterworth en cascade ; passe-tout de même phase = LP + HP
    struct LR4
    {
        Svf lp1, lp2, hp1, hp2;
        void set (double fc, double fs) { lp1.set (fc, fs); lp2.set (fc, fs); hp1.set (fc, fs); hp2.set (fc, fs); }
        void reset() { lp1.reset(); lp2.reset(); hp1.reset(); hp2.reset(); }
        void split (double x, int ch, double& lo, double& hi)
        {
            double l, h, t;
            lp1.tick (x, ch, l, t); lp2.tick (l, ch, lo, t);
            hp1.tick (x, ch, t, h); hp2.tick (h, ch, t, hi);
        }
    };
    struct LR4Allpass
    {
        LR4 f;
        void set (double fc, double fs) { f.set (fc, fs); }
        void reset() { f.reset(); }
        double tick (double x, int ch) { double lo, hi; f.split (x, ch, lo, hi); return lo + hi; }
    };

    //==========================================================================
    //  Étage multibande (taux de base) : 4 bandes, la réduction suit la bande qui dépasse
    //==========================================================================
    class MultibandStage
    {
    public:
        static constexpr int kBands = 4;
        void prepare (double fs_, int lookaheadSamples)
        {
            fs = fs_; la = std::max (1, lookaheadSamples);
            for (int c = 0; c < 2; ++c) for (int b = 0; b < kBands; ++b) { dl[c][b].prepare (la + 4); dl[c][b].setDelay (la); }
            for (auto& d : refDl) { d.prepare (la + 4); d.setDelay (la); }
            for (auto& d : plainDl) { d.prepare (la + 4); d.setDelay (la); }
            for (auto& d : plainRefDl) { d.prepare (la + 4); d.setDelay (la); }
            aMix = 1.0 - std::exp (-1.0 / (0.03 * fs));
            gainLa.prepare (la + 2, 0); gainLa.setup (la + 1, 0, 1.0);
            reset();
        }
        void reset()
        {
            x1.reset(); x2.reset(); x3.reset(); ap1.reset(); ap3.reset(); r1.reset(); r2.reset(); r3.reset(); rap1.reset(); rap3.reset();
            for (int c = 0; c < 2; ++c) for (int b = 0; b < kBands; ++b) dl[c][b].clear();
            for (auto& d : refDl) d.clear();
            for (auto& d : plainDl) d.clear();
            for (auto& d : plainRefDl) d.clear();
            for (auto& e : env) e = 0;
            for (auto& w : wSm) w = 1;
            for (auto& g : bandGrDb) g = 0;
            relEnv = 0; gainLa.setup (la + 1, 0, 1.0);
        }
        void setCrossovers (double f1, double f2, double f3)
        {
            f1 = std::clamp (f1, 20.0, 18000.0); f2 = std::clamp (f2, f1 * 1.25, 19000.0); f3 = std::clamp (f3, f2 * 1.25, 19500.0);
            if (f1 == c1 && f2 == c2 && f3 == c3) return;
            c1 = f1; c2 = f2; c3 = f3;
            x1.set (c1, fs); x2.set (c2, fs); x3.set (c3, fs); ap1.set (c1, fs); ap3.set (c3, fs);
            r1.set (c1, fs); r2.set (c2, fs); r3.set (c3, fs); rap1.set (c1, fs); rap3.set (c3, fs);
        }
        // ceilingLin : plafond ; on : réduction active (sinon simple filtre passe-tout + retard)
        // ref : sortie de référence (même phase, sans réduction) pour l'écoute DELTA
        void process (double& L, double& R, double& refL, double& refR, double ceilingLin, bool on)
        {
            mix += ((on ? 1.0 : 0.0) - mix) * aMix;
            if (mix < 1.0e-5 && ! on)                  // coupé : simple retard, phase intacte
            {
                mix = 0.0;
                L = plainDl[0].push ((float) L); R = plainDl[1].push ((float) R);
                refL = plainRefDl[0].push ((float) refL); refR = plainRefDl[1].push ((float) refR);
                for (auto& g : bandGrDb) g = 0;
                bandsIdle = true;
                return;
            }
            if (bandsIdle) { bandsIdle = false; x1.reset(); x2.reset(); x3.reset(); ap1.reset(); ap3.reset(); r1.reset(); r2.reset(); r3.reset(); rap1.reset(); rap3.reset(); }
            const double plain[2] = { plainDl[0].push ((float) L), plainDl[1].push ((float) R) };
            const double plainRef[2] = { plainRefDl[0].push ((float) refL), plainRefDl[1].push ((float) refR) };
            double in[2] = { L, R }, band[2][kBands];
            for (int c = 0; c < 2; ++c)
            {
                double lo, hi, b0, b1, b2, b3;
                x2.split (in[c], c, lo, hi);
                x1.split (ap3.tick (lo, c), c, b0, b1);
                x3.split (ap1.tick (hi, c), c, b2, b3);
                band[c][0] = b0; band[c][1] = b1; band[c][2] = b2; band[c][3] = b3;
            }
            // référence (sans réduction) : même chaîne de filtres
            double ref[2] = { refL, refR };
            for (int c = 0; c < 2; ++c)
            {
                double lo, hi, b0, b1, b2, b3;
                r2.split (ref[c], c, lo, hi);
                r1.split (rap3.tick (lo, c), c, b0, b1);
                r3.split (rap1.tick (hi, c), c, b2, b3);
                ref[c] = refDl[c].push ((float) (b0 + b1 + b2 + b3));
            }
            refL = plainRef[0] + (ref[0] - plainRef[0]) * mix; refR = plainRef[1] + (ref[1] - plainRef[1]) * mix;

            // poids des bandes : enveloppe de crête (attaque 0,5 ms, relâchement 60 ms)
            const double aA = 1.0 - std::exp (-1.0 / (0.0005 * fs)), aR = 1.0 - std::exp (-1.0 / (0.060 * fs));
            double wMax = 1e-12;
            for (int b = 0; b < kBands; ++b)
            {
                const double p = std::max (std::abs (band[0][b]), std::abs (band[1][b]));
                env[b] += (p - env[b]) * (p > env[b] ? aA : aR);
                wMax = std::max (wMax, env[b]);
            }
            // réduction large bande requise (somme), avec relâchement (80 ms)
            const double peak = std::max (std::abs (L), std::abs (R));
            const double need = peak > ceilingLin ? gainToDb (peak / ceilingLin) : 0.0;
            const double aRel = 1.0 - std::exp (-1.0 / (0.080 * fs));
            relEnv = need > relEnv ? need : relEnv + (need - relEnv) * aRel;
            const double g = gainLa.push (dbToGain (-relEnv));        // avance : lissée, alignée sur les bandes retardées
            const double grDb = -gainToDb (g);
            const double aW = 1.0 - std::exp (-1.0 / (0.010 * fs));
            double outv[2] = { 0, 0 };
            for (int b = 0; b < kBands; ++b)
            {
                // la bande dominante prend toute la réduction, les autres en proportion de leur crête
                const double w = std::pow (env[b] / wMax, 1.5);
                wSm[b] += (w - wSm[b]) * aW;
                const double gb = dbToGain (-grDb * wSm[b] * mix);
                bandGrDb[b] = grDb * wSm[b] * mix;
                for (int c = 0; c < 2; ++c) outv[c] += dl[c][b].push ((float) band[c][b]) * gb;
            }
            L = plain[0] + (outv[0] - plain[0]) * mix; R = plain[1] + (outv[1] - plain[1]) * mix;
        }
        int latency() const { return la; }
        double bandGrDb[kBands] {};
        double c1 = -1, c2 = -1, c3 = -1;
    private:
        double fs = 48000; int la = 96;
        LR4 x1, x2, x3, r1, r2, r3; LR4Allpass ap1, ap3, rap1, rap3;
        Delay dl[2][kBands], refDl[2], plainDl[2], plainRefDl[2];
        double env[kBands] {}, wSm[kBands] {}, relEnv = 0, mix = 0, aMix = 0.001;
        bool bandsIdle = true;
        LookaheadGain gainLa;
    };

    //==========================================================================
    //  Limiteur principal (au taux suréchantillonné) : deux enveloppes + avance garantie
    //==========================================================================
    struct MainParams
    {
        double ceilingLin = 0.891;   // plafond linéaire
        int style = modern;
        double attack = 0.5;          // 0..1 (0,25x .. 4x l'attaque lente du style)
        double releaseMs = 100;
        bool autoRelease = true;
        double lookaheadMs = 2.0;
        double linkTrans = 1.0, linkRel = 0.5;   // 0..1
        double transient = 0.0;       // 0..1
        double clip = 0.0;            // 0..1
    };

    class MainLimiter
    {
    public:
        // fsOs : fréquence suréchantillonnée ; os : facteur ; maxDelay : avance maximale (échantillons suréchantillonnés)
        void prepare (double fsOs_, int os_, int maxDelay)
        {
            fsOs = fsOs_; os = std::max (1, os_); Dmax = std::max (1, maxDelay);
            for (auto& d : dl) d.prepare (Dmax + 4);
            for (auto& g : la) g.prepare (Dmax + 2, 0);
            const double fsB = fsOs / os;
            cF03 = coefAt (0.3, fsB); cF8 = coefAt (8.0, fsB); cS40 = coefAt (40.0, fsB); c300 = coefAt (300.0, fsB);
            d300 = std::exp (-1.0 / (0.3 * fsB));
            cAtk = -1; reset();
        }
        void reset()
        {
            for (auto& d : dl) d.clear();
            for (int c = 0; c < 2; ++c) { eF[c] = eS[c] = 0; fastEnv[c] = slowEnv[c] = rmsEnv[c] = pkEnv[c] = 1e-9; lastGr[c] = 0; tr[c] = 0; aFR[c] = 1e-4; detPk[c] = 0; }
            curL = -1; tick = 0;
        }
        int delay() const { return Dmax; }

        // un échantillon suréchantillonné ; sort l'échantillon retardé de Dmax, limité
        void process (double& L, double& R, const MainParams& p)
        {
            const int Lw = std::clamp ((int) std::lround (p.lookaheadMs * 0.001 * fsOs), 0, Dmax);
            if (Lw != curL)
            {
                curL = Lw;
                const double fill = std::exp (-0.11512925 * std::max (lastGr[0], lastGr[1]));
                for (auto& g : la) g.setup (Lw + 1, 0, fill);
            }
            if (p.attack != cAtk || p.releaseMs != cRel || p.style != cStyle || p.autoRelease != cAuto || p.clip != cClip || p.transient != cTrans || p.ceilingLin != cCeil)
            {
                cAtk = p.attack; cRel = p.releaseMs; cStyle = p.style; cAuto = p.autoRelease; cClip = p.clip; cTrans = p.transient; cCeil = p.ceilingLin;
                st = styleSpec (p.style);
                const double atkMul = std::pow (4.0, (p.attack - 0.5) * 2.0);
                aSA = coefAt (st.slowAtkMs * atkMul, fsOs);
                aSR = coefAt (p.releaseMs * st.slowRelMul, fsOs);
                fRelMs = std::max (1.0, p.releaseMs * st.fastRelMul);
                clipAmt = std::clamp (p.clip + st.clipAdd * (1.0 - p.clip), 0.0, 1.0);
                trAmt = std::clamp (p.transient + st.transAdd * (1.0 - p.transient), 0.0, 1.0);
                cdb = gainToDb (p.ceilingLin);
                kneeLin = p.ceilingLin * dbToGain (-st.kneeDb);
                clipThr = p.ceilingLin * dbToGain ((1.0 - clipAmt) * 6.0);
                clipK0 = clipThr * (0.55 + 0.25 * (1.0 - clipAmt));
            }
            // écrêteur doux : seuil entre +6 dB au-dessus du plafond (peu) et le plafond (beaucoup)
            double x[2] = { L, R };
            if (clipAmt > 0.001)
                for (auto& v : x)
                {
                    const double a = std::abs (v);
                    if (a > clipK0) v = (v < 0 ? -1.0 : 1.0) * (clipK0 + (clipThr - clipK0) * std::tanh ((a - clipK0) / (clipThr - clipK0)));
                }
            // détection sur l'échantillon retardé de (Dmax - Lw)
            double det[2];
            for (int c = 0; c < 2; ++c) { dl[c].write ((float) x[c]); det[c] = dl[c].tap (Dmax - Lw); detPk[c] = std::max (detPk[c], std::abs (det[c])); }

            // calculs lents (transitoires, relâchement automatique) une fois par échantillon de base
            if (++tick >= os)
            {
                tick = 0;
                for (int c = 0; c < 2; ++c)
                {
                    const double a = detPk[c]; detPk[c] = 0;
                    fastEnv[c] += (a - fastEnv[c]) * (a > fastEnv[c] ? cF03 : cF8);
                    slowEnv[c] += (a - slowEnv[c]) * cS40;
                    const double rise = gainToDb (fastEnv[c] + 1e-9) - gainToDb (slowEnv[c] + 1e-9);
                    tr[c] = trAmt * std::clamp ((rise - 3.0) / 9.0, 0.0, 1.0);
                    double relMs = fRelMs;
                    if (cAuto)
                    {
                        pkEnv[c] = std::max (a, pkEnv[c] * d300);
                        rmsEnv[c] += (a * a - rmsEnv[c]) * c300;
                        const double crest = gainToDb (pkEnv[c] + 1e-9) - 0.5 * gainToDb (rmsEnv[c] + 1e-18);
                        // musique dense (facteur de crête faible) : relâchement plus long ; transitoires isolées : plus court
                        relMs *= std::clamp (std::pow (2.0, (12.0 - crest) / 6.0), 0.35, 3.0);
                    }
                    relMs /= (1.0 + 4.0 * tr[c]);
                    aFR[c] = coefAt (relMs, fsOs);
                }
            }

            double t[2], env[2];
            for (int c = 0; c < 2; ++c)
            {
                const double a = std::abs (det[c]);
                double req = 0.0;
                if (a > kneeLin)
                {
                    const double over = gainToDb (a) - cdb;
                    const double knee = st.kneeDb;
                    if (knee > 0.01) req = over >= knee ? over : (over + knee) * (over + knee) / (4.0 * knee);   // toujours >= dépassement
                    else req = std::max (0.0, over);
                }
                t[c] = req;
                // enveloppe lente (nivellement) : suit une part de la réduction, ignore les transitoires
                const double sTarget = req * st.slowAmt;
                if (sTarget > eS[c]) eS[c] += (sTarget - eS[c]) * aSA * (1.0 - tr[c]);
                else                 eS[c] += (sTarget - eS[c]) * aSR;
                // enveloppe rapide : attaque immédiate (l'avance la lisse), relâche vers l'enveloppe lente
                if (req > eF[c]) eF[c] = req;
                else { const double fl = std::max (eS[c], req); eF[c] += (fl - eF[c]) * aFR[c]; }
                env[c] = std::max (eF[c], eS[c]);
            }
            const double tLink = std::max (t[0], t[1]);
            const double eLink = std::max (env[0], env[1]);
            double g[2];
            for (int c = 0; c < 2; ++c)
            {
                const double rel = env[c] + (eLink - env[c]) * p.linkRel;
                const double trn = t[c] + (tLink - t[c]) * p.linkTrans;
                const double gr = std::max ({ rel, trn, t[c] });
                lastGr[c] = gr;
                g[c] = la[c].push (gr > 1e-7 ? std::exp (-0.11512925 * gr) : 1.0);
            }
            L = dl[0].tap (Dmax) * g[0];
            R = dl[1].tap (Dmax) * g[1];
            gMin = std::min (gMin, std::min (g[0], g[1]));
        }
        // plus petit gain appliqué depuis la dernière lecture (affichage)
        double takeMinGain() { const double v = gMin; gMin = 1.0; return v; }
    private:
        static double coefAt (double ms, double fs) { const double x = 1.0 / (std::max (0.01, ms) * 0.001 * fs); return x > 0.05 ? 1.0 - std::exp (-x) : x * (1.0 - 0.5 * x); }
        double fsOs = 192000; int os = 4, Dmax = 960, curL = -1, tick = 0;
        Delay dl[2];
        LookaheadGain la[2];
        StyleSpec st = styleSpec (modern);
        double eF[2] {}, eS[2] {}, fastEnv[2] {}, slowEnv[2] {}, rmsEnv[2] {}, pkEnv[2] {}, lastGr[2] {}, tr[2] {}, aFR[2] {}, detPk[2] {};
        double cAtk = -1, cRel = -1, cClip = -1, cTrans = -1, cCeil = -1, aSA = 0, aSR = 0, fRelMs = 50, clipAmt = 0, trAmt = 0, cdb = 0, kneeLin = 1, clipThr = 1, clipK0 = 1;
        double cF03 = 0, cF8 = 0, cS40 = 0, c300 = 0, d300 = 0, gMin = 1.0;
        int cStyle = -1; bool cAuto = false;
    };

    //==========================================================================
    //  Étage de sécurité (taux de base, après le sous-échantillonnage) : plafond garanti,
    //  crêtes entre échantillons comprises quand TRUE PEAK est actif
    //==========================================================================
    class SafetyLimiter
    {
    public:
        static constexpr int W = 12;           // étalement de la requête (= demi-longueur de l'interpolateur)
        void prepare (double fs_)
        {
            fs = fs_;
            S = std::max (2, (int) std::lround (0.001 * fs));
            for (auto& d : dl) d.prepare (2 * W + S + 64);
            la.prepare (S + 2, W); la.setup (S, W, 1.0);
            setRelease (15.0);
            reset();
        }
        void reset() { for (auto& d : dl) d.clear(); env = 1.0; la.setup (S, W, 1.0); n = 0; }
        int delay() const { return la.delay(); }         // 2W + S - 1 (la détection regarde W échantillons en avance)

        void process (double& L, double& R, double ceilingLin, bool truePeak)
        {
            dl[0].write ((float) L); dl[1].write ((float) R);
            // détection de l'échantillon p = n - W (crêtes entre p et p + 1 : besoin de x[p-11 .. p+12])
            double pk = 0;
            for (int c = 0; c < 2; ++c)
            {
                pk = std::max (pk, (double) std::abs (dl[c].tap (W)));
                if (truePeak)
                {
                    float xs[TruePeak4x::kTaps];
                    for (int k = 0; k < TruePeak4x::kTaps; ++k) xs[k] = dl[c].tap (W + 11 - k);  // x[p-11] .. x[p+12]
                    pk = std::max (pk, tp.peakBetween (xs));
                    // et la mesure officielle BS.1770 (lit un peu plus haut près de Nyquist) : x[p-5] .. x[p+6]
                    pk = std::max (pk, ItuTruePeak::peakBetween (xs + 6));
                }
            }
            const double r = pk > ceilingLin ? ceilingLin / pk : 1.0;
            env = std::min (r, env + (1.0 - env) * relC);
            const double g = la.push (env);
            L = dl[0].tap (delay()) * g;
            R = dl[1].tap (delay()) * g;
            // garde finale (n'agit qu'en cas d'erreur d'arrondi)
            L = std::clamp (L, -ceilingLin, ceilingLin);
            R = std::clamp (R, -ceilingLin, ceilingLin);
            grOut = -gainToDb (g);
            ++n;
        }
        void setRelease (double ms) { relC = 1.0 - std::exp (-1.0 / (ms * 0.001 * fs)); }
        double grOut = 0;
    private:
        double fs = 48000, env = 1.0, relC = 0.001; int S = 48; int64_t n = 0;
        Delay dl[2];
        LookaheadGain la;
        TruePeak4x tp;
    };

    //==========================================================================
    //  Mesure de sonie EBU R128 / ITU-R BS.1770-4 (+ LRA EBU Tech 3342, true peak)
    //==========================================================================
    class Loudness
    {
    public:
        static constexpr int kBins = 900;              // -70 .. +20 LUFS par pas de 0,1
        static double binLufs (int b) { return -70.0 + 0.1 * (b + 0.5); }
        void prepare (double fs_)
        {
            fs = fs_;
            // filtre K (pré-filtre plateau + passe-haut RLB), formules valables à toute fréquence d'échantillonnage
            {
                const double f0 = 1681.974450955533, G = 3.999843853973347, Q = 0.7071752369554196;
                const double K = std::tan (kPi * f0 / fs), Vh = std::pow (10.0, G / 20.0), Vb = std::pow (Vh, 0.4996667741545416);
                const double a0 = 1.0 + K / Q + K * K;
                b1[0] = (Vh + Vb * K / Q + K * K) / a0; b1[1] = 2.0 * (K * K - Vh) / a0; b1[2] = (Vh - Vb * K / Q + K * K) / a0;
                a1[0] = 2.0 * (K * K - 1.0) / a0; a1[1] = (1.0 - K / Q + K * K) / a0;
            }
            {
                const double f0 = 38.13547087602444, Q = 0.5003270373238773, K = std::tan (kPi * f0 / fs);
                const double a0 = 1.0 + K / Q + K * K;
                b2[0] = 1.0; b2[1] = -2.0; b2[2] = 1.0;
                a2[0] = 2.0 * (K * K - 1.0) / a0; a2[1] = (1.0 - K / Q + K * K) / a0;
            }
            sub = std::max (1, (int) std::lround (fs * 0.1));
            reset();
        }
        void reset()
        {
            for (auto& s : z) for (auto& v : s) v = 0;
            for (auto& v : hist) v.store (0);
            for (auto& v : histS) v.store (0);
            for (auto& v : ring) v = 0;
            ringPos = 0; filled = 0; acc = 0; cnt = 0;
            momentary = shortTerm = -200; tpMax = 0;
            for (int c = 0; c < 2; ++c) for (auto& v : tpHist[c]) v = 0;
            tpPos = 0;
        }
        void process (double L, double R)
        {
            const double x[2] = { L, R };
            double e = 0;
            for (int c = 0; c < 2; ++c)
            {
                double* s = z[c];
                const double y1 = b1[0] * x[c] + s[0];
                s[0] = b1[1] * x[c] - a1[0] * y1 + s[1];
                s[1] = b1[2] * x[c] - a1[1] * y1;
                const double y2 = b2[0] * y1 + s[2];
                s[2] = b2[1] * y1 - a2[0] * y2 + s[3];
                s[3] = b2[2] * y1 - a2[1] * y2;
                e += y2 * y2;
                tpHist[c][tpPos] = (float) x[c];
            }
            for (int c = 0; c < 2; ++c)
            {
                // vumètre : filtre officiel BS.1770-4 (4x)
                float xs[ItuTruePeak::kTaps];
                for (int k = 0; k < ItuTruePeak::kTaps; ++k) xs[k] = tpHist[c][(tpPos - (ItuTruePeak::kTaps - 1) + k + 32) & 31];
                tpMax = std::max (tpMax, std::max ((double) std::abs (xs[ItuTruePeak::kTaps - 1]), ItuTruePeak::peakBetween (xs)));
            }
            tpPos = (tpPos + 1) & 31;
            acc += e; ++cnt;
            if (cnt >= sub)
            {
                ring[ringPos] = acc / cnt; ringPos = (ringPos + 1) % 30; filled = std::min (30, filled + 1);
                acc = 0; cnt = 0;
                double m = 0; for (int i = 0; i < 4; ++i) m += ring[(ringPos - 1 - i + 30) % 30];
                m /= 4.0;
                double s = 0; for (int i = 0; i < 30; ++i) s += ring[i];
                s /= 30.0;
                momentary = filled >= 4 ? lufs (m) : -200;
                shortTerm = filled >= 30 ? lufs (s) : -200;
                if (filled >= 4 && momentary > -70.0) hist[binOf (momentary)].fetch_add (1, std::memory_order_relaxed);
                if (filled >= 30 && shortTerm > -70.0) histS[binOf (shortTerm)].fetch_add (1, std::memory_order_relaxed);
            }
        }
        // intégrée (gating absolu -70, relatif -10 LU)
        static double integrated (const uint32_t* h)
        {
            double sumP = 0; uint64_t n = 0;
            for (int b = 0; b < kBins; ++b) if (h[b]) { sumP += h[b] * power (binLufs (b)); n += h[b]; }
            if (n == 0) return -200;
            const double gate = lufs (sumP / n) - 10.0;
            sumP = 0; n = 0;
            for (int b = 0; b < kBins; ++b) if (h[b] && binLufs (b) >= gate) { sumP += h[b] * power (binLufs (b)); n += h[b]; }
            return n ? lufs (sumP / n) : -200;
        }
        // LRA (EBU Tech 3342) : gating relatif -20 LU, écart entre les centiles 10 et 95
        static double range (const uint32_t* h)
        {
            double sumP = 0; uint64_t n = 0;
            for (int b = 0; b < kBins; ++b) if (h[b]) { sumP += h[b] * power (binLufs (b)); n += h[b]; }
            if (n < 2) return 0;
            const double gate = lufs (sumP / n) - 20.0;
            uint64_t tot = 0; for (int b = 0; b < kBins; ++b) if (binLufs (b) >= gate) tot += h[b];
            if (tot < 2) return 0;
            auto pct = [&] (double q) { const uint64_t target = (uint64_t) std::ceil (q * tot); uint64_t c = 0;
                                        for (int b = 0; b < kBins; ++b) if (binLufs (b) >= gate) { c += h[b]; if (c >= target) return binLufs (b); }
                                        return binLufs (kBins - 1); };
            return std::max (0.0, pct (0.95) - pct (0.10));
        }
        static double lufs (double p) { return p > 1e-20 ? -0.691 + 10.0 * std::log10 (p) : -200.0; }
        static double power (double l) { return std::pow (10.0, (l + 0.691) / 10.0); }
        static int binOf (double l) { return std::clamp ((int) std::floor ((l + 70.0) * 10.0), 0, kBins - 1); }
        // lecture depuis un autre fil
        static void snapshot (const std::atomic<uint32_t>* h, uint32_t* out) { for (int b = 0; b < kBins; ++b) out[b] = h[b].load (std::memory_order_relaxed); }
        std::atomic<uint32_t> hist[kBins] {}, histS[kBins] {};
        double momentary = -200, shortTerm = -200, tpMax = 0;
    private:
        double fs = 48000, b1[3] {}, a1[2] {}, b2[3] {}, a2[2] {}, z[2][4] {};
        double ring[30] {}, acc = 0; int ringPos = 0, filled = 0, cnt = 0, sub = 4800;
        float tpHist[2][32] {}; int tpPos = 0;
    };

    //==========================================================================
    //  Dither TPDF (16 / 24 bits), mise en forme du bruit (Lipshitz, 5 coefficients)
    //==========================================================================
    class Dither
    {
    public:
        void reset() { for (auto& c : e) for (auto& v : c) v = 0; }
        double process (double x, int ch, int bits, bool shaped)
        {
            const double q = 1.0 / (double) (1 << (bits - 1));
            static const double h[5] = { 2.033, -2.165, 1.959, -1.590, 0.6149 };
            double v = x;
            if (shaped) for (int k = 0; k < 5; ++k) v -= h[k] * e[ch][k];
            const double d = (rnd() - rnd()) * q;            // TPDF +-1 LSB
            const double y = std::round ((v + d) / q) * q;
            if (shaped) { for (int k = 4; k > 0; --k) e[ch][k] = e[ch][k - 1]; e[ch][0] = y - v; }
            return y;
        }
    private:
        double rnd() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return (double) s / 4294967296.0; }
        uint32_t s = 2463534242u;
        double e[2][5] {};
    };
}
