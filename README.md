# _vita2hos_

A PlayStation Vita to Horizon OS (Nintendo Switch OS) translation layer (**_not_** an emulator)

## How does it work?

PlayStation Vita (ARMv7 CPU) executables can be run natively on Nintendo Switch ARMv8 CPU in 32-bit execution mode.

When loading a PlayStation Vita executable, _vita2hos_ redirects the [module](https://wiki.henkaku.xyz/vita/Modules) imports of said executable to jump to routines that implement the same behavior, by using native Horizon OS services, like the one exposed by the original PlayStation Vita OS modules.

## How can I use it?

### Running it on a real console

1. Copy `vita2hos.nsp` to your microSD card (i.e. to: `atmosphere/vita2hos.nsp`)
2. Create [`atmosphere/config/override_config.ini`](https://github.com/Atmosphere-NX/Atmosphere/blob/master/config_templates/override_config.ini) and add the following lines to it:

    ```ini
    [hbl_config]
    override_any_app=true
    override_any_app_key=R
    override_any_app_address_space=32_bit
    ; adjust the path according to the location of your file
    path=atmosphere/vita2hos.nsp
    ```

    - Note: As long as this file exists you won't be able to use the homebrew menu and instead will always run _vita2hos_.

      A quick workaround would be to rename the file and restart your Switch.
      Unfortunately `override_config.ini` doesn't allow multiple `path` entries which is why it has to be done this way.

3. Copy a PlayStation Vita executable (`.velf` or `.self`/`eboot.bin`) to `sd:/vita2hos/test.elf`
4. Boot (or reboot) your Switch and start any game while holding down `R`
    - Attempting to use _vita2hos_ via applet mode (album button) will currently result in a fatal error and wouldn't be recommended anyway.
5. Enjoy!

### Running it on yuzu

1. Copy a PlayStation Vita executable (`.velf` or `.self`/`eboot.bin`) to `sd:/vita2hos/test.elf` (_File_ → _Open yuzu Folder_ → `sdmc/`)
2. Disable Macro JIT (_Emulation_ → _Configure_ → _General_ → _Debug_ → _Disable Macro JIT_)
3. Run `vita2hos.nsp`
4. Enjoy!

#### Notes:

Make sure to use a very recent build of [yuzu](https://yuzu-emu.org/downloads/), such as _Mainline_ or _Early Access Builds_.

### Running it on Ryujinx

1. Copy a PlayStation Vita executable (`.velf` or `.self`/`eboot.bin`) to `sd:/vita2hos/test.elf` (_File_ → _Open Ryujinx Folder_ → `sdcard/`)
2. Disable PPTC (_Options_ → _Settings_ → _System_ → Unselect _Enable PPTC (Profiled Persistent Translation Cache)_)
3. Run `vita2hos.nsp`
4. Enjoy!

#### Notes:

- Ryujinx >= 1.1.133 is required
- Some ARM Thumb instructions are not yet implemented on Ryujinx

## Project status, compatibility and supported features

This is still in very early stages and therefore it can only run very simple CPU-rendered PlayStation Vita homebrews.

There is very initial 3D graphics support (it can run vitasdk's GXM triangle and cube samples by hardcoding _vita2hos_'s GLSL shaders to match the Cg shaders the samples use).

## Special Thanks

A few noteworthy teams/projects who've helped along the way are:

* **[Vita3K](https://vita3k.org/)**

    _vita2hos_ uses Vita3K's shader recompiler, and some parts of _vita2hos_'s code are based on Vita3K's implementation. Please, consider [**donating**](https://vita3k.org/#donate) and [**contributing**](https://vita3k.org/#contribute) to Vita3K!
* **[UAM - deko3d shader compiler](https://github.com/devkitPro/uam)**

     _vita2hos_ uses UAM ([deko3d](https://github.com/devkitPro/deko3d)'s shader compiler) to compile shaders. Please, also consider contributing to that project and donating to the developers!
* **[Ryujinx](https://ryujinx.org/)**
* **[yuzu](https://yuzu-emu.org/)**
* **[Atmosphère](https://github.com/Atmosphere-NX/Atmosphere)**
* **[Switchbrew](https://github.com/switchbrew/)**

Also special thanks to @PixelyIon and @SciresM for their help, and to all the testers, especially @TSRBerry.

## Disclaimer

* **Nintendo Switch** is a trademark of **Nintendo Co., Ltd**
* **PlayStation Vita** is a trademark of **Sony Interactive Entertainment**
