# LMIX (NINE HANDS | DSP) - notice PDF en français, illustrée avec le rendu réel du plugin (tools/render).
# Usage : python3 tools/manual.py <dossier des captures (sortie de tools/render)> <fichier PDF de sortie>
import os, sys, re, subprocess
import numpy as np
from PIL import Image, ImageDraw, ImageFont
from reportlab.lib.pagesizes import A4
from reportlab.lib.units import mm
from reportlab.lib import colors
from reportlab.lib.styles import ParagraphStyle
from reportlab.platypus import (BaseDocTemplate, PageTemplate, NextPageTemplate, Frame, Paragraph, Spacer, Image as RLImage,
                                Table, TableStyle, PageBreak, KeepTogether, CondPageBreak)
from reportlab.pdfbase import pdfmetrics
from reportlab.pdfbase.ttfonts import TTFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SHOTS = sys.argv[1]
OUT = sys.argv[2]
VERSION = re.search(r"project\(LMix VERSION ([0-9.]+)\)", open(os.path.join(ROOT, "CMakeLists.txt")).read()).group(1)
TMP = os.path.join(os.path.dirname(os.path.abspath(OUT)), "notice_tmp")
os.makedirs(TMP, exist_ok=True)

FONTS = os.path.join(ROOT, "fonts"); FONTS2 = os.path.join(ROOT, "tools", "fonts_notice")
for n, f, d in (("Pop", "Poppins-Regular", FONTS), ("PopL", "Poppins-Light", FONTS2), ("PopM", "Poppins-Medium", FONTS), ("PopB", "Poppins-Bold", FONTS),
                ("Title", "BarlowCondensed-ExtraBold", FONTS2)):
    pdfmetrics.registerFont(TTFont(n, os.path.join(d, f + ".ttf")))

ACC = colors.HexColor("#E0393D"); ACC_L = colors.HexColor("#FF4B4E")        # rouge d'accent de LMix (thème Graphite)
DARK = colors.HexColor("#1A1C22"); GREY = colors.HexColor("#646B76"); LIGHT = colors.HexColor("#FBECEC")
LOGO_PNG = os.path.join(TMP, "nine_hands_logo_blanc.png")
subprocess.run(["rsvg-convert", "-w", "1200", os.path.join(ROOT, "logo", "NineHandsLogoWhite.svg"), "-o", LOGO_PNG], check=True)

def shot(name): return Image.open(os.path.join(SHOTS, name)).convert("RGB")
src = shot("lmix_overview.png")
k = src.width / 960.0                                  # repère de l'interface : 960 x 540 unités (taille 60 %)

# ---------- capture annotée ----------
marks = [(1, 352, 23), (2, 722, 23), (3, 917, 41), (4, 60, 90), (5, 600, 66), (6, 18, 322), (7, 764, 80), (8, 932, 56),
         (9, 945, 251), (10, 790, 300), (11, 40, 400), (12, 186, 397), (13, 190, 432), (14, 262, 500), (15, 812, 405), (16, 771, 486)]
ann = src.copy(); dr = ImageDraw.Draw(ann)
fnt = ImageFont.truetype(os.path.join(FONTS, "Poppins-Bold.ttf"), 22)
for n, x, y in marks:
    cx, cy, r = x * k, y * k, 17
    dr.ellipse((cx - r, cy - r, cx + r, cy + r), fill=(255, 255, 255), outline=(224, 57, 61), width=4)
    dr.text((cx, cy + 1), str(n), font=fnt, fill=(140, 20, 24), anchor="mm")
annotated = os.path.join(TMP, "annotated.png"); ann.save(annotated)

def crop_u(im, box): return im.crop(tuple(int(v * k) for v in box))
bars = os.path.join(TMP, "bars.png")
a = crop_u(src, (350, 6, 610, 40)); b = crop_u(shot("lmix_status.png"), (350, 6, 610, 40))
st_img = Image.new("RGB", (a.width, a.height * 2 + 12), (255, 255, 255)); st_img.paste(a, (0, 0)); st_img.paste(b, (0, a.height + 12)); st_img.save(bars)
mbshot = os.path.join(TMP, "mb.png"); crop_u(shot("lmix_multiband.png"), (10, 47, 760, 357)).save(mbshot)
meters = os.path.join(TMP, "meters.png"); crop_u(src, (768, 47, 950, 357)).save(meters)
outp = os.path.join(TMP, "out.png"); crop_u(src, (768, 365, 950, 530)).save(outp)
stylep = os.path.join(TMP, "style.png"); crop_u(src, (183, 365, 760, 530)).save(stylep)

