# ValCraft

<p align="center">
  <strong>Sandbox voxel solo en C++20 / OpenGL, avec une aventure maritime devenue le coeur du projet</strong>
  <br>
  ValCraft est mon remake libre "dans l'esprit de Minecraft" : un monde procedural, une boucle FPS immediate,
  un terrain modifiable en temps reel et, surtout, un grand voyage a bord de L'Amelie.
</p>

<p align="center">
  <a href="https://github.com/Donj63000/ValCraft-Official-Game-/actions/workflows/strict.yml">
    <img src="https://github.com/Donj63000/ValCraft-Official-Game-/actions/workflows/strict.yml/badge.svg" alt="CI Strict">
  </a>
  <img src="https://img.shields.io/badge/C%2B%2B-20-00599C" alt="C++20">
  <img src="https://img.shields.io/badge/CMake-3.24%2B-064F8C" alt="CMake 3.24+">
  <img src="https://img.shields.io/badge/OpenGL-3.3%20Core-5586A4" alt="OpenGL 3.3 Core">
  <img src="https://img.shields.io/badge/Platform-Windows-0078D6" alt="Windows">
  <img src="https://img.shields.io/badge/License-Apache%202.0-2EA043" alt="Apache 2.0">
</p>

<p align="center">
  <a href="#apercu">Apercu</a> |
  <a href="#aventures-en-mer">Aventures en mer</a> |
  <a href="#du-voxel-au-procedural-sans-perdre-la-grille">Transition visuelle</a> |
  <a href="#points-forts">Points forts</a> |
  <a href="#captures">Captures</a> |
  <a href="#fonctionnalites">Fonctionnalites</a> |
  <a href="#etat-actuel">Etat actuel</a> |
  <a href="#demarrage-rapide">Demarrage rapide</a> |
  <a href="#controles">Controles</a> |
  <a href="#architecture">Architecture</a> |
  <a href="#roadmap">Roadmap</a>
</p>

<p align="center">
  <img src="Images/img.png" alt="Capture principale de ValCraft" width="920">
  <br>
  <em>Un sandbox voxel jouable dont le chantier principal est maintenant l'exploration et la survie en mer.</em>
</p>

## Apercu

ValCraft est un projet personnel de sandbox voxel developpe en `C++20`, `SDL2` et `OpenGL 3.3 Core`.
L'objectif n'est pas seulement de refaire une boucle de jeu "a la Minecraft", mais de construire une base
technique propre, testee et evolutive pour un vrai projet de jeu.

Aujourd'hui, le depot propose deja deux facettes d'une meme experience : le sandbox voxel classique
et **Aventures en mer**, le mode sur lequel je concentre l'essentiel du developpement. On peut explorer
un monde procedural en vue FPS, modifier le terrain, puis prendre la mer depuis un port complet a bord
de L'Amelie avec son equipage, ses ressources, ses dangers et sa propre progression.

Le projet reste en developpement actif. La base est solide, le coeur sandbox est deja la, mais il reste
encore des systemes a approfondir, des bugs a corriger et du contenu a enrichir.

## Aventures en mer

> **C'est la priorite actuelle de ValCraft.** Le mode `AVENTURE EN MER` n'est pas un simple decor pose
> sur le sandbox : il possede son monde, sa boucle de survie, son navire mobile, son equipage, ses combats,
> son ambiance sonore et sa sauvegarde.

### Du port a la haute mer

Une nouvelle aventure commence dans un **port genere de facon deterministe** et directement explorable.
Le quai de pierre, la passerelle, le ponton, les digues, le phare, la grue, la capitainerie, l'entrepot,
les cargaisons, les bollards et les lanternes composent un vrai point de depart, tout en laissant a
L'Amelie un chenal libre pour appareiller.

Le voyage suit trois phases lisibles :

