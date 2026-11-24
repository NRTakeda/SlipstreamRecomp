# Mario Kart 8 Deluxe 4.0.1

[Back to the README](../README.md)

**Slipstream Recomp**, this fork of Savage Wind, runs *Mario Kart 8 Deluxe* version **4.0.1** on the Savage Wind
runtime, the same way Savage Wind runs *Breath of the Wild*: the game's own code is recompiled into a native program
**on your machine, from your own copy of the game**. Nothing of the game is included here.

> **No game files are included.** You need your own legally obtained copy of *Mario Kart 8 Deluxe*, updated to
> **version 4.0.1** and dumped from **your own** Nintendo Switch. This project does not provide game files, keys,
> firmware, dumping or decryption tools, and does not explain how to get them. Issues or pull requests that share,
> link to or ask for any of these are removed.

## What works

Tested on Linux (an Intel laptop GPU) and on a Steam Deck OLED (SteamOS, in Desktop Mode and from a Steam shortcut in
Game Mode):

| | |
|---|---|
| Boot, title screen, menus, character and kart selection | works |
| Grand Prix and races in local wireless play | works, 60 fps on a Steam Deck |
| Music, sound effects and voices | works |
| Saves (progress kept between starts; a save from your own console can be brought in) | works |
| Local wireless play between two computers or Steam Decks running this fork, on the same network | works |
| Mii racers | the faces need your console's Mii model system data (see [Mii racers](#mii-racers)) |
| Online play | not supported |

Known issues are in [Known issues](#known-issues).

## Getting started (Linux and Steam Deck)

1. Download `Slipstream_Recomp-<version>-x86_64.AppImage` from the [Releases](../../../releases) page.
2. Make it executable (in the file manager: Properties > Permissions > *Is executable*, or
   `chmod +x Slipstream_Recomp-*.AppImage`) and open it. On a Steam Deck, do this in Desktop Mode.
3. The start screen asks for your game files: choose the folder that holds your extracted copy of the game
   (see [Game files](#game-files)).
4. It checks that the files are version 4.0.1, copies them into its data folder (`~/.local/share/slipstream-recomp`;
   hard links when they're on the same drive, so no second 12 GB) and builds the game code on your machine. That
   takes a few minutes and about 5 GB of free space while it builds. Then the game starts.
5. From then on, opening the AppImage goes straight into the game. *Slipstream Recomp Configuration* (the AppImage's
   menu entry, or `--config`) has the settings, the game files and *Add to Steam*: on a Steam Deck that gives you two
   entries in the Steam library, so you can play from Game Mode.

Slipstream Recomp keeps its own data folder and its own Steam entries, so it doesn't touch a Savage Wind set up for
*Breath of the Wild* on the same machine.

## Game files

Your own *Mario Kart 8 Deluxe*, updated to 4.0.1, extracted as files:

```
<your folder>/
  exefs/   main, main.npdm, rtld, sdk, subsdk0   (version 4.0.1)
  romfs/   the complete RomFS: the base game with the 4.0.1 update applied over it
  aoc1/    optional: your Booster Course Pass add-on content's RomFS (version.txt and contents.dat)
```

- The folder names don't have to be these: the setup finds the ExeFS (`main` and `main.npdm`), the RomFS (the folder
  with `Course/` and `RaceCommon/`) and the add-on content (`version.txt` and `contents.dat`) a few levels below the
  folder you choose.
- The `main` module's build id must start with `158c0c7399aabd7b` (4.0.1); `python tools/sw_probe.py` prints it.
- The RomFS must be the one with the update applied: 6,597 files. The base game's RomFS alone, or the update's alone,
  is refused with what was found.
- The Booster Course Pass courses come with the 4.0.1 update; the add-on content is what unlocks them, as on the
  console. It can also be set up later: choose its folder alone in *Set up game files*.

## Building from source

For Windows (no release yet) or to change the code: build it as a source build of Savage Wind
([Linux](linux.md), [Windows](BUILD-windows.md)), with your files in `extracted/` next to the repository's
`Makefile`:

```
python3 -m venv tools/venv && tools/venv/bin/pip install lz4 capstone unicorn
make gen        # recompiles your copy's code into generated/ (stays on your machine)
make -j8        # build/savagewind and build/swgame.so
build/savagewind --play extracted
```

`make appimage` builds the AppImage itself (in podman containers; see [Linux](linux.md)). It holds no game code.

`build/swgame.so` and `generated/` are made from your copy of the game and contain Nintendo's code: they are for
your own use only. Don't share, upload or attach them anywhere.

To run a source build from another folder, keep these next to `savagewind`: `swgame.so`, `swgame.so.id`, and the
two fonts `third_party/fonts/FiraSans/FiraSans-SemiBold.ttf` and `FiraSans-Medium.ttf` (without them the menus fall
back on a tiny built-in font). The data folder (saves, settings, shader caches) is chosen with
`SAVAGEWIND_DATA_ROOT` (see [Data folder](data-folder.md)).

## Saves

The game keeps two saves, as on the console:

- `save/save_0/` - the user's save: `userdata.dat` (progress, unlocks, settings) and `ghostlist.dat`
- `save/save_3/` - the console's save: the replays (`rp*.dat`)

To continue with a save from your own console, put its `userdata.dat` in `save_0` and its replays in `save_3`
(keep a copy first).

## Local wireless play

Two machines on the same network, each with this fork and the **same** game files, the same add-on content and the
same RomFS mods (a room where the items, bots or tracks differ goes out of sync). On each one, before starting:

```
SAVAGEWIND_LDN_IPV4=<this machine's address>  SAVAGEWIND_LDN_PEER=<the other machine's address>
```

Then *Wireless Play* in the game: one creates a room, the other joins it. Rooms are found on UDP port 24874
(`SAVAGEWIND_LDN_PORT` changes it) and the game's own traffic uses UDP port 12345. Leaving a room or the other side
quitting brings the other player back to the menu within a few seconds.

The name shown over each racer is the account's nickname: `player_name = <name>` in `savagewind.ini` (32 bytes of
UTF-8 at most; "Player" when not set).

## Mii racers

The Mii models are the console's system data (0100000000000802), not the game's. Without them Savage Wind writes empty
stand-ins and the Mii racers have no head. If you have that system data's RomFS from your own console, its six files
(`NXTextureLowLinear.dat`, `NXTextureLowSRGB.dat`, `NXTextureMidLinear.dat`, `NXTextureMidSRGB.dat`,
`ShapeHigh.dat`, `ShapeMid.dat`) go in `cache/mii_model/` in the data folder, and are used as they are.

## Known issues

- A few distant objects under water can show horizontal streaks.
- The first time each track loads, its shaders are compiled: expect a short hitch then, none the next time
  (the pipeline cache is kept in `cache/`).

## Respect copyright

- *Mario Kart 8 Deluxe* is © Nintendo. This is a fan project. It is not affiliated with, endorsed by or sponsored by
  Nintendo. *Nintendo Switch* and *Mario Kart* are trademarks of Nintendo.
- This repository contains no game code, no game assets, no keys and no firmware, and its builds won't either.
- The game code you generate and the build made from it contain Nintendo's code. They are for your own use only.
- Follow the copyright laws of your country. Dumping a game you own for personal use is legal in some countries and
  not in others.
