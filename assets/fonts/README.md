# Police d'interface ValCraft

Je versionne ici **Montserrat Regular variable**, provenant du répertoire
officiel [Google Fonts](https://github.com/google/fonts/tree/main/ofl/montserrat).
Les auteurs indiqués par la distribution sont les auteurs du projet
Montserrat.Git. La redistribution est couverte par la SIL Open Font License
1.1 incluse dans `OFL.txt`.

- Source : `Montserrat-wght.ttf`
- SHA-256 :
  `0f7b311b2f3279e4eef9b2f968bcdbab6e28f4daeb1f049f4f278a902bcd82f7`
- Licence : `OFL.txt`
- Atlas généré : `valcraft_ui_font.msdfa`

## Nature de l'atlas

La version 1 utilise un **SDF RGB multi-canal déterministe**, et non un MSDF à
coloration topologique des arêtes. Le canal vert contient la distance signée
centrale ; rouge et bleu l'échantillonnent respectivement à -1/3 et +1/3 de
pixel. La médiane RGB reste donc une reconstruction stable tout en offrant une
information subpixel. Ce choix ne revient pas à la police historique 5×7 :
chaque glyphe provient directement des courbes TrueType de Montserrat.

Le générateur écrit les métriques, le kerning, toute la chaîne de mipmaps et un
checksum FNV-1a 64 bits. La commande suivante doit être octet-identique :

Lorsque la version de Pillow ne dispose pas de `libraqm`, son moteur basique
n'applique pas le GPOS de la police variable. La recette contient alors une
petite table de kerning optique, exprimée en `em`, pour les paires UI les plus
visibles (`AV`, `To`, etc.). Le GPOS natif reste prioritaire dès qu'il est
exposé par le moteur de police.

```powershell
python tools/generate_msdf_font_atlas.py --check
```