1. **Amarre** : le navire attend au port tant que le joueur n'est pas reellement monte a bord.
2. **Depart** : les amarres sont larguees et L'Amelie accelere progressivement.
3. **En route** : le navire avance en haute mer dans un monde d'archipels, de recifs et d'iles
   deterministes, avec un corridor de navigation protege des obstacles.

Le joueur reste libre pendant la traversee. Il peut parcourir les ponts, entrer dans les espaces
interieurs, sauter, nager, quitter le navire ou rejoindre les terres rencontrees. Le moteur distingue
une vraie prise d'appui sur le pont d'un joueur en chute, en vol ou dans l'eau.

### L'Amelie, un navire jouable et mobile

L'Amelie est une entite dynamique separee des chunks du monde. Elle se deplace sans reecrire le terrain
et suit la houle avec pilonnement, tangage et roulis. Sa transformation transporte correctement le joueur,
l'equipage et les objets poses a bord, tandis que les collisions de coque continuent de repousser les
occupants sans les enfermer.

Le navire comprend notamment :

- une coque profilee avec plusieurs ponts praticables et des espaces interieurs abrites
- la cabine du capitaine, les quartiers d'equipage, la cuisine, la cale et les zones de cargaison
- une barre, des ecoutilles et un reseau de circulation utilise par les marins
- des mats, des voiles, du greement, un cabestan et des filets d'abordage grimpables
- douze canons avec leurs sabords, ainsi que des fanaux et lanternes integres au rendu
- un rendu moderne detaille et un pipeline Legacy conserve pour les anciennes sauvegardes

### Survie, peche et exploration

La traversee repose sur une boucle de survie propre au mode maritime :

- **faim, soif et endurance** evoluent pendant le voyage et sont affichees dans le HUD maritime
- les stocks suivent les rations, l'eau potable, le poisson, le bois, la pierre et les fibres
- les repas et les reserves d'eau peuvent etre servis automatiquement lorsque le joueur est a bord
- la peche est une action jouable, influencee par le moment de la journee et les conditions de mer
- la distance au navire est surveillee : s'en eloigner trop longtemps en pleine mer expose a l'isolement
- le respawn restaure un etat de survie sain sans effacer la progression ni remplir artificiellement les stocks

Le HUD regroupe aussi la phase du depart, la distance parcourue, la vitesse du navire, la progression
de la peche et les informations contextuelles sur les personnages vises.

### Un equipage qui vit et travaille

L'Amelie embarque **six membres d'equipage persistants**, chacun avec une identite, une apparence,
un poste et une routine :

| Role | Fonction a bord |
| --- | --- |
| Capitaine | Tient la barre, controle la route et surveille la poupe. |
| Pecheur | Peche pour les vivres puis transporte sa prise jusqu'a la cale. |
| Gabier | Securise le greement, travaille aux mats et manoeuvre le cabestan. |
| Maitre d'eau | Prepare l'eau potable et profite de la pluie lorsque la meteo le permet. |
| Matelot de pont | Entretient le pont, participe aux manoeuvres et prend ses quarts de repos. |
| Quartier-maitre | Organise les reserves et les cargaisons dans les ponts inferieurs. |

Les marins se deplacent sur un graphe de navigation relie entre les ponts, les escaliers et les pieces.
Ils cedent le passage au joueur, transportent visuellement leurs cargaisons, suspendent certaines taches
pendant les fortes tempetes et reprennent leur travail sans perdre leur progression. En les visant, le
HUD indique leur role, leur activite, leur destination et l'avancement de leur tache.

### La Vieille Garde

Six soldats de la **Vieille Garde** patrouillent sur des rondes distinctes et protegent le navire contre
les creatures devenues hostiles. Leur combat comprend perception, memoire de cible, verification du
terrain et des lignes alliees, mise en joue, tir au mousquet, rechargement et attaque a la baionnette.
Les tirs produisent un flash et une fumee de poudre noire soumis au vent, tout en suivant exactement le
mouvement et l'inclinaison de L'Amelie.

