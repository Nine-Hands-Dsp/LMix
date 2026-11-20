# LMix — NINE HANDS | DSP

Limiteur de mastering VST3 (Windows et macOS) : true peak garanti (mesure BS.1770-4), moteur à deux enveloppes avec avance, relâchement adaptatif, 8 styles, suréchantillonnage jusqu'à 32x, mode multibande et mesure de sonie EBU R128 complète.

<p align="center">
  <a href="https://github.com/Nine-Hands-Dsp/LMix/releases/latest/download/LMix-VST3-Windows.zip"><img alt="Télécharger pour Windows" src="https://img.shields.io/badge/%E2%AC%87%20T%C3%A9l%C3%A9charger-Windows-0078D6?style=for-the-badge"></a>
  <a href="https://github.com/Nine-Hands-Dsp/LMix/releases/latest/download/LMix-VST3-macOS.zip"><img alt="Télécharger pour macOS" src="https://img.shields.io/badge/%E2%AC%87%20T%C3%A9l%C3%A9charger-macOS-111111?style=for-the-badge&logo=apple&logoColor=white"></a>
  <br>
  <a href="https://github.com/Nine-Hands-Dsp/LMix/releases/latest/download/LMix-Notice.pdf"><img alt="Télécharger la notice (PDF)" src="https://img.shields.io/badge/%F0%9F%93%96%20Notice-PDF-919496?style=for-the-badge"></a>
  <br>
  <a href="https://github.com/Nine-Hands-Dsp/LMix/releases/latest"><img alt="Dernière version" src="https://img.shields.io/github/v/release/Nine-Hands-Dsp/LMix?label=derni%C3%A8re%20version&color=ff4b4e"></a>
</p>

## Fonctions

- **Plafond garanti** : la sortie ne dépasse jamais le plafond, ni en crête d'échantillon, ni en true peak. Un étage de sécurité final vérifie les crêtes entre échantillons avec le filtre officiel BS.1770-4 (celui des plateformes) et avec un interpolateur 8x plus fin.
- **Moteur à deux enveloppes** (dans l'esprit de Pro-L 2) :
  - une enveloppe rapide avec avance (0 à 5 ms) qui attrape les crêtes, crêtes entre échantillons comprises ;
  - un maintien calé automatiquement sur la demi-période du grave, pour ne pas déformer les basses ;
  - une enveloppe lente qui nivelle le corps du signal.
- **Distorsion mesurée** (THD+N, sinus 6 dB au-dessus du plafond, 4x) : −72 dB à 40 Hz, −81 dB à 100 Hz et −89 dB à 1 kHz en Modern et dans tous les styles sans écrêtage.
- **Relâchement adaptatif** (dans l'esprit d'ARC chez Waves et d'IRC chez Ozone) : il s'allonge sur la musique dense et se raccourcit sur les transitoires isolées.
- **8 styles** : Transparent, Punch, Dynamic, Loud, Bus, Safe, Modern, Aggressive.
- **TRANSIENT** : relâchement accéléré et nivellement suspendu sur les attaques, pour garder le punch.
- **ENHANCE** : remonte les détails faibles (compression vers le haut), dans l'esprit de Weiss et Sonnox.
- **CLIP** : écrêteur doux avant le limiteur, dans son propre étage suréchantillonné. Il ne coupe que les crêtes courtes au-dessus du niveau déjà nivelé, jamais un son tenu.
- **Liaison stéréo** séparée pour les transitoires et pour le relâchement.
- **Suréchantillonnage** 1x à 32x, en phase linéaire.
- **Multibande** : 4 bandes à reconstruction parfaite (la somme redonne exactement le signal, sans rotation de phase). La bande dominante prend la part tenue de la réduction (dans l'esprit du L3 de Waves), le limiteur principal garde les crêtes. Fréquences de coupure réglables à la souris.
- **Mesures** :
  - LUFS intégré, court terme et momentané (EBU R128 / ITU-R BS.1770-4), LRA (EBU Tech 3342), true peak et PLR ;
  - cibles Spotify, Apple Music, YouTube, Amazon, Deezer, SoundCloud, CD / Club, EBU R128 et ATSC, avec « Match gain to target » ;
  - historique défilant de l'entrée, de la sortie et de la réduction de gain (3, 6 ou 12 s).
- **Écoute** :
  - UNITY : gain compensé, pour comparer à volume égal ;
  - DELTA : on n'entend que ce que le limiteur retire.
- **Sortie** : filtre DC, dither TPDF 16 ou 24 bits avec mise en forme du bruit.
- **Presets et pilotage** : 13 presets d'usine, dont « Metal Master (7/8 strings) », sauvegarde de vos presets (`Documents/Nine Hands DSP/LMix`), MIDI Learn sur tous les réglages (aucun CC par défaut), fenêtre de 60 à 150 %, réglage Glow.

## Installation

Les boutons ci-dessus téléchargent toujours la dernière version.

- **Windows** : décompresser et copier `LMix.vst3` dans `C:\Program Files\Common Files\VST3`.
- **macOS** : décompresser et copier `LMix.vst3` dans `~/Library/Audio/Plug-Ins/VST3` (le plugin est signé ad hoc ; au premier lancement, autoriser si besoin dans Réglages Système > Confidentialité et sécurité).

Dans Reaper, le plugin apparaît sous le nom **LMix (NINE HANDS | DSP)**. Placez-le en dernier sur la piste de mastering.

## Compilation

JUCE 8.0.15 (récupéré par CMake) :

```
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

`tools/render` compile le plugin sous Linux et vérifie plusieurs points (`LmixRender analyse` mesure en plus la distorsion, le repliement, le travail de l'étage de sécurité, le multibande et la charge processeur) :
- le plafond true peak, pour chaque suréchantillonnage et chaque style ;
- la latence annoncée ;
- l'écoute DELTA ;
- la sonie, sur un sinus à −23 dBFS.

Il exporte aussi les captures de la notice.

## Historique

| Version | Changements |
|---|---|
| 1.1.2 | Menu Plugin size : sous-menu « Default size » pour choisir la taille d'ouverture des nouvelles fenêtres (60 / 75 / 100 %, commune à toutes les instances) ; ligne d'état sans scintillement (son opacité oscillait entre 100 et 89 % à chaque image) ; chiffres à chasse fixe, le texte ne bouge plus quand une valeur change. |
| 1.1.0 | Moteur optimisé : maintien calé sur le grave (distorsion des basses divisée par plus de 100), écrêteur suréchantillonné à part qui ne coupe que les crêtes courtes, détection des crêtes entre échantillons dans le limiteur principal, multibande à reconstruction parfaite et nouvelle répartition, marge de dither, calculs par bloc. |
| 1.0.0 | Première version. |
