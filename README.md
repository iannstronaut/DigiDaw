# DigiDAW 🎵

> **Free & Open-Source Digital Audio Workstation (DAW)**  
> Dibangun dengan C++20 murni, arsitektur modular (*Clean Architecture*), performa audio berlatensi rendah (*real-time DSP*), dan antarmuka desktop **Direct2D 1.1 + Direct3D 11 Shaders (Hardware-Accelerated GPU Rendering)**.

---

## 🌟 Daftar Fitur yang Sudah Ada (Implemented Features)

### 0. ⚡ GPU Acceleration: Direct2D + Direct3D 11 Shaders
- **Direct3D 11 Audio-Reactive Shader Visualizer:**
  - *Pixel Shader* berlatensi nol yang dikompilasi langsung di GPU via `D3DCompile` (`HLSL`).
  - Efek pencahayaan dinamis (*ambient reactive aura*) di latar belakang mixer saat instrumen dan track audio dimainkan.
- **Direct2D 1.1 Hardware Vector Graphics:**
  - Rendering 2D dengan *sub-pixel anti-aliasing* penuh: garis kisi mulus, sudut membulat (*rounded corners* tanpa gerigi), dan kurva tajam.
  - Multi-stop Linear Gradient Brushes untuk VU meter (gradien hijau limau -> kuning amber -> merah neon) dan fader mixer berkilau (*metallic glossy sliders*).
  - DXGI SwapChain BackBuffer presentation dengan refresh rate 60 FPS *tear-free* dan *vsync-locked*.
- **DirectWrite Sub-Pixel Typography:**
  - Render tipografi teks subpixel presisi tinggi (Segoe UI & Consolas) untuk penomoran bar, nama nada Piano Roll (`C0` s/d `B10`), meter dB, dan pembacaan BPM.
- **Zero-Crash Graceful Fallback:**
  - Otomatis beralih ke *GDI Double-Buffered Engine* jika dijalankan pada mesin tanpa hardware GPU Direct3D 11 yang kompatibel.

---

### 1. 🎛️ Playlist Arranger & Sequencer Grid (FL Studio Style)
- **Langsung Buka Full Layar (Maximized & Fullscreen Ready):**
  - Aplikasi otomatis terbuka langsung dalam mode **Maximized (Full Layar)** pada resolusi monitor pengguna dengan GPU buffer Direct2D/Direct3D yang langsung menyesuaikan ukuran layar.
  - Mendukung tombol pintas **F11** untuk beralih (*toggle*) antara mode jendela maksimal dan mode *borderless kiosk fullscreen*.
- **Horizontal Scrollbar Interaktif (Menggantikan Tombol Klik Kanan/Kiri):**
  - **Draggable Scrollbar Thumb:** Dilengkapi batang penggulung (*horizontal scrollbar*) di bagian bawah arranger dengan pegangan (*thumb*) yang dapat diklik dan digeser (*drag*) secara kontinu ke kiri maupun ke kanan.
  - **Click-to-Jump:** Klik pada area trek scrollbar mana pun untuk langsung melompat (*jump*) ke bar yang dituju.
  - **Mouse Wheel & Tilt Wheel Support:** Putar roda mouse vertikal untuk menggeser timeline secara halus per bar, atau gunakan roda horizontal/touchpad gesture untuk navigasi menyamping.
  - **Jangkauan Bar Tak Terbatas:** Timeline otomatis memperluas jangkauan (*dynamic expansion*) hingga 64, 128, bahkan 256 bar mengikuti klip terjauh.
  - **Indikator Rentang Bar:** Header arranger menampilkan badge rentang bar aktif secara real-time (misal: `↔ Showing Bars 1-16 / 64`).
- **Continuous Playlist Grid & Beat Subdivisions:**
  - Desain timeline arranger penuh dengan ruler bernomor bar presisi (`1`, `2`, `3`, `4`, `5`, `6`, dst.) dan garis subdivisi per ketukan (4 ketukan per bar).
- **Draggable Clips disetiap Ketukan (Beat-Snapping):**
  - **Move Drag:** Klik dan geser badan blok (*clip body/header*) ke kiri atau ke kanan untuk memindahkan posisi klip, otomatis mengunci (*snap*) pada setiap ketukan (*beat / ppq ticks*).
  - **Resize & Lengthen Drag:** Geser ujung kanan blok (*right edge handle*) untuk memanjangkan atau memendekkan durasi blok secara bebas (1 bar, 2 bar, 4 bar, dst.) mengunci presisi per ketukan.
  - **Click to Place:** Klik pada area track yang kosong untuk menempatkan blok klip melodi baru pada ketukan tersebut.
  - **Right-Click to Delete:** Klik kanan pada blok klip mana pun untuk menghapusnya secara instan.