### Un ocean vivant et une ambiance reactive

Le profil de monde maritime genere une mer continue, des fonds, des recifs et des archipels repartis
autour d'une route navigable. La houle est analytique et continue ; elle reste visible par temps calme
et peut atteindre plusieurs metres pendant les tempetes. Le vent, la pluie, les nuages, la nuit et
l'intensite de la mer influencent le voyage, la vitesse, la peche, l'equipage et les combats.

La musique procedurale accompagne elle aussi le parcours : elle evolue entre l'attente au port, le depart
et la haute mer, puis reagit a la nuit, aux tempetes et au danger sans abandonner l'identite musicale
du reste de ValCraft.

### Sauvegarde et compatibilite

Une sauvegarde maritime conserve la phase du voyage, la position et la distance du navire, les jauges
de survie, les ressources, la peche, l'equipage et la Vieille Garde. Les anciennes sauvegardes dans
lesquelles le navire etait encore grave dans les chunks sont migrees progressivement vers l'entite mobile,
avec reconciliation du joueur et des objets presents a bord.

La generation, les routines, la peche et les migrations restent deterministes a seed identique. Des tests
dedies couvrent le port, le corridor oceanique, la physique du navire, l'equipage, la Vieille Garde,
la houle, la musique, les sauvegardes historiques et les deux pipelines de rendu.

### Ce que je developpe en ce moment

Le travail actuel approfondit en priorite cette aventure maritime :

- refonte visuelle et amenagement complet des ponts interieurs de L'Amelie
- transition du rendu voxel vers une geometrie procedurale organique toujours ancree sur la grille
- eclairage local des pieces, lanternes et fanaux exterieurs selon la meteo
- horizon marin lointain et transitions propres entre chunks detailles et decor de haute mer
- vie oceanique, bancs de poissons et vegetation sous-marine avec budgets de performance stricts
- enrichissement du port, de l'equipage, des combats et de la lisibilite du voyage
- optimisation du streaming, du rendu de l'eau, des ombres et des performances en pleine mer

Ces chantiers sont en cours de consolidation : le but est de faire d'**Aventures en mer** une vraie
experience centrale de ValCraft, pas un mode secondaire ajoute a la fin.

## Du voxel au procedural, sans perdre la grille

ValCraft evolue progressivement vers un rendu **procedural non-voxel**, mais il ne renie pas sa
fondation. La grille reste le squelette du jeu ; le nouveau pipeline change la facon dont cette grille
est dessinee, pas la facon dont le monde fonctionne.

### La grille voxel reste la source de verite

Chaque bloc continue d'exister dans une cellule entiere, avec un `BlockId` stable et des coordonnees
monde deterministes. Cette couche logique conserve :

- la generation des chunks et des sections
- la pose, la casse et le remplacement des blocs
- les collisions, la navigation et les raycasts de selection
- l'eau, la lumiere, les ressources et les interactions de gameplay
- les sauvegardes, les migrations et la reproductibilite par seed

Le pipeline moderne ne modifie jamais ces donnees pour obtenir une forme plus douce. Une requete sur
la surface visuelle est reservee au rendu ; elle ne peut pas devenir une collision, une regle de jeu
ou une information sauvegardee. Basculer entre les rendus moderne et Legacy reconstruit uniquement
les caches graphiques et laisse la partie strictement identique.

| Grille voxel : logique du jeu | Procedural moderne : presentation |
| --- | --- |
| Cellules entieres, chunks, `BlockId` et coordonnees stables. | Sommets, normales, materiaux et niveaux de detail reconstruits depuis ces cellules. |
| Casse, pose, raycast, collision, eau, lumiere et sauvegarde. | Silhouettes plus organiques, surfaces continues, mobilier, vegetation, navire et horizon. |
| Topologie et limites exactes du monde jouable. | Deplacements visuels bornes qui ne changent ni les tunnels, ni les murs, ni les passages. |
| Identite deterministe partagee par tous les pipelines. | Variations procedurales derivees de la seed et des coordonnees de la grille. |

