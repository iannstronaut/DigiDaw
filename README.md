# DigiDAW 🎵

> **Free & Open-Source Digital Audio Workstation (DAW)**  
> Dibangun dengan C++20 murni, arsitektur modular (*Clean Architecture*), performa audio berlatensi rendah (*real-time DSP*), dan antarmuka desktop native Windows (Win32 GDI+).

---

## 🌟 Daftar Fitur yang Sudah Ada (Implemented Features)

### 1. 🎛️ Sequencer & Playlist Arranger (Placement Blocks — Gaya FL Studio)
- **Sistem Placement Block (Arranger):**
  - Pembuatan melodi pada Piano Roll tidak akan langsung berbunyi di sequencer sebelum blok ditempatkan secara eksplisit pada bar yang diinginkan.
  - Klik slot kosong `[ + ]` untuk menaruh blok melodi (*Clip*).
  - Klik tombol `[ ✕ ]` atau **klik kanan** pada blok untuk menghapusnya.
- **Mini Note Melody Preview:**
  - Setiap blok yang aktif menampilkan visual kontur miniatur not Piano Roll (posisi nada dan durasi) secara langsung di grid sequencer.
- **Navigasi & Bar Fleksibel (Tidak Terbatas pada 16 Bar):**
  - Tombol navigasi `[ ◄ Bar - ]` dan `[ Bar + ► ]` serta dukungan **Mouse Wheel** horizontal untuk menggeser bar tanpa batas (Bar 32, 64, 128, dst.).
- **Smart Song Mode & Dynamic WAV Export:**
  - Durasi pemutaran (*looping*) dan ekspor WAV otomatis menyesuaikan dengan ujung blok penempatan paling terakhir yang ada di semua track.

---

### 2. 🎹 Interactive Piano Roll (C0 s/d B10)
- **Rentang Nada Penuh C0 s/d B10 (MIDI Pitch 0–127):**
  - Mengikuti standar DAW profesional dengan penomoran oktaf FL Studio standard.
- **Navigasi Vertikal Lengkap:**
  - **Mouse Wheel:** Scroll mouse di atas Piano Roll langsung menggeser nada naik/turun secara halus.
  - **Tombol Oktaf:** `[ ◄ Oct - ]` dan `[ Oct + ► ]` untuk melompat 12 seminada.
  - **Tombol Seminada:** `[ ▼ ]` dan `[ ▲ ]` untuk penyesuaian nada presisi per 1 seminada.
  - **Scrollbar Vertikal:** Scrollbar interaktif di sisi kanan grid untuk melompat langsung ke oktaf mana pun.
- **Fitur Pemanjangan Not (Note Lengthening / Resizing):**
  - **Drag-to-Resize (`❙`):** Tarik ujung kanan not untuk memanjangkan atau memendekkan durasi secara bebas.
  - **Toolbar Default Length:** Pemilih durasi not default `[ 📏 Len: 1/2/4/8 Steps ]`.
  - **Label Not Cerdas:** Not yang dipanjangkan menampilkan jumlah ketukan/langkah (misal: `C5 (2)`, `E5 (4)`).
- **Interaksi Nada:**
  - Klik tuts piano di sebelah kiri untuk mengaudisi (*preview sound*) nada pada pitch tersebut.
  - Klik grid untuk menambahkan not; klik kanan atau klik ulang untuk menghapus not.
  - Tombol `[ Clear ]` untuk menghapus seluruh not pada channel aktif.
  - Toggle resolusi grid antara **16 Steps** (1 bar) dan **32 Steps** (2 bar).

---

### 3. ⏱️ Transport & Song Position Marker (SPM)
- **Playhead & Indikator Waktu SPM:**
  - Indikator visual berbentuk segitiga ke bawah (*downward-pointing triangle*) dengan garis penanda vertikal yang membentang dari ruler atas hingga ke dasar grid sequencer dan piano roll.
  - Berwarna **Oranye** untuk Song Position Marker (SPM) dan **Cyan** untuk Playhead yang sedang berjalan.
- **In-Place Pause vs Stop:**
  - **Pause:** SPM dan Playhead berhenti di posisi terakhir pemutaran tanpa kembali ke awal.
  - **Stop:** Menghentikan pemutaran sekaligus mengembalikan posisi SPM ke awal atau titik mulai yang ditentukan.
- **Ruler Timeline Interaktif:**
  - Klik atau geser (*drag*) pada timeline ruler untuk memindahkan titik mulai putar (*seek*) ke bar atau langkah tertentu secara instan.
- **Pencegahan Suara Stuck / Hanging Notes:**
  - Protokol *All Notes Off* otomatis memutus semua suara aktif secara bersih ketika playback di-pause, di-stop, atau saat looping kembali ke awal.

---

### 4. 🎚️ Mixer & Kontrol Gain (Faders & Meters)
- **5 Strip Mixer Bawaan:**
  - Master Track (0) dan Track 1 hingga Track 4.
- **Animasi Audio VU Meter Real-Time:**
  - VU meter stereo membaca amplitudo puncak riil per frame buffer audio dari masing-masing track instrumen (Master, Track 1, Track 2, Track 3, Track 4).
  - Dilengkapi animasi balistik *decay* yang halus.
- **Fader Volume Vertikal & Pembacaan dB:**
  - Kontrol fader vertikal interaktif dengan rentang 0.0x hingga 1.5x gain (+3.5 dB).
  - Tampilan readout desibel presisi (misal: `-inf dB`, `-6.0 dB`, `+0.0 dB`, `+3.5 dB`).

---

