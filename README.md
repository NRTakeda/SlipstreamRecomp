# Slipstream Recomp

**Slipstream Recomp** runs *Mario Kart 8 Deluxe* version **4.0.1** (Nintendo Switch) as a native program on Linux and
the Steam Deck. The game's own code is recompiled on your machine, from **your own** copy of the game, into a native
program that runs on the [Savage Wind](https://github.com/Revivedx/SavageWindRecomp) runtime.

> [!IMPORTANT]
> **No game files are included.** You need your own legally obtained copy of *Mario Kart 8 Deluxe*, updated to
> version 4.0.1 and extracted from **your own** Nintendo Switch. This project does not provide game files, keys,
> firmware, dumping or decryption tools, and does not explain how to get them.

> [!NOTE]
> This is a fork of Savage Wind Recomp, which runs *The Legend of Zelda: Breath of the Wild*. It is not the official
> Savage Wind and is not supported by its maintainer: please don't report Slipstream Recomp's problems to Savage Wind.
> For *Breath of the Wild*, use [Savage Wind Recomp](https://github.com/Revivedx/SavageWindRecomp).

## Get started (Linux and Steam Deck)

1. Download `Slipstream_Recomp-<version>-x86_64.AppImage` from the [latest release](../../releases/latest).
2. Make it executable (in the file manager: Properties > Permissions > *Is executable*, or
   `chmod +x Slipstream_Recomp-*.AppImage`) and open it. On a Steam Deck, do this in Desktop Mode.
3. Choose the folder with your extracted game:

   ```
   <your folder>/
     exefs/   main, main.npdm, rtld, sdk, subsdk0   (version 4.0.1)
     romfs/   the RomFS with the 4.0.1 update applied over the base game
     aoc1/    optional: the Booster Course Pass add-on content (version.txt and contents.dat)
   ```

4. It checks the version, copies the files and builds the game code on your machine. This takes a few minutes and
   needs about 5 GB of free space while it builds. Then the game starts.

After that, opening the AppImage goes straight into the game. *Slipstream Recomp Configuration* (`--config`, or the
AppImage's menu entry) has the settings and the game files, and *Add to Steam* adds two entries to your Steam
library, so you can play from Game Mode on a Steam Deck.

Slipstream Recomp has its own data folder (`~/.local/share/slipstream-recomp`) and its own Steam entries, so a Savage
Wind set up for *Breath of the Wild* on the same machine is left alone.

More about game files, saves, local wireless play and Mii racers: **[Mario Kart 8 Deluxe](docs/mk8dx.md)**.

## What works

Tested on Linux (an Intel laptop GPU) and on a Steam Deck OLED:

| | |
|---|---|
| Boot, menus, character and kart selection | works |
| Grand Prix and races, on every course of the game | works, 60 fps on a Steam Deck |
| Music, sound effects and voices | works |
| Saves (a save from your own console can be brought in) | works |
| Booster Course Pass courses (with your add-on content) | works |
| Local wireless play between two computers or Steam Decks on the same network | works |
| Mii racers | the faces need your console's Mii model system data ([details](docs/mk8dx.md#mii-racers)) |
| Online play | not supported |
| Windows | no release yet: [build from source](docs/mk8dx.md#building-from-source) |

Known issues: [docs/mk8dx.md](docs/mk8dx.md#known-issues).

## Found a bug?

[Open an issue](../../issues) here, with your `savagewind.log` (in `~/.local/share/slipstream-recomp`) and what you
did right before the problem. Don't attach game files, keys or generated code.

## Respect copyright

- *Mario Kart 8 Deluxe* is © Nintendo. This is a fan project. It is not affiliated with, endorsed by or sponsored by
  Nintendo. *Nintendo Switch* and *Mario Kart* are trademarks of Nintendo.
- This repository contains no game files, no game code (also not the C code the recompiler generates from it), no
  assets, no keys and no firmware, and its releases don't either.
- The game code you generate and the build made from it contain Nintendo's code. They are for your own use only:
  don't share them, upload them or ship them.
- Issues or pull requests that contain game files, keys, generated code or links to pirated copies will be removed.
- Follow the copyright laws of your country. Dumping a game you own for personal use is legal in some countries and
  not in others; that is your responsibility.

## Credits and license

Slipstream Recomp is **[Savage Wind Recomp](https://github.com/Revivedx/SavageWindRecomp)** by Revivedx and its
contributors, with the changes that make it run *Mario Kart 8 Deluxe*. The runtime, the recompiler and the start
screen are theirs. Savage Wind is built on [LAS Recomp](https://github.com/peiikai/LASRecomp) by peiikai (MIT), with
[TotK Recomp](https://github.com/valentderah/totk-pc-recomp) by Valiantsin Dzerakh (MIT) and
[Ryujinx](https://github.com/Ryujinx/Ryujinx) (MIT) as references. Full list and the licenses of what releases bundle:
[Credits](docs/third-party.md).

The code is under the [MIT License](LICENSE). The license covers this project's code only: it grants nothing for
Nintendo's game, which you must supply yourself.
