# vita2hos
A PlayStation Vita to Horizon OS (Nintendo Switch OS) translation layer (**not** an emulator)

### How does it work?

PlayStation Vita (ARMv7 CPU) executables can be run natively on Nintendo Switch ARMv8 CPU in 32-bit execution mode.
When loading a PlayStation Vita executable, _vita2hos_ redirects the [module](https://wiki.henkaku.xyz/vita/Modules) imports of said executable to jump to routines that implement the same behavior, by using native Horizon OS services, like the one exposed by the original PlayStation Vita OS modules.

### Special Thanks
A few noteworthy teams/projects who've helped along the way are:
* **[Vita3K](https://vita3k.org/)**
* **[Ryujinx](https://ryujinx.org/)**
* **[yuzu](https://yuzu-emu.org/)**
* **[Atmosphère](https://github.com/Atmosphere-NX/Atmosphere)**
* **[Switchbrew](https://github.com/switchbrew/)**

Also special thanks to @PixelyIon and @SciresM for their help, and to all the testers, especially @TSRBerry.

### Disclaimer
* **Nintendo Switch** is a trademark of **Nintendo Co., Ltd**
* **PlayStation Vita** is a trademark of **Sony Interactive Entertainment**
