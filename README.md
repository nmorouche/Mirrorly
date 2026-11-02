<p align="center">
  <img src="res/mirrorly.png" width="96" alt="Mirrorly icon">
</p>

<h1 align="center">Mirrorly</h1>

<p align="center">
  An AirPlay receiver for Windows. Mirror an iPhone or iPad screen to your PC, with sound.
</p>

---

## Features

- **Screen mirroring** from iPhone and iPad, up to 4K and 60 fps
- **Audio**, both while mirroring and for music-only AirPlay (ALAC, AAC, AAC-ELD)
- **Now Playing** view with artwork, title, artist and progress when streaming music
- **Portable**: a single `Mirrorly.exe` (about 4.5 MB) with no installer, runtime, Bonjour or iTunes needed
- **Themes**, picked at the bottom of Settings (the window and taskbar icon change with them):
  - **Default**: clean and quiet, follows Windows light/dark mode, frosted controls over the video
  - **Acid poster**: concrete-grey paper, giant acid-lime type, a cobalt block with pixel lettering, a halftone object and a live waveform
- **Volume** follows the iPhone's volume buttons, with a mute button on the PC

Tested with an iPhone on iOS 27.2 and Windows 11.

## Getting started

1. Download `Mirrorly.exe` from the [latest release](../../releases/latest) and run it.
2. When Windows Defender Firewall asks, click **Allow**. Tick both *Private* and *Public* networks.
3. Connect the iPhone and the PC to the **same network**.
4. On the iPhone, open **Control Center → Screen Mirroring** and choose your PC.

To stream music only, pick the PC from the AirPlay menu in Control Center or in any app.

## Controls

| Action | Shortcut |
| --- | --- |
| Toggle full screen | Double-click, <kbd>F11</kbd> or <kbd>Alt</kbd>+<kbd>Enter</kbd> |
| Leave full screen | <kbd>Esc</kbd> |
| Mute / unmute | <kbd>M</kbd> |
| Disconnect | ✕ button in the overlay |

Move the mouse while mirroring to show the controls.

## Settings

Click the gear icon:

| Setting | Description |
| --- | --- |
| Name | How the PC appears on your devices |
| Resolution | 720p, 1080p, 1440p or 4K: the highest quality the iPhone will send |
| Frame rate | 30 or 60 fps |
| Full screen on start | Go full screen as soon as mirroring begins |
| Play audio | Play the iPhone's sound on the PC |
| Theme | Default or Acid poster; more can be added (see below) |
| Windows Firewall | Lets Mirrorly through the firewall on all network types (asks for admin approval) |

Settings are saved in `%APPDATA%\Mirrorly\settings.ini`.

## Troubleshooting

| Problem | Fix |
| --- | --- |
| The PC doesn't appear on the iPhone | Use **Settings → Windows Firewall → Allow**, and check both devices are on the same network. Guest Wi-Fi, client isolation and VPNs block AirPlay. |
| "Network discovery is blocked" | Another program has UDP port 5353 locked. Close other AirPlay receivers and restart Mirrorly. |
| Two receivers show up | Close any other AirPlay receiver app running on the PC. |
| Picture but no sound | Check **Settings → Play audio**, the mute button, the iPhone volume and the Windows output device. |

Mirrorly writes a `Mirrorly.log` next to the exe (or in `%APPDATA%\Mirrorly`). Check it first when something goes wrong.

## How it works

```
iPhone ──mDNS──▶ discovery        (_airplay._tcp, _raop._tcp)
       ──RTSP──▶ pairing          (Ed25519 / X25519)
                 FairPlay setup   (session key exchange)
       ──TCP───▶ screen stream    (H.264, AES-128-CTR)
       ──UDP───▶ audio stream     (RTP, AAC-ELD / ALAC, AES-128-CBC)
                                    │
                         FFmpeg decode → Direct2D (video) / waveOut (audio)
```

## Project layout

```
src/core/          AirPlay engine (portable C++17, also builds on Linux)
  mdns.*             mDNS / DNS-SD responder
  receiver.*         RTSP server: /info, pairing, FairPlay, SETUP, TEARDOWN
  streams.*          mirroring video, RTP audio, NTP timing
  plist.*            binary property-list reader/writer
  crypto.*           SHA-512, AES, Ed25519, X25519
  native_media.c     FairPlay + FFmpeg decoding
src/app/           Windows app (Win32, Direct2D, DirectWrite, waveOut)
  app.h              app state, drawing toolkit and the Theme interface
  render.cpp         shared drawing, Settings layout, theme picker
  theme_default.cpp  Default theme
  theme_poster.cpp   Acid poster theme
  themes.cpp         theme registry
tests/             fake_iphone.cpp: simulated iPhone for end-to-end tests
third_party/       playfair, Monocypher, tiny-AES
res/               icon, manifest, resources
```

## Building

The build runs on Linux and cross-compiles for Windows with MinGW-w64.

```bash
# Ubuntu / Debian
sudo apt install build-essential nasm git gcc-mingw-w64-x86-64 g++-mingw-w64-x86-64

./build_ffmpeg.sh win        # minimal static FFmpeg 7.1 for Windows
./build_windows.sh           # → out/win/Mirrorly.exe
```

### Adding a theme

1. Create `src/app/theme_<name>.cpp` with a class deriving from `Theme` (see `app.h`). It draws each screen
   (home, mirroring overlay, Now Playing), the Settings controls and a small preview for the picker.
2. Add its factory to `allThemes()` in `src/app/themes.cpp`. The picker shows themes in that order.
3. Optionally give it its own icon: add `N ICON "file.ico"` to `res/app.rc` and return `N` from `iconResource()`.

### Releases

Pushing a version tag builds `Mirrorly.exe` on GitHub Actions and publishes it as a GitHub Release:

```bash
git tag v1.0.0
git push origin v1.0.0
```

The version number in the tag is written into the app's About text and its Windows file properties.
To build locally with a version: `VERSION=1.0.0 ./build_windows.sh`.

### Tests

`tests/fake_iphone.cpp` acts like an iPhone. It runs discovery, pairing, the FairPlay handshake and SETUP, then streams encrypted H.264 video and audio, and checks what comes out.

```bash
./build_ffmpeg.sh linux
./build_linux.sh
./out/linux/fake_iphone /path/to/testmedia 2    # 2 = ALAC, 4 = AAC-LC
```

`tests/video_edges.cpp` checks that every pixel of every decoded frame is drawn, whatever the frame width.
iPhones send odd widths such as 1014 pixels in portrait:

```bash
ffmpeg -f lavfi -i testsrc2=size=1014x1440:rate=30 -frames:v 10 -c:v libx264 -bf 0 -f h264 odd.h264
./out/linux/video_edges odd.h264
```

The test media folder for `fake_iphone` needs these files:
- `video.h264`: Annex-B H.264
- `alac_packets.bin` or `aac_packets.bin`: audio packets, each preceded by its 32-bit little-endian length

## Credits

- [UxPlay](https://github.com/FDH2/UxPlay) and [RPiPlay](https://github.com/FD-/RPiPlay): reference for the AirPlay protocol
- playfair (from shairplay): FairPlay handshake
- [FFmpeg](https://ffmpeg.org): H.264 and audio decoding
- [Monocypher](https://monocypher.org) and [tiny-AES-c](https://github.com/kokke/tiny-AES-c): cryptography

## License

Mirrorly is released under the [GNU General Public License v3.0](LICENSE.md), because it includes GPL-3 FairPlay code.

*AirPlay, iPhone and iPad are trademarks of Apple Inc. This project is not affiliated with or endorsed by Apple.*