# ---------- couverture : fenêtre détourée (coins arrondis) sur fond noir, reflet au sol, halo rouge très doux ----------
cutw, cuth = src.size
rmask = Image.new("L", src.size, 0); ImageDraw.Draw(rmask).rounded_rectangle((0, 0, cutw - 1, cuth - 1), radius=int(16 * k), fill=255)
cut = Image.new("RGBA", src.size, (0, 0, 0, 0)); cut.paste(src, (0, 0), rmask)
cw, ch = cut.size
pad = int(cw * 0.10)
canvas = Image.new("RGBA", (cw + 2 * pad, int(ch * 1.30) + pad), (0, 0, 0, 255))
yy, xx = np.mgrid[0:canvas.height, 0:canvas.width].astype(np.float64)
gcx, gcy = canvas.width / 2, pad + ch * 0.45
glow = np.exp(-(((xx - gcx) / (cw * 0.52)) ** 2 + ((yy - gcy) / (ch * 0.48)) ** 2)) * 34
edge = np.clip(np.minimum.reduce([xx, canvas.width - 1 - xx, yy, canvas.height - 1 - yy]) / (pad * 0.9), 0, 1)
glow *= edge ** 2
halo = np.dstack([glow * 1.0, glow * 0.29, glow * 0.31, np.full_like(glow, 255)]).astype(np.uint8)
canvas = Image.alpha_composite(canvas, Image.fromarray(halo, "RGBA"))
refl = cut.transpose(Image.FLIP_TOP_BOTTOM)
ra = np.array(refl).astype(np.float64)
fade = np.clip(1 - np.arange(refl.height) / (refl.height * 0.22), 0, 1) ** 1.6 * 0.22
ra[..., 3] *= fade[:, None]
canvas.alpha_composite(Image.fromarray(ra.astype(np.uint8), "RGBA"), (pad, pad + ch + 6))
canvas.alpha_composite(cut, (pad, pad))
canvas = canvas.crop((0, 0, canvas.width, pad + int(ch * 1.20)))
cover = os.path.join(TMP, "cover_cutout.png"); canvas.convert("RGB").save(cover)
COVER_PAD, COVER_CW, COVER_CH = pad, cw, ch

def img(path, width):
    im = Image.open(path); h = width * im.height / im.width
    return RLImage(path, width=width, height=h)

# ---------- styles ----------
st = {
    "h1": ParagraphStyle("h1", fontName="PopB", fontSize=20, leading=24, textColor=ACC, spaceAfter=8, spaceBefore=4, keepWithNext=1),
    "h2": ParagraphStyle("h2", fontName="PopM", fontSize=13, leading=17, textColor=DARK, spaceAfter=4, spaceBefore=10, keepWithNext=1),
    "p": ParagraphStyle("p", fontName="Pop", fontSize=9.6, leading=14, textColor=DARK, spaceAfter=5),
    "small": ParagraphStyle("small", fontName="Pop", fontSize=8.2, leading=11.5, textColor=GREY, spaceAfter=6),
    "cell": ParagraphStyle("cell", fontName="Pop", fontSize=8.4, leading=11, textColor=DARK),
    "cellb": ParagraphStyle("cellb", fontName="PopM", fontSize=8.4, leading=11, textColor=DARK),
}
def P(t, s="p"): return Paragraph(t, st[s])
def bullets(items): return [P("&bull;&nbsp;&nbsp;" + i) for i in items]
def table(rows, widths, header=True):
    data = [[P(c, "cellb" if (header and r == 0) else "cell") for c in row] for r, row in enumerate(rows)]
    t = Table(data, colWidths=widths, repeatRows=1 if header else 0)
    style = [("VALIGN", (0, 0), (-1, -1), "TOP"), ("LINEBELOW", (0, 0), (-1, -1), 0.3, colors.HexColor("#E5D8D8")),
             ("TOPPADDING", (0, 0), (-1, -1), 3), ("BOTTOMPADDING", (0, 0), (-1, -1), 3)]
    if header: style += [("BACKGROUND", (0, 0), (-1, 0), LIGHT), ("LINEBELOW", (0, 0), (-1, 0), 0.8, ACC)]
    t.setStyle(TableStyle(style)); return t

W = A4[0] - 36 * mm
f = [NextPageTemplate("p"), Spacer(1, 1), PageBreak()]

# ---------- 1. Présentation ----------
f.append(P("1. Présentation", "h1"))
f += [P("<b>LMix</b> est un limiteur de mastering. Il monte le niveau d'un mix et garantit qu'aucune crête ne dépasse le plafond choisi, y compris les crêtes « entre les échantillons » (true peak) que mesurent les plateformes de streaming."),
      P("Il a été conçu après l'étude des limiteurs de référence, en logiciel comme en matériel. LMix reprend leurs idées les plus efficaces dans un moteur écrit entièrement pour lui :")]
f += bullets(["<b>FabFilter Pro-L 2</b> : deux étages de limitation (un rapide pour les transitoires, un plus lent pour le corps), styles, liaison stéréo séparée, true peak et suréchantillonnage.",
              "<b>Waves L2 / L3</b> : relâchement qui s'adapte à la musique (ARC) et limiteur multibande qui répartit la réduction entre les bandes.",
              "<b>iZotope Ozone (Maximizer IRC)</b> : relâchement intelligent, accentuation des transitoires, écrêtage doux avant le limiteur.",
              "<b>Weiss DS1-MK3 / MM-1, Sonnox Oxford</b> : fonction <i>Enhance</i> qui ramène les détails faibles vers l'avant."])
f.append(P("Ce n'est la copie d'aucun d'entre eux : LMix réunit ces principes, et sa garantie de plafond est vérifiée par mesure (section 4)."))

# ---------- 2. Installation ----------
f.append(P("2. Installation", "h1"))
f.append(P("Windows", "h2"))
f.append(P("Téléchargez <b>LMix-VST3-Windows.zip</b>, décompressez-le et copiez le dossier <b>LMix.vst3</b> dans <b>C:\\Program Files\\Common Files\\VST3</b>."))
f.append(P("macOS", "h2"))
f.append(P("Téléchargez <b>LMix-VST3-macOS.zip</b>, décompressez-le et copiez <b>LMix.vst3</b> dans <b>~/Library/Audio/Plug-Ins/VST3</b> (binaire universel Apple Silicon et Intel). Le plugin est signé ad hoc : au premier lancement, autorisez-le si besoin dans Réglages Système &gt; Confidentialité et sécurité."))
f.append(P("Dans Reaper", "h2"))
f.append(P("Options &gt; Preferences &gt; Plug-ins &gt; VST : <b>Re-scan</b>. LMix apparaît sous le nom <b>LMix (NINE HANDS | DSP)</b>. Placez-le <b>en dernier</b> sur la piste de mastering (ou le bus master). Il accepte une piste mono ou stéréo et annonce sa latence au DAW, qui la compense."))
f.append(PageBreak())