### Une geometrie organique construite depuis les cellules

Le terrain naturel utilise un maillage de type **Surface Nets** : le moteur echantillonne l'occupation
de la grille, cree des sommets partages dans ses cellules duales et lisse les normales entre les faces.
Les sommets peuvent se deplacer legerement autour de leur ancrage logique, dans une enveloppe inferieure
a une demi-cellule. On obtient des collines, falaises, cavites et tunnels moins cubiques sans deplacer
la collision ni changer la topologie voxel.

Ce calcul reste local, deterministe et raccorde exactement les sections voisines, y compris aux
coordonnees negatives. Les halos de cellules servent a calculer des normales continues, mais ne peuvent
ni ajouter une face ni modifier la forme logique. Les familles geologiques issues des blocs sont ensuite
melangees progressivement pour eviter une frontiere artificielle entre herbe, terre, roche, sable et minerais.

### Des constructions nettes, toujours guidees par la grille

Le procedural ne signifie pas que tout devient arrondi. Les blocs architecturaux conservent leurs
alignements, leurs angles droits et leurs ouvertures. Un meshing glouton fusionne les faces coplanaires
de plusieurs cellules en grands panneaux propres, puis ajoute seulement des biseaux sur les silhouettes
exposees. Les changements de materiau, de lumiere et de transparence restent des frontieres explicites.

Les arbres, plantes, creatures, objets et elements du navire suivent la meme philosophie : leurs cellules
ou leurs points d'ancrage sont regroupes en recettes procedurales deterministes, puis convertis en formes
plus lisibles avec plusieurs niveaux de detail. Une cellule canonique conserve la propriete de chaque
element aux frontieres de chunks afin d'eviter doublons, coutures et variations aleatoires au rechargement.

### Garder l'esprit voxel

L'objectif n'est pas de masquer completement la grille, mais de la rendre plus naturelle :

- les volumes restent modulaires et lisibles a l'echelle d'une cellule
- le rythme des pentes, des ponts, des batiments et des iles reste guide par des grilles
- toutes les interactions continuent de repondre instantanement au bloc vise
- les formes procedurales conservent des ancrages, des limites et des budgets mesurables
- le rendu `LegacyVoxel` reste disponible comme reference differentielle

Dans **Aventures en mer**, cette direction devient particulierement visible : la route oceanique,
les archipels et le port partent de grilles de generation, L'Amelie part d'un blueprint modulaire,
la houle ajoute un mouvement continu, et l'horizon lointain se stabilise sur une grille plus large.
Le resultat peut devenir moins cubique sans cesser d'etre structure, editable et reconnaissable comme
un monde voxel.

Des tests differentiels rejouent les memes modifications, placements, destructions, collisions,
raycasts, inventaires et sauvegardes dans les pipelines moderne et Legacy. Ils garantissent que cette
transition reste une evolution visuelle et ne provoque aucune regression de gameplay.

## Points forts

| Jouable maintenant | Techniquement solide | Pense pour evoluer |
| --- | --- | --- |
| Sandbox voxel et mode Aventures en mer avec port, navire mobile, survie, equipage et combats. | Build `CMake`, `FetchContent`, warnings stricts, tests `doctest`, smoke tests classiques et maritimes, couverture critique et CI Windows. | Architecture separee par modules, sauvegardes versionnees, migrations historiques et roadmap centree sur la mer. |

## Ce que propose deja ValCraft

