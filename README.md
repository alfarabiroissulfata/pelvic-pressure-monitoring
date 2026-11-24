# Pelvic Pressure Monitoring System

Dashboard prototipe pemantauan distribusi pembebanan pada area panggul menggunakan empat sensor FSR dan ESP32 melalui Bluetooth Low Energy (BLE).

## Arsitektur

- **GitHub Pages**: hosting halaman dashboard melalui HTTPS.
- **ESP32 + FSR1–FSR4**: akuisisi ADC dan kontrol melalui BLE; tidak membutuhkan internet.
- **Browser laptop**: monitoring, timestamp penerimaan BLE, grafik, CSV, dan penyimpanan sesi lokal (IndexedDB).
- **Firebase Authentication (Anonymous) + Cloud Firestore**: sinkronisasi sesi dan penyajian laporan QR lintas perangkat.

## File utama

- `index.html` — halaman utama.
- `Laptop_Monitor.html` — monitoring BLE dan pencatatan sesi.
- `HP_Control.html` — kontrol BASELINE / START / STOP melalui BLE.
- `Riwayat_Pengukuran.html` — riwayat lokal dan QR sesi yang sudah tersinkron.
- `Laporan_FSR4.html` — laporan sesi dari Firestore.
- `firebase-config.js` — konfigurasi Firebase Web App untuk project `fsr4-monitoringg`.
- `firestore.rules` — salinan aturan Firestore untuk prototipe.
- `FSR4_ESP32_BLE_Full_1s_StringLength.ino` — firmware referensi ESP32.

## 1. Upload ke GitHub Pages

Repository yang dituju: `alfarabiroissulfata/pelvic-pressure-monitoring`.

Upload **isi folder paket ini** langsung ke root repository (bukan folder paket sebagai subfolder). Pastikan `index.html`, seluruh halaman HTML, `firebase-config.js`, dan `.nojekyll` berada sejajar di root.

Kemudian buka **Settings → Pages**:

1. Build and deployment: `Deploy from a branch`.
2. Branch: `main`.
3. Folder: `/(root)`.
4. Klik `Save` dan tunggu deployment selesai.

URL yang diharapkan:

- `https://alfarabiroissulfata.github.io/pelvic-pressure-monitoring/`
- `https://alfarabiroissulfata.github.io/pelvic-pressure-monitoring/Laptop_Monitor.html`
- `https://alfarabiroissulfata.github.io/pelvic-pressure-monitoring/HP_Control.html`
- `https://alfarabiroissulfata.github.io/pelvic-pressure-monitoring/Riwayat_Pengukuran.html`

URL baru aktif setelah GitHub Pages berhasil menerbitkan repository.

## 2. Firebase yang harus disiapkan satu kali

Firebase project: `fsr4-monitoringg`.

1. Authentication → Sign-in method → Anonymous: **Enabled**.
2. Authentication → Settings → Authorized domains: tambahkan `alfarabiroissulfata.github.io` (host saja, tanpa `https://` dan tanpa path repository).
3. Cloud Firestore: gunakan database `(default)`.
4. Firestore → Rules: cocokkan dengan file `firestore.rules`, lalu klik **Publish**.

Rules saat ini mengizinkan pengguna anonim terautentikasi membaca satu dokumen berdasarkan ID (`get`) serta membuat/memperbarui sesi. Rules menolak `list` dan `delete`. Ini hanya untuk prototipe; bukan kontrol akses per pasien. Jangan menyimpan nama, NIK, nomor rekam medis, atau identitas pasien.

## 3. Alur pemakaian

1. Buka `Laptop_Monitor.html` dari URL GitHub Pages di Chrome/Edge laptop.
2. Hubungkan ESP32 bernama `FSR4-Monitor` melalui Web Bluetooth.
3. Lakukan BASELINE, START, pengukuran, lalu STOP.
4. Sesi disimpan lokal pada browser laptop. Jika internet tersedia dan Firebase siap, dashboard mencoba menyinkronkan sesi ke Firestore.
5. Jika belum tersinkron, buka `Riwayat_Pengukuran.html` dan tekan **Sinkronkan Firebase**.
6. QR hanya dibuat untuk sesi yang sudah tersinkron. Pindai dengan HP untuk membuka `Laporan_FSR4.html?id=...`.
7. HP Control dibuka dari Chrome Android melalui HTTPS. iOS Safari tidak mendukung Web Bluetooth.

## 4. Batasan penting

- BLE antara laptop dan ESP32 tidak memerlukan internet. Firebase hanya dipakai untuk sinkronisasi dan laporan QR.
- Web Bluetooth membutuhkan secure context (HTTPS atau localhost). GitHub Pages menyediakan HTTPS.
- Timestamp sampel adalah waktu lokal laptop ketika notifikasi BLE diterima, bukan timestamp RTC di ESP32.
- Penyimpanan lokal terikat pada browser/perangkat yang sama. Sesi lokal lama dari `file://` tidak otomatis pindah ke origin GitHub Pages. Buka origin lama dan ekspor CSV jika perlu menyimpan salinan; sesi lokal lama dapat memerlukan migrasi manual.
- QR lintas perangkat hanya berfungsi setelah sesi berhasil disinkronkan ke Firestore.
- Firebase Web API key yang berada di halaman web adalah pengenal aplikasi, bukan kunci admin. Jangan pernah menaruh service-account/private key di repository.
- Data FSR adalah pembacaan sensor untuk pemantauan paparan pembebanan, bukan diagnosis luka tekan. Kalibrasi awal dan indikator distribusi pada dashboard bersifat provisional dan perlu validasi teknis/klinis.
- Uji sistem dengan data dummy terlebih dahulu. Jangan gunakan data kesehatan teridentifikasi.

## 5. BLE interface

- Device name: `FSR4-Monitor`
- Service UUID: `6e400001-b5a3-f393-e0a9-e50e24dcca9e`
- Command Write UUID: `6e400002-b5a3-f393-e0a9-e50e24dcca9e`
- Data Notify UUID: `6e400003-b5a3-f393-e0a9-e50e24dcca9e`
- Status Notify UUID: `6e400004-b5a3-f393-e0a9-e50e24dcca9e`
- Payload: `ADC1,ADC2,ADC3,ADC4`
- FSR pins: GPIO 34, 35, 32, 33
- Buttons: BASELINE GPIO 25, START GPIO 26, STOP GPIO 27
- Target interval: 1000 ms

Catatan: kemampuan laptop dan HP tersambung bersamaan bergantung pada dukungan multi-client BLE firmware/stack. Uji terpisah terlebih dahulu; jangan mengasumsikan koneksi simultan pasti berhasil.
