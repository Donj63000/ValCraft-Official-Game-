# ValCraft

<p align="center">
  <strong>Mon defi perso: refaire un jeu dans l'esprit de Minecraft, a ma facon, en C++</strong>
  <br>
  ValCraft est un projet ne d'un challenge personnel: recreer une experience sandbox voxel, avec ma propre direction, mes propres choix de design et une implementation C++ maison.
</p>

<p align="center">
  <a href="https://github.com/Donj63000/ValCraft-Official-Game-/actions/workflows/strict.yml">
    <img src="https://github.com/Donj63000/ValCraft-Official-Game-/actions/workflows/strict.yml/badge.svg" alt="CI Strict">
  </a>
  <img src="https://img.shields.io/badge/C%2B%2B-20-00599C" alt="C++20">
  <img src="https://img.shields.io/badge/CMake-3.24%2B-064F8C" alt="CMake">
  <img src="https://img.shields.io/badge/OpenGL-3.3%20Core-5586A4" alt="OpenGL 3.3 Core">
  <img src="https://img.shields.io/badge/Platform-Windows-0078D6" alt="Platform Windows">
  <img src="https://img.shields.io/badge/Status-Active%20Development-2EA043" alt="Status Active Development">
  <img src="https://img.shields.io/badge/Development-AI%20Assisted-8A2BE2" alt="AI Assisted Development">
</p>

<p align="center">
  <a href="#apercu">Apercu</a> |
  <a href="#captures">Captures</a> |
  <a href="#pour-les-joueurs">Pour les joueurs</a> |
  <a href="#fonctionnalites">Fonctionnalites</a> |
  <a href="#demarrage-rapide">Demarrage rapide</a> |
  <a href="#controles">Controles</a> |
  <a href="#roadmap">Roadmap</a>
</p>

<p align="center">
  <img src="Images/img.png" alt="Capture principale de ValCraft" width="920">
  <br>
  <em>ValCraft propose deja une vraie boucle sandbox: explorer, modifier le terrain, poser des blocs et tester un monde voxel vivant en temps reel.</em>
</p>

## Apercu

ValCraft est ne d'un defi personnel: refaire un jeu "comme Minecraft", mais a ma facon, avec ma propre vision du sandbox voxel et avec une implementation orientee C++ plutot qu'un simple assemblage dans un moteur tout fait. L'idee est simple: entrer dans un monde genere proceduralement, se deplacer librement, observer le relief, casser des blocs, en poser d'autres et voir le terrain reagir instantanement.

Le projet est encore en developpement, mais il est deja pense pour etre agreable a parcourir autant pour un joueur curieux que pour une personne qui suit sa construction technique.

Aujourd'hui, ValCraft met surtout l'accent sur:

- l'exploration libre d'un monde voxel genere automatiquement
- les sensations de deplacement en vue FPS avec gravite, collisions et nage
- l'interaction directe avec le decor via la casse et la pose de blocs
- une presentation visuelle deja solide avec eau translucide, torches, hotbar et lecture claire du terrain
- une base de projet propre pour continuer a enrichir le jeu sans casser l'existant

> ValCraft est aujourd'hui une V1 jouable: le coeur de l'experience sandbox est deja la, meme si le contenu et la profondeur de jeu vont encore beaucoup evoluer.

## Pourquoi essayer ValCraft

| Explorer | Jouer tout de suite | Suivre un projet qui evolue |
| --- | --- | --- |
| Parcourez un monde voxel avec relief, eau, vegetation, zones ouvertes et une ambiance sandbox facile a lire tres vite. | Deplacez-vous en vue FPS, sautez, nagez, cassez des blocs, posez-en d'autres et utilisez la hotbar sans attendre un gros setup de gameplay. | Le projet avance comme un vrai jeu en construction: captures regulieres, base jouable, code teste et feuille de route claire. |

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
      <strong>Explorer un monde ouvert, lisible et immediatement jouable</strong>
    </td>
    <td align="center">
      <strong>Profiter d'un relief procedural qui donne deja une vraie sensation d'aventure</strong>
    </td>
  </tr>
  <tr>
    <td width="50%">
      <img src="Images/img_3.png" alt="Capture de gameplay avec interface et hotbar dans ValCraft">
    </td>
    <td width="50%">
      <img src="Images/img_4.png" alt="Capture montrant la construction et la lecture du terrain dans ValCraft">
    </td>
  </tr>
  <tr>
    <td align="center">
      <strong>Retrouver les bases du sandbox voxel: observer, se reperer et agir sur le decor</strong>
    </td>
    <td align="center">
      <strong>Construire et modifier le terrain avec une boucle simple, claire et immediate</strong>
    </td>
  </tr>
  <tr>
    <td width="50%">
      <img src="Images/img_6.png" alt="Capture de gameplay mettant en avant l'eau et l'ambiance de ValCraft">
    </td>
    <td width="50%">
      <img src="Images/img_7.png" alt="Capture de gameplay mettant en avant la profondeur visuelle de ValCraft">
    </td>
  </tr>
  <tr>
    <td align="center">
      <strong>Une ambiance visuelle deja posee avec eau translucide, volumes et contrastes lisibles</strong>
    </td>
    <td align="center">
      <strong>Un prototype qui commence deja a ressembler a un vrai jeu a parcourir</strong>
    </td>
  </tr>
  <tr>
    <td colspan="2" align="center">
      <img src="Images/img_5.png" alt="Panorama du monde voxel de ValCraft" width="72%">
    </td>
  </tr>
  <tr>
    <td colspan="2" align="center">
      <strong>Un panorama qui montre bien la direction du projet: un sandbox voxel simple, propre et evolutif</strong>
    </td>
  </tr>