- un monde voxel genere proceduralement avec seed deterministe
- un mode Aventures en mer complet avec port de depart, archipels, navire mobile et progression sauvegardee
- des deplacements FPS avec collisions, saut, nage, plongee et mode fly debug
- une interaction directe avec le terrain : casser, poser, remplacer certaines decorations
- une boucle sandbox plus complete que la simple demo terrain : hotbar, inventaire modernise, menu pause, item drops et respawn
- des creatures avec cycle jour/nuit : animaux passifs le jour, zombies agressifs la nuit
- un rendu deja lisible avec eau translucide, torches emissives, vegetation decorative et presentation FPS
- un projet code comme un vrai depot de production, pas juste un prototype jetable

## Captures

<table>
  <tr>
    <td width="50%">
      <img src="Images/img_1.png" alt="Exploration d'un environnement naturel dans ValCraft">
    </td>
    <td width="50%">
      <img src="Images/img_2.png" alt="Vue en jeu du relief et du monde voxel de ValCraft">
    </td>
  </tr>
  <tr>
    <td align="center">
      <strong>Explorer un monde ouvert procedural deja agreable a parcourir</strong>
    </td>
    <td align="center">
      <strong>Lire rapidement le relief, les volumes et la structure du terrain</strong>
    </td>
  </tr>
  <tr>
    <td width="50%">
      <img src="Images/img_3.png" alt="Menu inventaire modernise de ValCraft">
    </td>
    <td width="50%">
      <img src="Images/img_4.png" alt="Menu pause modernise de ValCraft">
    </td>
  </tr>
  <tr>
    <td align="center">
      <strong>Gerer l'inventaire dans une interface lisible, moderne et testee</strong>
    </td>
    <td align="center">
      <strong>Mettre le jeu en pause avec un menu clair, compact et conforme a l'UI actuelle</strong>
    </td>
  </tr>
  <tr>
    <td width="50%">
      <img src="Images/img_6.png" alt="Capture mettant en avant l'eau et l'ambiance de ValCraft">
    </td>
    <td width="50%">
      <img src="Images/img_7.png" alt="Capture mettant en avant la profondeur visuelle de ValCraft">
    </td>
  </tr>
  <tr>
    <td align="center">
      <strong>Eau, brume, contrastes et silhouettes donnent deja une vraie ambiance</strong>
    </td>
    <td align="center">
      <strong>Le prototype commence deja a ressembler a un vrai jeu jouable</strong>
    </td>
  </tr>
  <tr>
    <td colspan="2" align="center">
      <img src="Images/img_5.png" alt="Panorama du monde voxel de ValCraft" width="72%">
    </td>
  </tr>
  <tr>
    <td colspan="2" align="center">
      <strong>Un panorama qui montre bien la direction du projet : un sandbox voxel simple, propre et evolutif</strong>
    </td>
  </tr>
</table>

## Fonctionnalites

### Gameplay

- exploration libre en vue FPS
- collisions joueur / terrain, chute, degats de chute, nage et gestion de l'air sous l'eau
- hotbar `9` slots avec selection clavier et roulette
- inventaire jouable avec UI modernisee, drag/drop, split de stack, echanges hotbar et drop d'objets
- casse de blocs au clic gauche et pose de blocs au clic droit
- fusil a silex utilisable en FPS dans les rendus Modern Stylized et Legacy :
  tir unique, visee ADS, recul, rechargement anime et fumee de poudre noire
- commande `/give fusil` depuis la console `Â²`, avec insertion atomique dans
  la hotbar puis repli vers le stockage
- torches placables avec lumiere
- objets recoltes transformes en item drops recuperables
- ecran de mort, respawn et menu pause modernise
- cycle de creatures avec animaux le jour et zombies offensifs la nuit

### Monde et rendu

- generation procedurale deterministe
- streaming de chunks autour du joueur
- meshing avec suppression des faces cachees
- eau translucide avec surface ondulante continue
- propagation de la lumiere des torches et calcul de lumiere du ciel
- atlas de blocs et d'accents, vegetation decorative, silhouettes de creatures distinctes
- culling camera / shadow pass et presentation FPS separee du corps monde

### Pour les devs