# ---------- 3. Vue d'ensemble ----------
f.append(P("3. Vue d'ensemble", "h1"))
f.append(img(annotated, W)); f.append(Spacer(1, 3 * mm))
ov = [["N°", "Élément", "Rôle"],
      ["1", "Barre de presets", "Flèches : preset précédent / suivant ; clic sur le nom : liste des presets. Devient la ligne d'état pendant un réglage."],
      ["2", "Latence et mode de crête", "Latence totale annoncée au DAW ; TRUE PEAK (rouge) ou SAMPLE PEAK selon le bouton TRUE PEAK."],
      ["3", "Power", "Allume ou contourne LMix, en fondu ; le son contourné reste retardé de la latence (pas de décalage)."],
      ["4", "Historique", "Entrée (gris), sortie (blanc), réduction de gain (rouge, depuis le haut), plafond en pointillés (section 8)."],
      ["5", "Durée / FREEZE", "Historique sur 3, 6 ou 12 secondes ; FREEZE fige l'affichage."],
      ["6", "Légende", "Rappel du plafond et des couleurs."],
      ["7", "IN / OUT", "Crêtes avant et après le limiteur, avec maintien ; trait rouge : plafond (section 8)."],
      ["8", "LOUDNESS", "Sonie intégrée, court terme, momentanée, LRA, true peak, PLR. Un clic remet ces mesures à zéro."],
      ["9", "TARGET", "Cible de sonie (Spotify, Apple Music, YouTube…), Match gain to target."],
      ["10", "GAIN REDUCTION", "Historique court de la réduction de gain et son maximum. Un clic remet le maximum à zéro."],
      ["11", "GAIN", "Gain d'entrée : ce qui pousse le signal dans le limiteur (section 5)."],
      ["12", "STYLE", "8 caractères de limitation ; la description du style choisi s'affiche à droite."],
      ["13", "Réglages fins", "ATTACK, RELEASE, LOOKAHEAD, LINK TRANS., LINK REL., TRANSIENT, ENHANCE, CLIP (section 5)."],
      ["14", "AUTO", "Relâchement adaptatif (allumé par défaut)."],
      ["15", "CEILING", "Plafond de sortie, en dBTP (true peak) ou dBFS."],
      ["16", "Boutons de sortie", "TRUE PEAK, OS (suréchantillonnage), UNITY, DELTA, DITHER, MULTIBAND (section 6)."]]
f.append(table(ov, [9 * mm, 36 * mm, W - 45 * mm]))
f.append(PageBreak())

# ---------- 4. Comment LMix limite ----------
f.append(P("4. Comment LMix limite", "h1"))
f.append(P("Le signal traverse ces étages, dans l'ordre :"))
f.append(table([["Étage", "Rôle"],
                ["GAIN", "Monte (ou baisse) le signal avant tout le reste, de −12 à +30 dB."],
                ["Filtre DC", "Retire une éventuelle composante continue (passe-haut 5 Hz). Actif par défaut, clic droit &gt; DC offset filter."],
                ["ENHANCE", "Compression vers le haut des passages faibles (si ENHANCE &gt; 0)."],
                ["MULTIBAND", "4 bandes à reconstruction parfaite, la bande dominante prend la part tenue de la réduction (si MULTIBAND est allumé, section 7)."],
                ["CLIP", "Écrêteur doux dans son propre étage suréchantillonné : il arrondit seulement les crêtes courtes, puis le signal est refiltré pour que le limiteur voie un signal propre."],
                ["Suréchantillonnage", "1x à 32x (bouton OS) : le limiteur travaille à une fréquence plus élevée, donc avec moins de repliement et des crêtes plus fidèles."],
                ["Limiteur principal", "Deux enveloppes + avance + maintien calé sur le grave (ci-dessous)."],
                ["Sécurité true peak", "Dernier contrôle, au taux d'origine : garantit le plafond, crêtes entre échantillons comprises."],
                ["Écoute, dither", "UNITY, DELTA, puis dither en toute fin de chaîne."]],
               [34 * mm, W - 34 * mm]))
f.append(P("Deux enveloppes", "h2"))
f += bullets(["<b>L'enveloppe rapide</b> suit chaque crête, y compris celles qui tombent entre deux échantillons (estimées au taux suréchantillonné). Grâce à l'avance (LOOKAHEAD), LMix « voit » la crête arriver et descend le gain en douceur juste avant, au lieu de la couper net : pas de distorsion sur l'attaque.",
              "<b>Le maintien</b> : avant de remonter, l'enveloppe rapide attend une demi-période du grave, mesurée en continu (jusqu'à 9 à 30 ms selon le style, moins sur les attaques). Le gain ne suit plus chaque oscillation d'une basse ou d'une corde grave : c'est ce qui supprime la distorsion des basses. Elle remonte ensuite vers le niveau de l'enveloppe lente.",
              "<b>L'enveloppe lente</b> suit une part de la réduction (selon le style) avec une attaque plus lente. Elle « nivelle » le corps du signal : sur un passage dense, le gain descend un peu et y reste, si bien que l'enveloppe rapide a moins à faire. C'est ce qui permet de pousser fort sans pompage ni distorsion.",
              "Le gain appliqué est le plus bas des deux, jamais plus haut que ce qu'il faut pour respecter le plafond."])