### 5. 🔊 Generator Suara & Efek DSP Bawaan
- **Synthesizer 3xOsc Subtractive (`core.generator.3xosc`):**
  - 3 Osilator independen dengan pilihan bentuk gelombang: Sine, Triangle, Sawtooth, Square, dan White Noise.
  - Kontrol Coarse Tuning (oktaf/semitone), Fine Detune, dan Mix Level per osilator.
  - Polyphonic Voice Manager dengan amplop volume ADSR (Attack, Decay, Sustain, Release).
- **GUI Editor Instrumen Terintegrasi:**
  - Klik tombol nama channel (misal: `3xOsc Synth #1 ⚙`) pada Channel Rack untuk membuka jendela GUI pengaturan suara synthesizer secara visual.
- **Sampler & Drum Sampler:**
  - Playback sampel audio dengan *pitch resampling* dan *pad triggers* dengan *choke/mute groups*.
- **Prosesor Efek & DSP:**
  - Lookahead Peak Limiter (mencegah clipping digital).
  - Filter Biquad Lowpass & Highpass.
  - Dynamic Range Compressor dengan kontrol Threshold, Ratio, Attack, Release, dan Makeup Gain.
  - Convolution / Algorithmic Reverb dengan simulasi peluruhan akustik ruangan.
  - One-Pole Parameter Smoother (menghilangkan audio glitch / zipper noise saat menggeser fader).

---

### 6. 💾 Manajemen Proyek, Ekspor & Interoperabilitas
- **Format Proyek Biner `.odp` (Open DAW Project):**
  - Penyimpanan chunked biner berkecepatan tinggi dengan integritas magic header dan forward-compatibility.
  - Fitur Autosave dan pemulihan darurat (*recovery*).
- **Offline WAV Exporter:**
  - Ekspor bitwise-deterministic ke format standar audio RIFF/WAVE 44.1 kHz 16-bit stereo.
- **Integrasi Desktop Windows:**
  - Registrasi *File Association* `.odp` (klik ganda file proyek langsung membuka DigiDAW).
  - Dukungan *Drag-and-Drop* file proyek (`WM_DROPFILES`) langsung ke jendela aplikasi.
  - Out-of-process Plugin Crash Isolation via `PluginScanner.exe` dan `PluginBridge.exe`.
- **C API Scripting Facade:**
  - Antarmuka C-API untuk otomasi headless dan integrasi scripting eksternal.

---

## ⌨️ Pintasan Keyboard (Shortcuts)

| Shortcut | Fungsi |
| :--- | :--- |
| **`Spasi`** | Toggle Play / Pause |
| **`Esc`** | Menutup GUI VST / Kembali ke Channel Rack / Stop Playback |
| **`F6`** | Membuka tampilan **Channel Rack / Sequencer** |
| **`F7`** | Membuka tampilan **Piano Roll** |
| **Mouse Wheel (Sequencer)** | Menggeser Bar Sequencer ke kiri / kanan |
| **Mouse Wheel (Piano Roll)** | Menggeser rentang nada Piano Roll naik / turun |
| **Klik Kiri (Sequencer Pad `+`)** | Menambahkan Placement Block pada Bar |
| **Klik Kanan / Tombol `✕`** | Menghapus Placement Block pada Bar |
| **Drag Ujung Kanan Not (`❙`)** | Memanjangkan / memendekkan durasi not di Piano Roll |

---

## 🏗️ Struktur Arsitektur Kode (*Clean Architecture*)

```
DigiDaw/
├── adapters/
│   ├── c_api/               # C-API Facade untuk otomasi & scripting
│   └── gui/                 # Native Win32 Window, GUI Renderer, dan tema FL-Style
├── app/
│   ├── engine.hpp           # Core Audio Engine real-time & threading
│   └── usecases/            # Transport, Project Session, Plugin Manager, WAV Renderer
├── domain/
│   ├── dsp/                 # 3xOsc Synth, Sampler, Limiter, Reverb, Compressor, Filter
│   ├── mixer/               # Graph routing, topological sort, pan laws, VU meters
│   ├── sequencing/          # Track, Clip, Note, Pattern, Channel
│   └── time/                # Time Map, PPQ (960 ticks/quarter), BPM converter
├── sdk/                     # Plugin SDK interface (C++ plugin host/guest)
├── shell/
│   └── main.cpp             # Entry point (GUI window & CLI REPL launcher)
├── tests/                   # 35 Automated Unit & Integration Tests (100% Pass)
└── build.py                 # Skrip build & automated test runner MinGW GCC
```

---

## 🚀 Cara Kompilasi & Menjalankan

### Persyaratan:
- Sistem Operasi: **Windows 10 / 11**
- Compiler: **MinGW-w64 GCC (mendukung C++20)**
- Python 3 (untuk menjalankan skrip `build.py`)

### Langkah Kompilasi:
1. Pastikan compiler MinGW telah terpasang (default path: `D:\mingw64\bin\g++.exe` atau sesuaikan pada `build.py`).
2. Jalankan perintah kompilasi:
   ```cmd
   python build.py
   ```
3. Binary yang dihasilkan berada di folder `bin/`:
   - `bin/DigiDAW.exe` — Aplikasi utama (Native Desktop GUI)
   - `bin/run_tests.exe` — Test runner terotomasi (35 test cases)
   - `bin/PluginBridge.exe` — Out-of-process plugin bridge
   - `bin/PluginScanner.exe` — Plugin sandbox scanner

---

## 📄 Lisensi
DigiDAW dilisensikan di bawah lisensi Open Source (MIT / GPL-compatible). Bebas digunakan, dipelajari, dan dikembangkan lebih lanjut.
