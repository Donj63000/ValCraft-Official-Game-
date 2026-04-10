# Performance Audit

Ce dossier regroupe les runs d'audit locaux de `ValCraft`.

Le jeu reste normal par defaut.
Rien n'est ecrit ici tant qu'on ne lance pas explicitement un audit via `--audit` ou via les flags `--perf-*`.

## Structure

- `runs/<timestamp>-<mode>-<label>/manifest.json`
- `runs/<timestamp>-<mode>-<label>/summary.json`
- `runs/<timestamp>-<mode>-<label>/summary.txt`
- `runs/<timestamp>-<mode>-<label>/events.jsonl`
- `runs/<timestamp>-<mode>-<label>/seconds.jsonl`
- `runs/<timestamp>-<mode>-<label>/frames.jsonl`
- `runs/<timestamp>-<mode>-<label>/spikes.json`

## Lancer un audit interactif

```powershell
.\scripts\audit.ps1 -BuildDir .\cmake-build-debug -Mode measure -Label manual_block_break
```

## Lancer un smoke audit

```powershell
.\scripts\audit.ps1 -BuildDir .\cmake-build-debug -Mode measure -Label smoke -SmokeFrames 180 -NoConfigure -NoBuild
```

## Lancer la suite perf

```powershell
.\scripts\perf.ps1 -BuildDir .\cmake-build-debug -NoConfigure -NoBuild
```

## Modes

- `measure` : agregats, evenements utiles, fenetres de spikes, cout reduit.
- `forensic` : ajoute les entrees SDL brutes et la trace complete des frames.