f.append(P("Relâchement adaptatif (AUTO)", "h2"))
f.append(P("LMix mesure en continu le facteur de crête du signal (écart entre les crêtes et le niveau moyen sur 300 ms). Musique dense, peu de crêtes qui dépassent : le relâchement s'allonge (jusqu'à 3 fois), pour éviter le pompage. Transitoires isolées sur un fond calme : il raccourcit (jusqu'à 0,35 fois), pour que le son remonte tout de suite après l'impact."))
f.append(P("Plafond garanti", "h2"))
f.append(P("Construction mathématique : le gain appliqué à chaque échantillon ne dépasse jamais le gain requis par ses voisins. L'étage de sécurité final mesure les crêtes entre les échantillons de deux façons : avec le filtre officiel de la norme <b>ITU-R BS.1770-4</b> (celui des vumètres des plateformes) et avec un interpolateur 8x plus fin, puis il garde la plus haute des deux."))
f.append(P("Vérification sur une musique de test poussée de +14 dB, plafond −1,0 dBTP, 1x / 4x / 8x / 32x, styles Transparent, Punch (multibande), Loud, Modern et Aggressive : crête mesurée −1,000 dBTP dans tous les cas, avec les deux méthodes de mesure. Sur un signal sans réduction, le traitement est transparent (écart de gain 0,002 dB).", "small"))
f.append(P("Distorsion mesurée", "h2"))
f.append(P("THD+N (harmoniques + bruit, de 20 Hz à 20 kHz) sur un sinus tenu 6 dB au-dessus du plafond, suréchantillonnage 4x :"))
f.append(table([["Style", "40 Hz", "100 Hz", "1 kHz"],
                ["Transparent, Punch, Dynamic, Bus, Safe, Modern", "−72 dB", "−81 dB", "−89 dB"],
                ["Loud (écrêtage interne)", "−39 dB", "−54 dB", "−82 dB"],
                ["Aggressive (saturé par choix)", "−24 dB", "−23 dB", "−24 dB"]],
               [70 * mm, (W - 70 * mm) / 3, (W - 70 * mm) / 3, (W - 70 * mm) / 3]))
f.append(P("Sur un son tenu, le gain reste stable au lieu de suivre la forme d'onde : un sinus de 9 kHz limité de 6 dB ressort sans aucun produit de distorsion mesurable (−138 dB), à tous les suréchantillonnages.", "small"))

# ---------- 5. Réglages ----------
f.append(P("5. Les réglages", "h1"))
f.append(img(stylep, W)); f.append(Spacer(1, 2 * mm))
f.append(P("GAIN", "h2"))
f.append(P("−12 à +30 dB. C'est le réglage principal : plus de gain, plus de niveau et plus de réduction. Regardez la réduction de gain (rouge) : jusqu'à 2 à 3 dB, la limitation reste en général inaudible ; au-delà de 6 dB, le choix du style compte beaucoup."))
f.append(P("STYLE", "h2"))
f.append(table([["Style", "Caractère", "Ce qui change"],
                ["Transparent", "Le plus propre, nivellement lent", "Enveloppe lente très présente (90 %), relâchement lent ; aucun écrêtage."],
                ["Punch", "Garde la grosse caisse et la caisse claire", "Remonte vite après chaque frappe ; détection des transitoires forte (60 %)."],
                ["Dynamic", "Garde le plus de dynamique", "Genou doux de 3 dB, peu de nivellement (30 %)."],
                ["Loud", "Rapide, dense et fort", "Relâchement court, écrêtage interne 30 % (sur les crêtes courtes)."],
                ["Bus", "Colle un bus ou des stems", "Genou doux de 2 dB, relâchement plus long et souple."],
                ["Safe", "Lent et sûr, distorsion minimale", "Tout le travail sur l'enveloppe lente, relâchement le plus long."],
                ["Modern", "Master moderne fort et clair (par défaut)", "Nivellement 75 %, relâchement court, écrêtage interne 20 % (sur les crêtes courtes), transitoires 40 %."],
                ["Aggressive", "Dur, en avant, saturé", "Relâchement très court, maintien court, écrêtage interne 50 % : sature aussi les sons forts tenus, par choix."]],
               [24 * mm, 52 * mm, W - 76 * mm]))
f.append(P("L'écrêtage et la détection de transitoires internes d'un style s'ajoutent aux potards CLIP et TRANSIENT (qui vont alors de la valeur du style à 100 %). Clic droit sur un style : MIDI Learn du style.", "small"))
f.append(P("Réglages fins", "h2"))
f.append(table([["Réglage", "Plage", "Rôle"],
                ["ATTACK", "0 à 100 % (50 %)", "Vitesse de l'enveloppe lente : de 4 fois plus rapide (0 %) à 4 fois plus lente (100 %) que celle du style. Plus lent : plus de transitoires, plus de travail pour l'enveloppe rapide."],
                ["RELEASE", "1 à 1 000 ms (100 ms)", "Échelle de temps du relâchement : chaque style en tire son relâchement rapide (une fraction) et lent (un multiple). Court : plus fort, plus de distorsion ; long : plus doux, risque de pompage."],
                ["AUTO", "marche / arrêt", "Relâchement adaptatif (section 4). Bouton sous RELEASE ou clic droit sur RELEASE."],
                ["LOOKAHEAD", "0 à 5 ms (2 ms)", "Avance : durée de la descente de gain avant la crête. Plus long : plus doux sur les attaques ; 0 : coupure nette. Ne change pas la latence (5 ms sont toujours réservées)."],
                ["LINK TRANS.", "0 à 100 % (100 %)", "Liaison stéréo des transitoires : à 100 %, une crête à gauche baisse aussi la droite (image stable)."],
                ["LINK REL.", "0 à 100 % (50 %)", "Liaison stéréo du relâchement : plus bas, chaque côté remonte de lui-même (image plus large, possible flottement)."],
                ["TRANSIENT", "0 à 100 % (0 %)", "Sur une attaque, accélère le relâchement (jusqu'à 5 fois) et suspend le nivellement : l'impact passe, le corps n'est pas écrasé."],
                ["ENHANCE", "0 à 100 % (0 %)", "Remonte les passages plus faibles que la moyenne des 3 dernières secondes, jusqu'à +6 dB à 100 % (jamais sous −60 dBFS, donc pas sur le silence)."],
                ["CLIP", "0 à 100 % (0 %)", "Écrêteur doux avant le limiteur : seuil de +6 dB au-dessus du plafond (peu) jusqu'au plafond (100 %), relevé de la réduction déjà portée par l'enveloppe lente. Il ne coupe donc que les crêtes courtes qui dépassent le niveau nivelé, et le limiteur a moins à faire."]],
               [24 * mm, 30 * mm, W - 54 * mm]))