- `C++20`, `CMake 3.24+`, `SDL2`, `OpenGL 3.3 Core`, `glad`, `glm`, `FastNoiseLite`, `doctest`
- dependances recherchees localement avant un repli `FetchContent` sur des commits verifies
- configuration hors ligne verifiee en CI avec un cache de clones Git explicite
- warnings stricts avec `-Werror`
- tests unitaires et de regression
- smoke test non interactif du jeu
- mode `--ui-preview=inventory|pause` pour valider rapidement les menus
- verification de couverture critique
- meme logique de verification en local et en CI GitHub Actions

## Etat actuel

| Deja en place | En consolidation | Pas encore prioritaire |
| --- | --- | --- |
| Sandbox solo et Aventures en mer jouables | Refonte interieure et eclairage de L'Amelie | Multijoueur |
| Port, navire mobile, survie et sauvegardes maritimes | Horizon, vie marine et decor sous-marin | Crafting profond |
| Equipage, Vieille Garde et musique reactive | Optimisations du streaming et du rendu oceanique | Progression longue type RPG |
| Pipeline de qualite local + CI | Stabilisation generale et polish | Gros systeme de quetes / narration |

## Demarrage rapide

### Prerequis

- Windows
- GCC / MinGW
- Git
- Ninja
- OpenGL `3.3 Core`
- PowerShell ou CLion

Un build complet qui doit regenerer glad exige aussi Python 3.8+ et les
versions verrouillees dans `tools/requirements-build.txt` :

```powershell
python -m pip install --requirement tools/requirements-build.txt
```

Un paquet glad systeme n'est accepte que s'il expose glad2 via `<glad/gl.h>`,
`gladLoadGL` et les symboles OpenGL 3.3. Sinon CMake affiche le diagnostic du
probe. Pour eviter Python, `VALCRAFT_GLAD_GENERATED_SOURCE_DIR` peut designer
une sortie glad2 deja generee et validee par SHA-256.

> Le script `scripts/check.ps1` sait retrouver automatiquement les outils `cmake`, `ctest`, `gcov`, `ninja`
> et MinGW depuis une installation CLion recente si besoin.

### Build

```powershell
cmake -S . -B cmake-build-relwithdebinfo -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build cmake-build-relwithdebinfo --target ValCraft --parallel
```

### Lancer le jeu

```powershell
.\cmake-build-relwithdebinfo\bin\ValCraft.exe
```

### Lancer les tests

```powershell
cmake --build cmake-build-relwithdebinfo --target valcraft_tests --parallel
ctest --test-dir cmake-build-relwithdebinfo --output-on-failure
```

### Tests Backrooms sans SDL/OpenGL

Cette cible isole la generation, l'ambiance et l'arbitrage Backrooms. Elle ne
configure ni SDL ni glad et convient aux runners sans fenetre ou pilote GPU.

```powershell
cmake -S . -B cmake-build-backrooms-core -G Ninja `
  -DVALCRAFT_HEADLESS_BACKROOMS_TESTS_ONLY=ON
cmake --build cmake-build-backrooms-core --target backrooms_core_tests --parallel
ctest --test-dir cmake-build-backrooms-core --output-on-failure
```

Pour interdire tout acces reseau, installer `glm` et `doctest` comme paquets
CMake ou placer des clones Git propres dans `<cache>/glm` et
`<cache>/doctest`. CMake verifie leur revision exacte avant de construire :

- GLM : `8d1fd52e5ab5590e2c81768ace50c72bae28f2ed`
- doctest : `1da23a3e8119ec5cce4f9388e91b065e20bf06f5`

Configurer ensuite ainsi :

```powershell
cmake -S . -B cmake-build-offline -G Ninja `
  -DVALCRAFT_ALLOW_DEPENDENCY_DOWNLOADS=OFF `
  -DVALCRAFT_DEPENDENCY_SOURCE_ROOT=C:\deps\valcraft `
  -DVALCRAFT_HEADLESS_BACKROOMS_TESTS_ONLY=ON
cmake --build cmake-build-offline --target backrooms_core_tests --parallel
ctest --test-dir cmake-build-offline --output-on-failure
```

