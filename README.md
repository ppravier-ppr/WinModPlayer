# WinModPlayer (Windows 11 Tracker Module Player)

A compact, standalone, native C++ music player for Windows 11 designed specifically for demoscene and retro tracker music (**MOD, XM, S3M, IT, MED, MPTM, 669, STM**, and more).

Directly inspired by our Android **CarModPlayer**, it features the same high-contrast cyber/retro dark UI, real-time channel VU meters, instrument/sample inspector, and low-latency audio playback powered by **libopenmpt**.

---

## ✨ Features

- **Built with libopenmpt**: Uses the exact same OpenMPT audio playback engine as the Android app, ensuring cycle-accurate demoscene playback (Amiga ProTracker, FastTracker II, Scream Tracker 3, Impulse Tracker, OctaMED, etc.).
- **Ultra-Compact & Zero Dependencies**: Compiled statically (`/MT`) into a single **2.1 MB** executable. No installers, no runtime DLLs required, no external dependencies.
- **Windows 11 & Dark Mode Aesthetic**:
  - Immersive Dark Mode title bar (`DwmSetWindowAttribute`).
  - High-contrast color palette: Electric Cyan (`#00E5FF`), Neon Amber (`#FFB300`), Deep Carbon (`#0F1218`).
  - Smooth double-buffered GDI+ / ClearType rendering.
  - Per-Monitor V2 DPI awareness with crisp scaling.
- **Real-Time Channel Visualizer**: Dynamic multi-channel VU meter bars reflecting actual individual channel activity and volume peaks.
- **Full Metadata & Instrument Inspector**:
  - **Playlist Tab**: Lists tracks with format badges (MOD, XM, IT, S3M, MED), channel counts, durations, and file sizes. Background metadata scanner parses tags without stalling the UI.
  - **Instruments Tab**: Displays full instrument list, sample names, and tracker commentary / song messages.
  - **Shortcuts Modal**: Dedicated **`[⌨ SHORTCUTS]`** button in the header (or press `F1` / `?`) opening a styled cyber overlay cheat-sheet of all handy controls.
- **Low-Latency WASAPI Audio**:
  - Event-driven shared-mode WASAPI streaming.
  - Dynamic default audio device switching (`IMMNotificationClient`) when plugging/unplugging headphones or switching speakers.
- **Windows Integration**:
  - **Taskbar Thumbnail Toolbar**: Previous, Play/Pause, Next controls directly from the Windows taskbar preview.
  - **Taskbar Progress**: Real-time track progress shown directly on the taskbar icon.
  - **Global Media Keys**: Handles Play/Pause, Next, Previous, and Stop keys across the OS.
  - **Drag & Drop**: Drag files or folders onto the window to play immediately.
  - **Single Instance**: Opening another file forwards the song to the existing player window.

---

## ⌨️ Keyboard Shortcuts

| Key | Action |
| :--- | :--- |
| `Space` | Play / Pause |
| `Ctrl + Right` / `N` | Next Track |
| `Ctrl + Left` / `P` / `B` | Previous Track |
| `Left` / `Right` | Seek backward / forward 5 seconds |
| `Up` / `Down` | Navigate playlist |
| `Enter` | Play selected track |
| `Tab` | Switch between **Playlist** and **Instruments** tabs |
| `S` | Toggle Shuffle |
| `R` | Toggle Repeat / Loop |
| `+` / `-` (or mouse wheel over volume) | Adjust volume |
| `M` | Mute / Unmute |
| `O` | Open Folder dialog |
| `L` | Jump to currently playing track in playlist |
| `F5` | Refresh / rescan folder |
| `F1` / `?` | Toggle the Handy Shortcuts window |
| `Esc` | Close the Shortcuts window |

---

## 🛠️ Building from Source

Requirements:
- Windows 10/11
- Visual Studio 2019 / 2022 Build Tools (with C++ toolset)
- CMake 3.20+ and Ninja (included or on PATH)

Simply run:
```bat
build.bat
```
This builds `build\WinModPlayer.exe` and copies the standalone binary to `WinModPlayer.exe`.