f.append(P("Potards : glisser verticalement (Maj : réglage fin), molette, <b>double-clic</b> : valeur par défaut, <b>clic droit</b> : MIDI Learn.", "small"))

# ---------- 6. Sortie ----------
f.append(CondPageBreak(80 * mm))
f.append(P("6. Sortie", "h1"))
ot = Table([[[P("<b>CEILING</b> (−30 à 0 dB, −1,0 par défaut) : plafond de sortie. Avec TRUE PEAK, il s'exprime en dBTP (crêtes entre échantillons comprises) ; sans, en dBFS (crêtes d'échantillons seulement). Les plateformes recommandent −1 dBTP (−2 dBTP pour Amazon)."),
              P("<b>TRUE PEAK</b> : marche par défaut. À couper seulement pour une étape intermédiaire.")],
             [img(outp, 60 * mm)]]], colWidths=[W - 64 * mm, 64 * mm])
ot.setStyle(TableStyle([("VALIGN", (0, 0), (-1, -1), "TOP"), ("LEFTPADDING", (0, 0), (0, 0), 0)]))
f.append(ot)
f += bullets(["<b>OS</b> (suréchantillonnage) : clic pour choisir 1x à 32x (4x par défaut, phase linéaire). Plus haut : moins de repliement quand le limiteur et l'écrêteur travaillent fort, au prix du processeur. 8x est un bon choix pour le rendu final ; 32x demande environ 4 fois plus de calcul que 4x.",
              "<b>UNITY</b> : baisse la sortie de la valeur du GAIN, pour comparer avec et sans limitation au même volume. À couper avant l'export.",
              "<b>DELTA</b> : on n'entend que ce que le limiteur retire (différence entre l'entrée et la sortie). Idéal pour régler RELEASE et le style : si on y entend la musique, c'est trop. Sans réduction, le résidu est à −63 dB.",
              "<b>DITHER</b> : clic pour Off, 16 bit (CD) ou 24 bit, et <b>Noise shaping</b> (mise en forme du bruit vers l'aigu, moins audible). Dither TPDF, en toute fin de chaîne ; à activer seulement pour l'export final à la résolution choisie. Coupé en DELTA.",
              "<b>MULTIBAND</b> : section 7."])
f.append(table([["Suréchantillonnage", "1x", "2x", "4x", "8x", "16x", "32x"],
                ["Latence à 44,1 kHz", "8,5 ms", "10,7 ms", "11,3 ms", "11,5 ms", "11,6 ms", "11,6 ms"],
                ["Latence à 48 kHz", "8,5 ms", "10,5 ms", "11,0 ms", "11,2 ms", "11,3 ms", "11,3 ms"]],
               [36 * mm] + [(W - 36 * mm) / 6] * 6))
f.append(P("La latence est annoncée au DAW, qui la compense ; elle ne change qu'avec le suréchantillonnage (LOOKAHEAD, MULTIBAND et TRUE PEAK n'y touchent pas). Changer de suréchantillonnage fait un court fondu (30 ms).", "small"))

# ---------- 7. Multibande ----------
f.append(P("7. Mode multibande", "h1"))
f.append(P("Avec <b>MULTIBAND</b>, le signal est partagé en 4 bandes à <b>reconstruction parfaite</b> : chaque bande est soustraite du reste, si bien que la somme redonne exactement le signal d'entrée, sans rotation de phase (une rotation de phase ferait monter les crêtes des attaques et coûterait de la sonie). LMix suit la part <b>tenue</b> de la réduction nécessaire (attaque 25 ms, retour 300 ms) et la confie à la bande qui porte le plus d'énergie ; les autres bandes en prennent en proportion. Le limiteur principal garde les crêtes et garantit toujours le plafond. Exemple typique : une grosse caisse ou une guitare 7 cordes très grave ne fait plus baisser les cymbales et la voix (sur la musique de test, environ 1 dB d'aigus préservé)."))
f.append(img(mbshot, W)); f.append(P("Mode multibande (preset Metal Master) : les bandes LOW et LOW MID prennent la réduction, l'aigu reste presque intact.", "small"))
f += bullets(["L'écran se partage : historique en haut, bandes en bas avec leur réduction en direct.",
              "<b>Glisser une poignée</b> (110 Hz, 900 Hz…) déplace une fréquence de coupure : 40 à 600 Hz, 300 Hz à 4 kHz et 2 à 16 kHz, toujours dans l'ordre. Elles sont aussi automatisables (Crossover Low / Mid / High).",
              "Allumer ou couper le mode fait un fondu de 30 ms ; coupé, le signal n'est pas filtré du tout."])

