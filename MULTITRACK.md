# Sound2LightMultiTrack

Version multipiste de [Sound2Light](https://github.com/ETCLabs/Sound2Light) (ETC Labs, licence MIT).
L'original moyenne toutes les entrées en un seul signal mono. Ici, **chaque canal de la carte son a sa propre chaîne d'analyse** : spectre, 6 déclencheurs (Bass, LoMid, HiMid, High, Level, Silence) et détection de BPM, jusqu'à **16 pistes**.

## Ce qui change

| | Original | MultiTrack |
|---|---|---|
| Entrée audio | stéréo, mixée en mono | jusqu'à 64 canaux (CoreAudio, WASAPI, ASIO) |
| Analyse | 1 chaîne | 1 chaîne par piste, 1 à 16 pistes |
| Fréquence d'échantillonnage | 44,1 kHz uniquement | tout taux accepté, rééchantillonné en 44,1 kHz (ex. Dante en 48 kHz) |
| Sortie OSC | messages configurés à la main | messages configurés **par piste** + flux automatique `/s2l/chN/…` |
| Presets | 1 jeu de réglages | tous les réglages de toutes les pistes ; les anciens presets `.s2l` se chargent dans la piste 1 |

## L'interface

- **Barre des pistes** en haut : une tuile par piste avec son nom, son entrée, un vumètre d'entrée et les 6 bandes.
- **Clic** sur une tuile : la piste devient la piste éditée (spectre, réglages, BPM en dessous).
- **Double-clic** : renommer la piste (le nom d'un artiste, par exemple).
- **Menu « Entrée »** : choisir le canal écouté par la piste, ou « Mix » pour retrouver le comportement d'origine.
- **Ctrl+1 … Ctrl+9** : sélectionner les pistes 1 à 9 au clavier.
- **Signal de test (8 canaux)** dans les réglages d'entrée : 8 tonalités rythmées, une par canal, pour tout régler sans carte son.

## Flux OSC multipiste (pour Chataigne, MadMapper, TouchDesigner…)

Activé par la case « OSC multipiste » (et « OSC Output » doit être allumé). `N` = numéro de piste, de 1 à 16.

| Adresse | Type | Contenu |
|---|---|---|
| `/s2l/chN/bass` | float 0–1 | niveau de la bande Bass |
| `/s2l/chN/lo_mid` | float 0–1 | niveau LoMid |
| `/s2l/chN/hi_mid` | float 0–1 | niveau HiMid |
| `/s2l/chN/high` | float 0–1 | niveau High |
| `/s2l/chN/level` | float 0–1 | niveau global |
| `/s2l/chN/silence` | float 0–1 | inverse du niveau (1 = silence) |
| `/s2l/chN/<bande>/trig` | int 0/1 | état du déclencheur, après On/Off Delay et Decay |
| `/s2l/chN/bpm` | float | BPM détecté ou tapé |

Les niveaux sont envoyés jusqu'à 44 fois par seconde, uniquement quand ils changent. Une bande mutée n'envoie rien.

Les messages OSC « classiques » (On / Off / Level par bande, presets Eos, Cobalt, Hog…) restent configurables **par piste**, comme dans l'original.

## Commandes OSC entrantes

Toutes celles de l'original, plus :

| Adresse | Argument | Effet |
|---|---|---|
| `/s2l/track` | int | sélectionne la piste affichée (1–16) |
| `/s2l/track_count` | int | nombre de pistes actives |
| `/s2l/chN/<bande>/mute` | – | mute / démute une bande d'une piste |
| `/s2l/preset/save` | string | enregistre le preset sous ce nom |
| `/s2l/preset` | string | charge un preset (comme l'original) |

## Dante

- **macOS** : Dante Virtual Soundcard apparaît comme une carte CoreAudio multicanal. Choisis-la dans Settings, puis assigne une entrée à chaque piste.
- **Windows** : DVS en mode **ASIO** (recommandé) ou WDM. Les deux apparaissent dans la liste, préfixés `[ASIO]` ou `[WASAPI]`.
- 48 kHz est rééchantillonné automatiquement en 44,1 kHz pour l'analyse.

## Installer

- **macOS** : prendre `Sound2LightMultiTrack-macOS-arm64` pour un Mac Apple Silicon (M1, M2…), `Sound2LightMultiTrack-macOS-intel` pour un Mac Intel (menu Pomme > À propos de ce Mac : « Puce Apple » ou « Processeur Intel »). Ouvrir le `.dmg`, glisser l'app dans Applications. L'app n'est pas signée par Apple : au premier lancement, clic droit > Ouvrir, ou Réglages Système > Confidentialité et sécurité > « Ouvrir quand même ». Autoriser l'accès au micro, sinon l'entrée reste muette.
- **Windows** : dézipper, lancer `Sound2LightMultiTrack.exe`.

Les réglages sont séparés de ceux du Sound2Light d'origine : les deux peuvent cohabiter.

## Compiler

Qt 5.15 (Qt Quick Controls 1), aucune autre dépendance : RtAudio est inclus dans `src/rtaudio`.

```
cd src
qmake S2L.pro CONFIG+=release          # Windows avec ASIO : CONFIG+=asio
make                                   # Windows : nmake
```

Les builds macOS et Windows sont produits automatiquement par GitHub Actions (`.github/workflows/build.yml`) ; un tag `v*` publie une release.

## Licences

- Sound2Light : MIT, © 2016 Electronic Theatre Controls, Inc.
- Extension multipiste : MIT.
- RtAudio : licence de type MIT, © Gary P. Scavone.
- Build Windows avec ASIO : contient des fichiers du SDK ASIO de Steinberg (licence Steinberg ou GPLv3). Pour un usage interne, aucune contrainte ; pour redistribuer publiquement, compiler sans `CONFIG+=asio` ou respecter la GPLv3.
