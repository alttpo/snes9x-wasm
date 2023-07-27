# REX

See [rex.md](rex.md) for documentation on REX subsystems.

# MacOS build
This fork enables a working MacOS build of snes9x from the GTK port using cmake instead of XCode.

## Changes:
Some CMakeLists.txt changes were required to make optional the alleged X11 requirements of the GTK port. Most of the code dealing specifically with X11/Wayland is conditionally compiled away but some parts were missed so I fixed that up.

The native GTK display driver did not properly update the main drawing area and was coded incorrectly per the GTK documentation. The visible effects were that new emulation frames would only appear while interacting with the main menu when it emitted a draw signal to refresh the window contents. The GTK display driver was attempting to update the screen outside the handling of a draw signal, which is incorrect per the documentation and so nothing would update on the screen. I changed it so that when a frame needs to be presented, it queues up a draw signal to be emitted and then draws onto the screen during the handling of the draw signal.

## Drivers:
The GTK OpenGL display driver is only coded for X11 and Wayland and has no MacOS support. The native GTK display driver is plenty fast enough so you don't miss anything speed-wise with hardware-accelerated display drivers. When turbo mode is engaged, I see ~2475 fps at 1x scale on a 14" MacBook Pro M1 2021.

## Build Process:
```
brew install qt@5 gtkmm3 sdl2
cd gtk
mkdir build
cd build
cmake .. -DUSE_WAYLAND=OFF -DUSE_X11=OFF -DUSE_XV=OFF -DUSE_PULSEAUDIO=OFF -DUSE_ALSA=OFF -DUSE_SLANG=OFF
make -j6
```

You should have a `./snes9x-gtk` executable in your `gtk/build/` folder now.

# Snes9x
*Snes9x - Portable Super Nintendo Entertainment System (TM) emulator*

This is the official source code repository for the Snes9x project.

Please check the [Wiki](https://github.com/snes9xgit/snes9x/wiki) for additional information.

## Nightly builds

Download nightly builds from continuous integration:

### snes9x

| OS            | status                                           |
|---------------|--------------------------------------------------|
| Windows       | [![Status][s9x-win-all]][appveyor]               |
| Linux (GTK)   | [![Status][snes9x_linux-gtk-amd64]][cirrus-ci]   |
| Linux (X11)   | [![Status][snes9x_linux-x11-amd64]][cirrus-ci]   |
| FreeBSD (X11) | [![Status][snes9x_freebsd-x11-amd64]][cirrus-ci] |
| macOS         | [![Status][snes9x_macOS-amd64]][cirrus-ci]       |

[appveyor]: https://ci.appveyor.com/project/snes9x/snes9x
[cirrus-ci]: http://cirrus-ci.com/github/snes9xgit/snes9x

[s9x-win-all]: https://ci.appveyor.com/api/projects/status/github/snes9xgit/snes9x?branch=master&svg=true
[snes9x_linux-gtk-amd64]: https://api.cirrus-ci.com/github/snes9xgit/snes9x.svg?task=snes9x_linux-gtk-amd64
[snes9x_linux-x11-amd64]: https://api.cirrus-ci.com/github/snes9xgit/snes9x.svg?task=snes9x_linux-x11-amd64
[snes9x_freebsd-x11-amd64]: https://api.cirrus-ci.com/github/snes9xgit/snes9x.svg?task=snes9x_freebsd-x11-amd64
[snes9x_macOS-amd64]: https://api.cirrus-ci.com/github/snes9xgit/snes9x.svg?task=snes9x_macOS-amd64

### libretro core

| OS                  | status                                                  |
|---------------------|---------------------------------------------------------|
| Linux/amd64         | [![Status][libretro_linux-amd64]][cirrus-ci]            |
| Linux/i386          | [![Status][libretro_linux-i386]][cirrus-ci]             |
| Linux/armhf         | [![Status][libretro_linux-armhf]][cirrus-ci]            |
| Linux/armv7-neon-hf | [![Status][libretro_linux-armv7-neon-hf]][cirrus-ci]    |
| Linux/arm64         | [![Status][libretro_linux-arm64]][cirrus-ci]            |
| Android/arm         | [![Status][libretro_android-arm]][cirrus-ci]            |
| Android/arm64       | [![Status][libretro_android-arm64]][cirrus-ci]          |
| Emscripten          | [![Status][libretro_emscripten]][cirrus-ci]             |
| macOS/amd64         | [![Status][libretro_macOS-amd64]][cirrus-ci]            |
| Nintendo Wii        | [![Status][libretro_nintendo-wii]][cirrus-ci]           |
| Nintendo Switch     | [![Status][libretro_nintendo-switch-libnx]][cirrus-ci]  |
| Nintendo GameCube   | [![Status][libretro_nintendo-ngc]][cirrus-ci]           |
| PSP                 | [![Status][libretro_playstation-psp]][cirrus-ci]        |

[libretro_linux-amd64]: https://api.cirrus-ci.com/github/snes9xgit/snes9x.svg?task=libretro_linux-amd64
[libretro_linux-i386]: https://api.cirrus-ci.com/github/snes9xgit/snes9x.svg?task=libretro_linux-i386
[libretro_linux-armhf]: https://api.cirrus-ci.com/github/snes9xgit/snes9x.svg?task=libretro_linux-armhf
[libretro_linux-armv7-neon-hf]: https://api.cirrus-ci.com/github/snes9xgit/snes9x.svg?task=libretro_linux-armv7-neon-hf
[libretro_linux-arm64]: https://api.cirrus-ci.com/github/snes9xgit/snes9x.svg?task=libretro_linux-arm64
[libretro_android-arm]: https://api.cirrus-ci.com/github/snes9xgit/snes9x.svg?task=libretro_android-arm
[libretro_android-arm64]: https://api.cirrus-ci.com/github/snes9xgit/snes9x.svg?task=libretro_android-arm64
[libretro_emscripten]: https://api.cirrus-ci.com/github/snes9xgit/snes9x.svg?task=libretro_emscripten
[libretro_macOS-amd64]: https://api.cirrus-ci.com/github/snes9xgit/snes9x.svg?task=libretro_macOS-amd64
[libretro_nintendo-wii]: https://api.cirrus-ci.com/github/snes9xgit/snes9x.svg?task=libretro_nintendo-wii
[libretro_nintendo-switch-libnx]: https://api.cirrus-ci.com/github/snes9xgit/snes9x.svg?task=libretro_nintendo-switch-libnx
[libretro_nintendo-ngc]: https://api.cirrus-ci.com/github/snes9xgit/snes9x.svg?task=libretro_nintendo-ngc
[libretro_playstation-psp]: https://api.cirrus-ci.com/github/snes9xgit/snes9x.svg?task=libretro_playstation-psp