`VALCRAFT_ALLOW_DEPENDENCY_DOWNLOADS=OFF` force
`FETCHCONTENT_FULLY_DISCONNECTED=ON`. Le job CI `backrooms-headless-offline`
desactive en plus les transports Git et les proxies reseau pendant la
configuration, la compilation et les tests.

L'exemple ci-dessus est volontairement headless : il n'a besoin que de GLM et
doctest. Pour un build complet sans paquets systeme, la racine de dependances
doit contenir des clones Git propres, sans modification ni fichier non suivi, dans
les sous-dossiers suivants :

- `glm` : `8d1fd52e5ab5590e2c81768ace50c72bae28f2ed`
- `fastnoise` : `7ccfbc16eb1c932568f177d63a9ba51d89bbe516`
- `doctest` : `1da23a3e8119ec5cce4f9388e91b065e20bf06f5`
- `sdl2` : `c98c4fbff6d8f3016a3ce6685bf8f43433c3efcc`
- `glad` : `73db193f853e2ee079bf3ca8a64aa2eaf6459043`, uniquement si glad doit etre regenere

Une source glad2 pre-generee peut remplacer le clone `glad` et le runtime
Python. Elle doit contenir `include/glad/gl.h`, `include/KHR/khrplatform.h` et
`src/gl.c`, avec respectivement les SHA-256 suivants :

- `38555534130ab0f6bdf7815fbbae52241f4f7aeb6165949fccd284348a1daa1a`
- `5ee5d5c2c6b2abd028cec8dc1a24e995bb47b075654a6fc55f3a1293de36943f`
- `60ba6709b27cde10e2b617a4a4ca6793ffb733fab18623a0e7b7513167a44fc9`

```powershell
cmake -S . -B cmake-build-full-offline -G Ninja `
  -DVALCRAFT_ALLOW_DEPENDENCY_DOWNLOADS=OFF `
  -DVALCRAFT_DEPENDENCY_SOURCE_ROOT=C:\deps\valcraft `
  -DVALCRAFT_GLAD_GENERATED_SOURCE_DIR=C:\deps\valcraft-glad-generated
```

Sous GCC ou Clang, `-DVALCRAFT_ENABLE_UNDEFINED_SANITIZER=ON` active
UBSan ainsi que `float-cast-overflow`. L'option peut etre combinee avec
`-DVALCRAFT_ENABLE_ADDRESS_SANITIZER=ON` pour les campagnes de robustesse.

### Lancer un smoke/perf run

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\perf.ps1 -Configuration RelWithDebInfo
```