# ---------- 8. Mesures ----------
f.append(P("8. Mesures", "h1"))
mt = Table([[[P("<b>Historique</b> : entrée (après GAIN, avant limitation, en gris), sortie (blanc) et réduction de gain (rouge, depuis le haut ; 4,5 unités par dB). Les deux courbes sont alignées dans le temps. 200 points par seconde."),
              P("<b>IN / OUT</b> : crêtes de −60 à 0 dB avec maintien 1,5 s ; la valeur maximale s'affiche dessous. Le trait rouge sur OUT est le plafond."),
              P("<b>GAIN REDUCTION</b> : 6 dernières secondes et maximum depuis la dernière remise à zéro (clic sur la case).")],
             [img(meters, 52 * mm)]]], colWidths=[W - 56 * mm, 56 * mm])
mt.setStyle(TableStyle([("VALIGN", (0, 0), (-1, -1), "TOP"), ("LEFTPADDING", (0, 0), (0, 0), 0)]))
f.append(mt)
f.append(P("Sonie (EBU R128 / ITU-R BS.1770-4)", "h2"))
f.append(table([["Mesure", "Définition"],
                ["INTEGRATED", "Sonie de tout ce qui a été joué depuis la remise à zéro (fenêtres de 400 ms, seuils −70 LUFS et −10 LU). Verte à ±1 LU de la cible, rouge au-dessus, blanche en dessous."],
                ["SHORT-TERM", "Sonie sur les 3 dernières secondes."],
                ["MOMENTARY", "Sonie sur les 400 dernières millisecondes."],
                ["RANGE (LRA)", "Plage de sonie (EBU Tech 3342) : écart entre les passages forts et faibles. Un master très limité descend sous 4 LU."],
                ["TRUE PEAK", "Crête maximale entre échantillons (filtre BS.1770-4). Rouge si elle dépasse le true peak de la cible."],
                ["PLR", "True peak moins sonie intégrée : marge dynamique. Plus elle est petite, plus le master est écrasé."]],
               [28 * mm, W - 28 * mm]))
f.append(P("Vérification : un sinus de 997 Hz à −23 dBFS sur les deux canaux est mesuré à −23,0 LUFS. Clic sur la zone LOUDNESS (ou clic droit &gt; Reset loudness meters) : remise à zéro. Les mesures portent sur la sortie limitée, avant UNITY, DELTA et le dither.", "small"))
f.append(P("Cibles", "h2"))
f.append(table([["Cible", "Sonie", "True peak"],
                ["Spotify / Tidal (par défaut)", "−14 LUFS", "−1 dBTP"], ["Apple Music", "−16 LUFS", "−1 dBTP"], ["YouTube", "−14 LUFS", "−1 dBTP"],
                ["Amazon Music", "−14 LUFS", "−2 dBTP"], ["Deezer", "−15 LUFS", "−1 dBTP"], ["SoundCloud", "−14 LUFS", "−1 dBTP"],
                ["CD / Club", "−9 LUFS", "−0,3 dBTP"], ["EBU R128 (diffusion)", "−23 LUFS", "−1 dBTP"], ["ATSC A/85", "−24 LKFS", "−2 dBTP"]],
               [60 * mm, 40 * mm, 40 * mm]))
f.append(P("<b>Match gain to target</b> (menu de TARGET) : après avoir joué le morceau (ou au moins son passage le plus fort), corrige le GAIN de l'écart entre la sonie intégrée et la cible, puis remet les mesures à zéro. Rejouez pour vérifier. Les plateformes baissent les masters trop forts : viser plus haut que la cible n'apporte pas de volume en plus."))

# ---------- 9. MIDI ----------
f.append(P("9. MIDI et automation", "h1"))
f += bullets(["Tous les réglages sont automatisables dans le DAW (Gain, Ceiling, Style, Attack, Release, Auto Release, Lookahead, Link Transients, Link Release, Transient, Enhance, Clip, True Peak, Oversampling, Unity Gain, Delta, Dither, Noise Shaping, DC Filter, Multiband, Crossover Low / Mid / High, Power).",
              "<b>MIDI Learn</b> : clic droit sur un potard, un style ou un bouton de sortie &gt; <b>MIDI Learn</b>, puis bougez la pédale d'expression ou le bouton MIDI. La ligne d'état affiche MIDI LEARN puis le CC appris. <b>Clear MIDI</b> retire l'assignation ; clic droit &gt; MIDI &gt; <b>Clear all MIDI</b> les retire toutes.",
              "Aucun CC n'est assigné par défaut. Un CC déjà utilisé passe au nouveau réglage. Les assignations sont sauvegardées avec le projet.",
              "La course réelle de la pédale d'expression est apprise : une pédale qui n'envoie que 2 à 116 atteint quand même 0 et 100 %."])

# ---------- 10. Fenêtre ----------
f.append(P("10. Fenêtre et ligne d'état", "h1"))
wt = Table([[[P("La fenêtre s'ouvre à la taille par défaut (<b>60 %</b>) et ne peut pas être réduite en dessous. Clic droit &gt; <b>Plugin size</b> : 60 %, 75 % ou 100 % (taille enregistrée avec le projet) ; le coin en bas à droite agrandit jusqu'à 150 % en gardant les proportions. Clic droit &gt; <b>Glow intensity</b> : 25, 50, 75 ou 100 % pour l'intensité des halos. Clic droit &gt; <b>History length</b> : 3, 6 ou 12 s."),
              P("La <b>barre de presets</b> et la <b>ligne d'état</b> partagent la même pastille. Dès qu'un réglage bouge (souris, molette, automation, MIDI), la ligne d'état prend sa place avec la valeur en direct ; elle s'efface 2,5 s après le dernier mouvement. Un clic sur la pastille fait revenir la barre de presets tout de suite.")],
             [img(bars, 66 * mm), P("Au repos (barre de presets) et pendant un réglage (ligne d'état).", "small")]]], colWidths=[W - 70 * mm, 70 * mm])