</table>

## Pour les joueurs

Si vous regardez ValCraft avant tout comme un jeu, voici ce qu'il propose deja:

- explorer un monde voxel genere proceduralement
- se deplacer en vue FPS avec collisions, saut et nage
- casser des blocs et reconstruire le decor en temps reel
- utiliser une hotbar simple pour rester dans une boucle de jeu immediate
- profiter d'une V1 jouable qui se concentre sur les sensations de base du sandbox

Ce que ValCraft ne cherche pas encore a faire a ce stade:

- raconter une campagne ou une progression longue
- proposer du multijoueur ou un gros contenu RPG
- remplacer un jeu complet fini du genre

Le projet est plutot dans une logique de prototype jouable solide: une bonne base de jeu aujourd'hui, plus de profondeur demain.

## Fonctionnalites

### Gameplay

- deplacement `WASD`
- vue souris en premiere personne
- saut avec gravite
- nage, wading et plongee dans l'eau (`Space` / `Ctrl`)
- collisions joueur contre blocs solides
- mode fly debug
- casse de blocs au clic gauche
- pose de blocs au clic droit
- hotbar `9` slots avec selection a la roulette
- prevention de pose dans le volume du joueur

### Monde

- chunks streames autour du joueur
- seed deterministe
- generation de relief et de surface par bruit
- palette V1: `Air`, `Grass`, `Dirt`, `Stone`, `Sand`, `Wood`, `Leaves`, `Water`, `Torch`
- maillage de chunks avec suppression des faces cachees
- eau translucide animee avec surface ondulante continue entre blocs

### Pour les devs

- build `CMake` en `C++20`
- dependances gerees via `FetchContent`
- warnings stricts avec `-Werror`
- suite de tests automatises
- smoke test non interactif
- verification de couverture critique
- gate locale et CI executees via le meme script

## Demarrage rapide

### Prerequis

- Windows
- GCC / MinGW
- Ninja
- OpenGL `3.3 Core`
- CLion ou terminal PowerShell

### Build

```powershell
cmake -S . -B cmake-build-release -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build cmake-build-release --target ValCraft --parallel
```

### Lancer le jeu

```powershell
.\cmake-build-release\bin\ValCraft.exe
```

### Lancer les tests

```powershell
cmake --build cmake-build-release --target valcraft_tests --parallel
ctest --test-dir cmake-build-release --output-on-failure
```

### Lancer un profilage smoke/perf

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\perf.ps1 -Configuration RelWithDebInfo
```

### Lancer la gate stricte complete

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\check.ps1
```

Cette verification controle:

- la compilation stricte
- la presence d'au moins `20` tests
- l'execution complete de la suite de tests
- un smoke test du jeu
- une couverture critique minimale sur les modules coeur

## Controles

| Action | Touche |
| --- | --- |
| Avancer / reculer | `W` / `S` |
| Strafe gauche / droite | `A` / `D` |
| Saut | `Space` |
| Nager / plonger dans l'eau | `Space` / `Ctrl` |
| Monter / descendre en fly | `Space` / `Ctrl` |
| Basculer le mode fly | `F` |
| Liberer / reprendre la souris | `Escape` |
| Casser un bloc | `Clic gauche` |
| Poser un bloc | `Clic droit` |
| Selection du bloc actif | `1` a `9` / `roulette souris` |

## Stack technique

- `C++20`
- `CMake 3.24+`
- `SDL2`
- `OpenGL 3.3 Core`
- `glad`
- `glm`
- `FastNoiseLite`
- `doctest`

Toutes les dependances sont recuperees automatiquement via `FetchContent`.

## Architecture

```text
src/
  app/         Boucle de jeu, initialisation SDL/OpenGL
  gameplay/    Controle joueur, collisions, interactions monde
  render/      Shaders, atlas, meshes GPU, rendu OpenGL
  world/       Blocs, chunks, generation, raycast, meshing

tests/
  Tests unitaires et de regression

scripts/
  Gate stricte locale

.github/workflows/
  CI Windows qui execute la meme gate que le local
```

## Developpement assiste par IA

Le developpement de ValCraft s'appuie aussi sur des outils d'IA pour accelerer certaines phases du projet, notamment:

- la structuration de plans de travail
- l'assistance au prototypage et a certaines implementations
- la relecture technique et l'amelioration de la documentation

Les choix techniques, l'integration dans le projet, les validations et la direction globale restent pilotes par le mainteneur du depot.

## Etat du projet

ValCraft avance avec une priorite simple: d'abord consolider une boucle de jeu sandbox agreable, ensuite enrichir le contenu.

| Dans le perimetre actuel | Pas encore dans le perimetre |
| --- | --- |
| Monde procedural jouable | Multijoueur |
| Exploration FPS | Crafting |
| Modification du terrain en temps reel | Inventaire complet |
| Base moteur testee | Sauvegarde persistante complete |
| Pipeline strict anti-regression | Mobs / IA |
| CI GitHub reproductible | Eclairage dynamique avance |

## Roadmap

- sauvegarde et chargement des modifications monde
- inventaire complet et HUD etendu
- optimisation du meshing
- frustum culling plus fin
- generation plus riche
- systeme d'inventaire
- sandbox plus profond et interactif

## Contribution

Les contributions sont bienvenues, surtout sur:

- la stabilite du moteur
- le gameplay voxel
- la qualite de rendu
- la couverture de tests
- l'ergonomie du pipeline de build

Avant toute proposition de changement:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\check.ps1
```

## Licence

Ce projet est distribue sous la licence [Apache-2.0](LICENSE).

Voir aussi le fichier [NOTICE](NOTICE) pour l'attribution du projet.