### Lancer la gate stricte complete

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\check.ps1
```

Cette verification controle notamment :

- la compilation stricte
- la decouverte d'un volume minimal de tests
- l'execution complete de la suite
- des smoke runs du jeu a plusieurs moments du cycle jour/nuit
- une couverture critique minimale sur les modules coeur

## Controles

| Action | Touche |
| --- | --- |
| Avancer / reculer | `W` / `S` |
| Strafe gauche / droite | `A` / `D` |
| Saut | `Space` |
| Nager / monter | `Space` |
| Plonger / descendre | `Ctrl` |
| Basculer le fly debug | `F` |
| Ouvrir / fermer l'inventaire | `E` |
| Pause / reprendre | `Escape` |
| Casser un bloc | `Clic gauche` |
| Poser un bloc | `Clic droit` |
| Tirer avec le fusil selectionne | `Clic gauche` |
| Viser avec le fusil selectionne | `Clic droit` maintenu |
| Recharger le fusil selectionne | `R` |
| Ouvrir / fermer la console de commandes | `Â²` |
| Selection hotbar | `1` a `9` / roulette souris |
| Drop d'objet | `Q` |
| Drop de pile complete | `Ctrl + Q` |

## Stack technique

- `C++20`
- `CMake 3.24+`
- `SDL2`
- `OpenGL 3.3 Core`
- `glad`
- `glm`
- `FastNoiseLite`
- `doctest`

Les paquets systeme compatibles sont utilises en priorite. Les fallbacks
`FetchContent` ne sont actives que pour les dependances absentes et leurs
revisions Git sont verifiees avant la fin de la configuration.

### Regeneration des visuels proceduraux

Apres une modification de la recette du fusil ou des autres objets visuels,
regenerer les deux familles d'assets puis executer les controles deterministes :

```powershell
cmake --build cmake-build-relwithdebinfo --target valcraft_regenerate_model_icon_atlas
cmake --build cmake-build-relwithdebinfo --target valcraft_regenerate_block_texture_tiles
cmake --build cmake-build-relwithdebinfo --target valcraft_check_model_icon_atlas
cmake --build cmake-build-relwithdebinfo --target valcraft_check_block_texture_tiles
ctest --test-dir cmake-build-relwithdebinfo --output-on-failure -L assets
```

L'atlas de modeles alimente les icones Modern Stylized. La silhouette generee
dans l'atlas de blocs couvre le rendu Legacy ; leurs checksums versionnes
empechent qu'une regeneration partielle ou non deterministe soit acceptee. Les
cibles `valcraft_check_*` comparent les assets existants sans les reecrire.

## Architecture

```text
src/
  app/         Boucle de jeu, UI, branding, options, controles
  gameplay/    Controle joueur, collisions, interactions et drops
  player/      Geometrie et presentation du joueur
  creatures/   Spawn, logique, rendu et silhouettes des creatures
  render/      Renderer OpenGL, culling, shaders, HUD et viewmodel
  world/       Blocs, chunks, generation, eclairage, meshing, streaming

tests/
  Tests unitaires et de regression

scripts/
  Checks, smoke/perf et verification locale

.github/workflows/
  CI Windows qui execute la meme logique stricte que le local
```

## Qualite et pipeline

Le depot est structure pour limiter les regressions et garder une base saine pendant l'evolution du jeu.

- la CI Windows lance une gate stricte sur chaque `push` et `pull_request`
- le script `scripts/check.ps1` prepare des builds frais pour les checks stricts et la couverture
- les tests couvrent le gameplay, le monde, le rendu, l'UI, l'inventaire et les creatures
- les smoke runs valident que le jeu peut tourner sans interaction utilisateur
- les checks locaux et la CI reposent sur la meme logique pour eviter les surprises

## Roadmap

- finaliser la refonte jouable et visuelle de L'Amelie
- enrichir l'horizon, la vie marine, les iles, les ports et les rencontres en mer
- approfondir les routines d'equipage, la Vieille Garde, la survie et les ressources
- optimiser le meshing, le streaming, l'eau, les ombres et le rendu en haute mer
- consolider les sauvegardes et migrations de l'aventure maritime
- continuer a enrichir le sandbox voxel qui sert de fondation a l'exploration

## Contribution

Les contributions sont bienvenues, surtout sur :

- la stabilite du moteur et du pipeline
- le gameplay voxel
- le rendu et la lisibilite visuelle
- la couverture de tests
- l'ergonomie du build et de la CI

Avant toute proposition de changement :

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\check.ps1
```

## Developpement assiste par IA

Le developpement de ValCraft s'appuie aussi sur des outils d'IA pour accelerer certaines phases :
documentation, relecture technique, prototypage, structuration de plans de travail et certaines
implementations. Les choix techniques, l'integration finale, les validations et la direction globale
restent pilotes par le mainteneur du depot.

## Licence

Ce projet est distribue sous la licence [Apache-2.0](LICENSE).

Voir aussi le fichier [NOTICE](NOTICE) pour l'attribution du projet.