wt.setStyle(TableStyle([("VALIGN", (0, 0), (-1, -1), "TOP"), ("LEFTPADDING", (0, 0), (0, 0), 0)]))
f.append(wt)
f.append(P("Le clic droit sur le fond de la fenêtre ouvre le menu principal : Presets, Style, Oversampling, True peak limiting, Multiband, DC offset filter, Dither, Loudness target, Reset loudness meters, History length, MIDI, Plugin size, Glow intensity.", "small"))

# ---------- 11. Presets ----------
f.append(P("11. Presets", "h1"))
f.append(P("Flèches de la barre de presets, clic sur son nom, clic droit &gt; Presets, ou menu de presets du DAW. Un astérisque après le nom signale un preset modifié. <b>Save preset...</b> et <b>Load preset...</b> enregistrent et rechargent vos réglages (fichiers .lmix, dossier Documents/Nine Hands DSP/LMix par défaut). Les presets ne touchent ni à Power, ni à UNITY, DELTA et DITHER. Les réglages non cités gardent leur valeur par défaut."))
f.append(table([["Preset", "Contenu"],
                ["Default", "Modern, GAIN 0 dB, CEILING −1,0 dBTP, RELEASE 100 ms AUTO, LOOKAHEAD 2 ms, OS 4x."],
                ["Modern Master", "Modern, GAIN +6 dB, RELEASE 120 ms, TRANSIENT 30 %, CLIP 15 %, OS 8x."],
                ["Transparent Master", "Transparent, GAIN +3 dB, RELEASE 250 ms, LOOKAHEAD 3 ms."],
                ["Streaming -14 LUFS", "Transparent, GAIN +4 dB, RELEASE 180 ms, LOOKAHEAD 3 ms, CEILING −1,0 dBTP. Ajustez avec Match gain to target."],
                ["Club Loud", "Loud, GAIN +9 dB, CEILING −0,3 dBTP, RELEASE 60 ms, CLIP 30 %, OS 8x."],
                ["Metal Master (7/8 strings)", "Modern, GAIN +6 dB, MULTIBAND (110 Hz, 900 Hz, 5,5 kHz), TRANSIENT 40 %, RELEASE 90 ms, CLIP 10 %, OS 8x."],
                ["Bass Control (Multiband)", "Dynamic, GAIN +4 dB, MULTIBAND, coupure basse 150 Hz."],
                ["Punchy Drum Bus", "Punch, GAIN +5 dB, TRANSIENT 60 %, LOOKAHEAD 1 ms, RELEASE 80 ms, TRUE PEAK coupé, OS 2x."],
                ["Mix Bus Glue", "Bus, GAIN +3 dB, RELEASE 300 ms, ATTACK 60 %, CEILING −0,5 dBTP."],
                ["Aggressive EDM", "Aggressive, GAIN +10 dB, CLIP 40 %, RELEASE 40 ms, CEILING −0,3 dBTP, OS 8x."],
                ["Enhanced Detail", "Transparent, GAIN +4 dB, ENHANCE 40 %."],
                ["Broadcast EBU R128", "Safe, LOOKAHEAD 5 ms, RELEASE 300 ms (cible EBU R128 à choisir dans TARGET)."],
                ["Safety Limiter", "Safe, CEILING −0,3 dBTP, OS 2x : simple garde-fou en bout de chaîne."]],
               [44 * mm, W - 44 * mm]))

# ---------- 12. Conseils ----------
f.append(P("12. Conseils d'utilisation", "h1"))
f += bullets(["Mettez LMix <b>en dernier</b>. Faites l'égalisation (QMix) et la compression (CompMix) avant lui.",
              "Méthode simple : choisissez un preset proche, montez le GAIN jusqu'à la sonie voulue, puis écoutez en <b>UNITY</b> pour juger la limitation à volume égal, et en <b>DELTA</b> pour régler RELEASE et le style.",
              "Pour du metal en 7 ou 8 cordes : partez de <b>Metal Master</b>. Le multibande empêche la corde grave et la grosse caisse de faire baisser les guitares et la voix. Si le grave pompe, montez RELEASE ou passez en Transparent ; si les attaques s'émoussent, montez TRANSIENT.",
              "Pour le streaming, inutile de dépasser la cible : les plateformes ramènent tout à −14 LUFS (Apple : −16). Un master à −14 LUFS bien dynamique sonnera aussi fort, et plus vivant, qu'un master écrasé à −8.",
              "Pour l'export final : OS 8x ou plus, TRUE PEAK, CEILING −1,0 dBTP ; DITHER 16 bit pour un fichier 16 bits, 24 bit pour un fichier 24 bits, rien pour un fichier en virgule flottante.",
              "Si vous entendez de la distorsion dans le grave : RELEASE plus long, LOOKAHEAD plus long, ou style Safe / Transparent."])