- **Desain Blok Klip Mirip FL Studio:**
  - Header banner atas dengan ikon pola `≡` dan judul (misal: `Pattern 1` / nama instrumen).
  - Badan klip kontainer rounded slate dengan garis kisi ketukan.
  - Menampilkan pratinjau melodi miniatur berbentuk batang perak horizontal (*silver note bars*) yang proporsional dengan nada (*pitch*) dan durasi aslinya dari Piano Roll.
- **Downward Lime-Green Playhead Marker:**
  - Penanda Playhead dan Song Position Marker (SPM) berbentuk segitiga panah ke bawah berwarna hijau limau (*lime green*) di atas ruler bar, dengan garis panduan vertikal melintasi seluruh jalur track.
- **Smart Song Mode & Dynamic WAV Export:**
  - Durasi pemutaran (*looping*) dan ekspor WAV otomatis menyesuaikan dengan ujung klip paling terakhir yang ditempatkan di semua track.
  - Melodi pada klip yang dipanjangkan (misal 4 bar) akan memainkan not-not pola secara utuh dan berulang rapi (*pattern looping*).

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

### 3. ⏱️ Transport & Real-Time Position Clock
- **Tombol Kontrol Play, Pause, Stop Presisi:**
  - `▶ PLAY`: Memulai pemutaran instrumen/lagu secara instan dari posisi Playhead/SPM (atau spasi keyboard).
  - `❚❚ PAUSE`: Menghentikan sementara (*in-place pause*) di posisi berjalan tanpa memundurkan timeline, serta memutus suara instrumen (*all notes off*) agar tidak berdengung.
  - `■ STOP`: Menghentikan playback, membersihkan seluruh suara aktif, dan me-rewind posisi Playhead/SPM kembali ke awal (00:00.00 | Bar 1).
  - Area klik (*hitbox*) tombol terkalibrasi 100% presisi dan selaras dengan engine grafis GPU Direct2D.
- **Jam Digital Real-Time Standar (Mulai dari 00:00.00):**
  - Penghitung waktu di header transport kini menggunakan format standar jam musik: `Menit : Detik . Centisecond | Bar` (contoh: `00:00.00  |  Bar 1` saat awal).
  - Menghilangkan kebingungan penomoran bar/beat sebelumnya (`001:01:000`) dengan waktu riil yang selalu dimulai dari detik 0.
- **Header Bersih & Rapi:**
- **Analog Real-Time Audio Signal Oscilloscope (Di Sebelah Tombol Piano Roll):**
  - Section khusus berbentuk display osiloskop CRT analog (*hardware CRT / OLED chassis*) yang terletak tepat di sebelah kanan tombol `[ 🎹 PIANO ROLL ]`.
  - **Garis Sinyal Audio Real-Time (Waveform Time-Domain):** Menggambarkan sinyal suara aktual yang sedang keluar dari audio engine, bukan grafik spektrum frekuensi bar.
  - **Posisi Idle di Tengah (0V Baseline):** Saat tidak ada suara atau musik berhenti, berkas laser neon berada dalam keadaan tenang tepat di **tengah-tengah horizontal layar** (garis lurus di $Y = \text{center}$).
  - **Membentuk Gelombang saat Bersuara:** Begitu instrumen atau klip lagu berbunyi, berkas garis langsung berosilasi dinamis ke atas dan ke bawah membentuk pola gelombang suara riil (pola sinusoidal, gigi gergaji synth, transient dentuman drum, dsb.).
  - **Dual-Layer Neon Phosphor Glow:** Garis sinyal dirender dengan pendaran ganda: *outer phosphor halo* (3.5px) berwarna cyan bercahaya dan *inner neon laser filament* (1.6px) tajam berwarna putih-cyan.
  - **Triggering Nol-Lintas (Zero-Crossing):** Dilengkapi sinkronisasi fasa otomatis agar bentuk gelombang nada musik tampil stabil di layar tanpa bergetar acak.
  - **Graticule Reticle & Lampu Clip LED:** Garis kisi referensi tegangan 0V dan rel batas $\pm 0.5$, serta lampu LED analog di sudut kanan atas yang menyala merah saat output mencapai level peak/clipping.
- **Ruler Timeline Interaktif & Downward Playhead:**
  - Klik atau geser (*drag*) pada timeline ruler untuk memindahkan titik mulai putar (*seek*) ke bar atau ketukan tertentu secara instan.
  - Indikator Playhead berbentuk panah segitiga ke bawah berwarna hijau limau (*lime green*) melintasi seluruh jalur track.
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
