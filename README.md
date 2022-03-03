# vita2hos
A PlayStation Vita to Horizon OS (Nintendo Switch OS) translation layer (**not** an emulator)

### How does it work?

PlayStation Vita (ARMv7 CPU) executables can be run natively on Nintendo Switch ARMv8 CPU in 32-bit execution mode.
When loading a PlayStation Vita executable, _vita2hos_ redirects the [module](https://wiki.henkaku.xyz/vita/Modules) imports of said executable to jump to routines that implement the same behavior, by using native Horizon OS services, like the one exposed by the original PlayStation Vita OS modules.

### How can I use it?

1. Copy `vita2hos.nsp` to your microSD card (i.e. to: `atmosphere/vita2hos.nsp`)
2. Create `atmosphere/config/override_config.ini` and add the following lines to it:

    ```ini
    [hbl_config]
    override_any_app=true
    override_any_app_key=R
    override_any_app_address_space=32_bit
    ; adjust the path according to the location of your file
    path=atmosphere/vita2hos.nsp

    ```

    - Note: As long as this file exists you won't be able to use the homebrew menu and instead will always run vita2hos.

      A quick workaround would be to rename the file and restart your Switch.

3. Copy a PSVita `.velf` to the root of your microSD card and rename it to `test.elf`
4. Boot (or reboot) your Switch and start any game while holding down `R`
    - Attempting to use `vita2hos` via applet mode (album button) will currently result in a fatal error and wouldn't be recommended anyway.
5. Enjoy!

### Special Thanks
A few noteworthy teams/projects who've helped along the way are:
* **[Vita3K](https://vita3k.org/)**
* **[Ryujinx](https://ryujinx.org/)**
* **[yuzu](https://yuzu-emu.org/)**
* **[Atmosphère](https://github.com/Atmosphere-NX/Atmosphere)**
* **[Switchbrew](https://github.com/switchbrew/)**

Also special thanks to @PixelyIon and @SciresM for their help, and to all the testers, especially @jeliebig.

### Disclaimer
* **Nintendo Switch** is a trademark of **Nintendo Co., Ltd**
* **PlayStation Vita** is a trademark of **Sony Interactive Entertainment**