# ---------- 13. Caractéristiques ----------
f.append(KeepTogether([P("13. Caractéristiques techniques", "h1"), table([
    ["Format", "VST3 (Windows 64 bits, macOS universel), mono ou stéréo"],
    ["Plafond", "−30 à 0 dB, true peak (BS.1770-4 + interpolateur 8x de 192 coefficients) ou crête d'échantillon ; garanti par construction"],
    ["Limiteur", "deux enveloppes (rapide avec avance 0 à 5 ms et maintien calé sur la demi-période du grave, lente de nivellement), détection des crêtes entre échantillons, lissage par minimum glissant + double moyenne, relâchement adaptatif selon le facteur de crête, genou doux selon le style"],
    ["Suréchantillonnage", "1x à 32x, filtres demi-bande RIF à phase linéaire, pour l'écrêteur et pour le limiteur ; latence 8,5 à 11,6 ms selon le facteur et la fréquence d'échantillonnage"],
    ["Multibande", "4 bandes à reconstruction parfaite (Linkwitz-Riley 24 dB/oct soustractifs), avance 2 ms, réduction lente répartie selon l'énergie de chaque bande"],
    ["Distorsion", "THD+N à 6 dB de réduction : −72 dB (40 Hz), −81 dB (100 Hz), −89 dB (1 kHz) en Modern, 4x"],
    ["Mesures", "EBU R128 / BS.1770-4 : momentanée, court terme, intégrée (seuils −70 / −10), LRA (EBU Tech 3342), true peak, PLR ; historique 200 points/s"],
    ["Sortie", "filtre DC 5 Hz, UNITY, DELTA, dither TPDF 16 / 24 bits avec mise en forme du bruit (5 coefficients) ; avec le dither, le plafond garde 2 pas de quantification de marge"],
    ["Charge processeur", "environ 7 % d'un cœur à 4x, 10 % à 8x, 31 % à 32x (48 kHz, mesure indicative)"],
    ["MIDI", "MIDI Learn sur tous les réglages, aucun CC par défaut"],
    ["Fenêtre", "60 / 75 / 100 %, jusqu'à 150 %, jamais sous 60 % ; anti-gel v2 sous Windows"]],
    [30 * mm, W - 30 * mm], header=False)]))

def deco(c, doc):
    c.saveState()
    c.setFillColor(DARK); c.rect(0, A4[1] - 6 * mm, A4[0], 6 * mm, stroke=0, fill=1)
    c.setFillColor(ACC); c.rect(0, A4[1] - 6.8 * mm, A4[0], 0.8 * mm, stroke=0, fill=1)
    c.setFont("Pop", 7.5); c.setFillColor(GREY)
    c.drawString(18 * mm, 10 * mm, "LMix (NINE HANDS | DSP) · v" + VERSION)
    c.drawRightString(A4[0] - 18 * mm, 10 * mm, str(doc.page))
    c.restoreState()

# ---- Couverture « minimale » (modèle 6) : gabarit fixe commun à toutes les notices ----
COVER_NAME = "LMIX"; COVER_SUB = "Limiteur de mastering"
PEDAL_H, PEDAL_MAXW, PEDAL_TOP = 82 * mm, 115 * mm, 84 * mm
LANG_TOP = 18 * mm
NAME_TOP, NAME_PT = 194 * mm, 30
SUB_TOP, SUB_PT = 208 * mm, 10
LOGO_W, LOGO_BOTTOM = 38 * mm, 22 * mm
BAND_H = 3 * mm
def cover_bg(c, doc):
    c.saveState(); c.setFillColor(colors.black); c.rect(0, 0, A4[0], A4[1], stroke=0, fill=1)
    c.setFillColor(ACC); c.rect(0, 0, A4[0], BAND_H, stroke=0, fill=1)
    def line(text, top, font, size, cs, col):
        w = pdfmetrics.stringWidth(text, font, size) + cs * (len(text) - 1)
        t = c.beginText((A4[0] - w) / 2, A4[1] - top - size * 0.8); t.setFont(font, size); t.setCharSpace(cs)
        t.setFillColor(col); t.textOut(text); c.drawText(t)
    line("FRANÇAIS", LANG_TOP, "PopM", 8.5, 0, ACC_L)
    ci = Image.open(cover); iw, ih = ci.size
    kk = min(PEDAL_H / COVER_CH, PEDAL_MAXW / COVER_CW)
    dw, dh = iw * kk, ih * kk
    y0 = A4[1] - (PEDAL_TOP + (PEDAL_H - COVER_CH * kk) / 2) + COVER_PAD * kk
    c.drawImage(cover, (A4[0] - dw) / 2, y0 - dh, dw, dh)
    line(COVER_NAME, NAME_TOP - NAME_PT * 0.02, "Title", NAME_PT, NAME_PT * 0.5, colors.white)
    line(COVER_SUB, SUB_TOP, "Pop", SUB_PT, 1.5, colors.HexColor("#BDC4B8"))
    lw, lh = Image.open(LOGO_PNG).size
    c.drawImage(LOGO_PNG, (A4[0] - LOGO_W) / 2, LOGO_BOTTOM, LOGO_W, LOGO_W * lh / lw, mask="auto")
    c.restoreState()

doc = BaseDocTemplate(OUT, pagesize=A4, leftMargin=18 * mm, rightMargin=18 * mm, topMargin=16 * mm, bottomMargin=18 * mm,
                      title="LMIX - Notice v" + VERSION, author="NINE HANDS | DSP")
doc.addPageTemplates([PageTemplate(id="cover", frames=[Frame(doc.leftMargin, doc.bottomMargin, doc.width, doc.height, id="fc")], onPage=cover_bg),
                      PageTemplate(id="p", frames=[Frame(doc.leftMargin, doc.bottomMargin, doc.width, doc.height, id="f")], onPage=deco)])
doc.build(f)
print(OUT)
