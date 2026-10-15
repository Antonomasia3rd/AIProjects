# Minecraft-Bedrock-AFK v1.7.16-patched

## v1.7.16: post-respawn local-player reinitialization

The v1.7.15 controlled run proved that post-respawn physics is stable, MOVE is not input-locked, and the server still processes corrections while refusing horizontal movement. v1.7.16 keeps the proven v1.7.11 respawn handshake and v1.7.13-v1.7.15 movement/tick fixes, then resends `set_local_player_as_initialized` after the authoritative respawn settle and before the first re-arm probe. It also logs `play_status: player_spawn` during respawn and treats it as an explicit reinitialization trigger. No destructive packets are enabled.

# v1.7.14 — preserve server-data ACK + handled_teleport before movement re-arm

v1.7.13 proved that movement tick correlation is correct and that the server keeps processing post-respawn PlayerAuthInput packets, but every controlled movement probe is corrected back to exactly the same authoritative position. The target server returns the player to the death location through a large `CorrectPlayerMovePrediction` correction rather than a visible `MovePlayer(mode=teleport)` packet.

This release keeps the v1.7.11 respawn handshake, v1.7.12 settle phase, and v1.7.13 tick/re-arm logic unchanged, but fixes the acknowledgement boundary between settle and re-arm:

- A multi-block `CorrectPlayerMovePrediction` received during post-respawn settle is treated as teleport-equivalent.
- The next `PlayerAuthInput` includes both `received_server_data` and `handled_teleport`.
- Pending ACK flags are no longer cleared when transitioning from position settle to movement re-arm.
- Re-arm warmup therefore sends a neutral ACK packet before the controlled movement probe.
- The ACK is logged with its tick and reason so live logs can prove whether it was actually emitted.

Expected sequence:

```text
post-respawn authoritative correction (large shift)
-> queue handled_teleport ACK
-> position settle completes
-> re-arm neutral PlayerAuthInput [received_server_data, handled_teleport]
-> controlled movement probe
-> authoritative server movement observed
-> wandering released
```

---

# v1.7.13 — post-respawn movement re-arm + authoritative tick correlation

This release keeps the proven v1.7.11 respawn handshake unchanged and builds on the v1.7.12 post-respawn position settle.

Changes:

- Seeds `PlayerAuthInput.tick` from `start_game.current_tick` instead of always beginning at tick 1. Bedrock server-authoritative movement correlates input/correction packets by this tick timeline.
- Logs `startGameCurrentTick`, `nextPlayerAuthInputTick`, and `rewindHistorySize` at startup.
- Adds a post-respawn movement re-arm phase after the v1.7.12 position settle.
- Re-arm begins with neutral input, then sends a small controlled forward probe (`moveZ=0.35`).
- Normal wandering is not released until an authoritative `MovePlayer`/`CorrectPlayerMovePrediction` proves horizontal movement (`>=0.06` blocks or non-zero server horizontal velocity).
- Stuck/recovery and server-reject heuristics remain suppressed during re-arm, preventing false recovery loops while the movement channel is being re-established.
- If no authoritative movement is observed in 5 seconds, re-arm retries instead of pretending that normal wandering is working.
- The v1.7.11 respawn handshake remains exactly: client Respawn state 2 -> server ReadyToSpawn state 1 -> standalone PlayerAction action 6, face -1.

Expected post-respawn sequence:

```text
ReadyToSpawn
-> final PlayerAction action 6 face -1
-> post-respawn position settle
-> authoritative teleport/correction captured
-> movement re-arm neutral packet
-> controlled movement probe
-> authoritative server movement observed
-> wandering released
```

---

## v1.7.15 — authoritative post-respawn physics + input-lock diagnostics

- Keeps the proven v1.7.11 respawn handshake unchanged.
- Re-arm probes no longer run normal local gravity. Y/vertical delta stay anchored to the latest authoritative server correction, preventing the client from locally falling while BDS holds the respawn pose.
- Horizontal re-arm probes are stateless and originate from the latest authoritative position, preventing accumulated prediction drift before movement acceptance is proven.
- Observes `update_client_input_locks`; if the server locks MOVE, the bot waits and sends neutral input instead of spamming rejected probes.
- Instruments self `set_entity_motion` during death/sync/re-arm to expose server-applied motion.


## v1.7.12 post-respawn authoritative settle

v1.7.11 fixed the death/respawn disconnect loop, but the target server can immediately move the player again after respawn (the server itself says "You are back where you died"). The ReadyToSpawn position is therefore not always the final authoritative position.

v1.7.12 leaves the proven v1.7.11 respawn handshake untouched and adds a post-respawn settle phase only. After the final standalone PlayerAction succeeds, AI/pathfinding/jump/swim/stuck recovery stay paused while the client sends neutral PlayerAuthInput packets. MovePlayer/CorrectPlayerMovePrediction feedback is accepted and rebases the predictor without counting as movement rejection. After the settle window captures the server position, prediction history and rejection state are cleared and normal wandering resumes from the latest authoritative position.

Defaults are internal/fallback-safe (`~2.2s` minimum settle, `~4.2s` maximum, `~350ms` quiet period after the latest authoritative update) and may be overridden later with `survival.postRespawnSyncMinMs` / `postRespawnSyncMaxMs` without requiring config changes. Overlays intentionally do not overwrite `config.js`.

## v1.7.10 PlayerAuthInput block_action shape fix

v1.7.9 selected the correct server-authoritative channel, but its smoke test exposed a serialization bug before the bot could be started: `PlayerAuthInput.block_action` was emitted as `{ action }` only. The installed `bedrock-protocol`/ProtoDef schema requires a complete `PlayerBlockActionData` object and tried to read `position.x` from the missing position.

v1.7.10 now emits the Respawn block action as `{ action: <wire-6 compat token>, position: { x: 0, y: 0, z: 0 }, face: 0 }`. The non-destructive guard permits only that exact zero-position/face-0 Respawn form and still rejects all destructive or unknown block actions. `npm test` must pass before live testing.


## v1.7.9 respawn: server-authoritative PlayerAuthInput action

BDS 1.26.51 uses server-authoritative movement. After the observed `Respawn state 0 -> client state 2 -> server state 1` transition, v1.7.9 sends Respawn as `PlayerAuthInput` with the `block_action` input flag and `block_action` with the protocol-2193 Respawn action. The legacy standalone `player_action` packet is no longer used by the respawn state machine. v1.7.10 later fixed the required zero-position/face-0 shape for this payload. Non-destructive safety still fails closed on destructive block actions.

# v1.7.8 observed-BDS respawn handshake + face=0 isolation fix

v1.7.8 combines the two halves that were independently proven by the controlled tests. v1.7.6's ordering (`client Respawn state 2 -> server ReadyToSpawn state 1 -> final PlayerAction`) was the only ordering that actually advanced this BDS to state 1, but its final PlayerAction still used `face: -1`. v1.7.7 corrected the protocol-2193 PlayerAction to wire action 6 + `face: 0`, but changed the ordering back to PlayerAction-first, and the server then stayed at state 0.

This build therefore keeps the empirically successful v1.7.6 ordering and combines it with the corrected v1.7.7 action payload: after `SearchingForSpawn`, send `Respawn(ClientReadyToSpawn/state=2, position=0,0,0)`; after the server explicitly replies `ReadyToSpawn/state=1`, send exactly one `PlayerAction(respawn)` encoded as wire action 6 with zero block/result positions and `face: 0`. Ordinary movement/correction/positive-health packets cannot finish respawn until *all three* handshake facts are true (`state2 sent`, `state1 received`, final PlayerAction sent), preventing the initial state-2 request from being mistaken for a completed respawn.

The disconnect black-box files remain included in both full and overlay packages as repair-safe files. `.bot-instance-id` and existing `disconnect-blackbox.jsonl` are never included in overlays and are not deleted.

# v1.7.7 canonical respawn + face=0 fix

v1.7.7 fixes the post-death respawn failure exposed by the v1.7.6 black-box logs. v1.7.6 proved the server could reach `ReadyToSpawn`, but our `PlayerAction(respawn)` still used `face: -1`. Bedrock's non-block PlayerAction form uses zero block/result positions and `face: 0`; BDS 1.26.51 could ignore the otherwise-correct wire action 6 when the payload was non-canonical.

The handshake is now canonical and state-driven: `PlayerAction(respawn, wire=6, face=0)` requests the respawn, the bot waits for server `ReadyToSpawn`, then sends exactly one `Respawn(state=2 / ClientReadyToSpawn)` using the server-provided respawn position. That client state-2 packet is treated as the final ACK. The bot prefers a following authoritative `MovePlayer`, correction, positive health, or respawn event, but after a short grace it adopts the server ReadyToSpawn position locally instead of force-disconnecting a healthy session.

The persistent per-install `.bot-instance-id`, strict non-destructive outbound guard, protocol-2193 mapper shim, and disconnect black-box remain unchanged. Overlay packages remain rootless and never contain `.bot-instance-id`.

# v1.7.6 respawn handshake correction

v1.7.6 fixes the remaining respawn ordering bug found by the v1.7.5 black-box logs. The client now sends `Respawn(state=2 / ClientReadyToSpawn)` first, waits for server `state=1 / ReadyToSpawn`, then sends the final protocol-2193 `PlayerAction(respawn)` wire ordinal 6. Runtime ID 0 from server respawn packets is still ignored. Ordinary `MovePlayer` packets received while dead can no longer falsely complete respawn before state 1 + final PlayerAction.

# v1.7.5 protocol-2193 respawn compatibility fix

This build fixes the remaining confirmed `local-respawn-timeout` on Bedrock protocol 2193. The 1.26.51 protocol defines `PlayerAction.Respawn` as wire ordinal **6**, while the currently bundled `minecraft-data` mapper may serialise symbolic `respawn` as ordinal **7** (the value introduced for the 1.26.60-preview protocol family). v1.7.5 probes the actual serializer at startup and selects a symbolic compatibility token that emits wire ordinal 6 without bypassing the non-destructive safety guard.

Respawn is now state-driven: after death/`SearchingForSpawn`, the bot sends one correct PlayerAction respawn request, waits for server `ReadyToSpawn`, then replies with `Respawn(ClientReadyToSpawn)`. Only one delayed PlayerAction retry is allowed while the server remains in state 0. The black-box report now records respawn state history, wire ordinal, selected serializer token, and whether the compatibility shim was active.

Expected startup on the affected mapper looks like:

```text
Respawn wire action: 6 via token 'stop_sleeping' (compat shim aktif)
Mapper protocol symbolic respawn=7; protocol 2193 butuh 6. Compatibility shim digunakan.
```

The token name above is an internal serializer workaround only: the emitted protocol-2193 wire value is 6, which the server interprets as Respawn.

# v1.7.4 respawn hotfix

This build fixes the confirmed 9-second self-disconnect after death. The old flow sent `Respawn(ClientReadyToSpawn)` first and waited for `ReadyToSpawn`. v1.7.4 now starts with `PlayerAction(respawn)`, waits for the server `Respawn(ReadyToSpawn)`, then replies with `Respawn(ClientReadyToSpawn)`. It also refuses to overwrite a valid local runtime entity ID with `0` from a respawn packet. Respawn packets are logged while dead for diagnostics.

# Minecraft Bedrock AFK Bot — Patched 1.7.3

Patch ini dibuat untuk memperbaiki project yang sebelumnya mengalami masalah movement/jump, swimming, obstacle avoidance, respawn, reconnect, dan transport selection.

## Perubahan utama

- `config.js` sekarang benar-benar dipakai oleh `bot.js`.
- `player_auth_input.input_data` diperbarui ke format Minecraft Bedrock 1.26.51 (`[]InputData`).
- Prediksi posisi lokal dipertahankan antar-tick dan server correction tetap dihormati.
- Jump memakai vertical prediction sederhana dan `start_jumping` hanya dikirim saat awal jump.
- Deteksi block dinormalisasi (`air`, `water`, dll.) agar `air` tidak lagi dianggap solid.
- Swimming state ditambahkan dengan `start_swimming`, `stop_swimming`, `auto_jumping_in_water`, `want_up`, dan `ascend`.
- Obstacle avoidance diganti dengan local steering planner yang memilih arah dengan clearance terbaik dan membelok lebih halus.
- Tracking player diperbaiki dari `runtime_entity_id` menjadi `runtime_id` dan `unique_id`.
- Respawn otomatis ditambahkan dengan handshake `ClientReadyToSpawn`.
- Reconnect otomatis dengan backoff 2s → 5s → 10s → 20s → 30s.
- Transport default sekarang `auto`; `bedrock-protocol` dapat mencoba discovery RakNet/NetherNet.
- Timer dibersihkan saat disconnect agar tidak menumpuk setelah reconnect.
- `npm start` dan `npm run check` ditambahkan.
- Node.js minimum dibuat eksplisit: Node 24+.

## Instalasi Windows

1. Install Node.js 24 atau lebih baru.
2. Jangan memakai `node_modules` dari komputer Linux/Armbian temanmu.
3. Di folder project:

```powershell
Remove-Item -Recurse -Force node_modules -ErrorAction SilentlyContinue
npm ci
npm run check
npm start
```

Debug protocol:

```powershell
$env:DEBUG="minecraft-protocol"
npm start
```

## Override konfigurasi tanpa edit source

PowerShell:

```powershell
$env:MC_HOST="alamat-server"
$env:MC_PORT="19132"
$env:MC_USERNAME="NamaBot"
$env:MC_TRANSPORT="auto"  # auto / raknet / nethernet
npm start
```

## Urutan test yang disarankan

1. Connect + spawn.
2. Jalan lurus dan lihat apakah posisi berubah stabil.
3. Aktifkan `testing.forceJumpTest = true` untuk uji jump paksa.
4. Matikan lagi forced jump lalu uji auto-jump di rintangan 1 block.
5. Masuk ke air dan lihat log `mode berenang`.
6. Uji tembok/lorong untuk melihat local planner memilih arah tanpa spin agresif.
7. Bunuh bot dan cek packet respawn.
8. Putuskan server/jaringan sebentar dan cek reconnect backoff.
9. Uji `MC_TRANSPORT=auto`, lalu jika perlu bandingkan dengan `raknet` dan `nethernet`.

## Catatan penting

- Patch ini sudah lolos syntax check.
- Packet `player_auth_input` untuk movement/jump dan swimming sudah diuji melalui serializer 1.26.51 milik `bedrock-protocol 3.60.1`.
- Packet respawn state `ClientReadyToSpawn` juga sudah diuji melalui serializer yang sama.
- Belum dilakukan live test ke server Minecraft dari lingkungan patch ini, jadi log hasil test nyata tetap penting.
- Decoder chunk masih memakai compatibility layer `prismarine-chunk` 1.21 karena package tersebut belum menyediakan decoder khusus 1.26. Jika ada masalah terrain/chunk tertentu, bagian ini kemungkinan titik berikutnya yang perlu dibenahi.
- “Pathfinding” saat ini adalah local steering planner untuk wandering, bukan A* global ke koordinat tujuan. Untuk bot AFK wander, ini lebih relevan dan jauh lebih natural daripada random ±75°/±120° turn.

## v1.2 movement-ready fix

Jika v1.1 berhasil login tetapi pemain hanya diam, v1.2 menambahkan client-ready handshake Bedrock modern saat `available_commands` / `player_spawn`:

1. `serverbound_loading_screen` type 1
2. `serverbound_loading_screen` type 2
3. `interact` / `mouse_over_entity` ke runtime id 0
4. `set_local_player_as_initialized`

Juga diperbaiki pembacaan yaw awal dari `start_game.rotation.z` (Bedrock `vec2f` memakai `{x,z}`), initial `ReadyToSpawn` tidak lagi dianggap sebagai kematian, dan ditambah log diagnostik packet movement pertama serta correction dari server.


## v1.3 eye-position / server-correction fix

v1.2 membuktikan server sudah menerima `PlayerAuthInput`, tetapi live test menunjukkan bot tetap diam dan server mengirim `CorrectPlayerMovePrediction` berulang. v1.3 memperbaiki lapisan physics yang masih keliru:

- `PlayerAuthInput.position` diperlakukan sebagai **eye/movement position**, bukan feet position.
- Probe block/collision mengubah eye position ke feet position dengan eye height Bedrock `1.6200100183486938`.
- Posisi grounded dari `MovePlayer` / correction di-snap secara aman ke `integer feet Y + eye height` untuk menghilangkan noise float.
- `delta` tetap merepresentasikan velocity **setelah** physics tick, bukan sekadar selisih posisi.
- Menambahkan state flags vanilla-ish: `block_breaking_delay_enabled`, `vertical_collision`, `horizontal_collision`, `received_server_data`, dan one-shot `handled_teleport`.
- `play_mode` diubah ke `screen` seperti client modern yang diamati.
- Movement tick dimulai dari 1 dan hanya dikirim setelah ada posisi authoritative dari `MovePlayer`, `Respawn`, atau correction.
- `CorrectPlayerMovePrediction` sekarang me-reset velocity prediksi lokal, meng-ack `received_server_data`, dan tidak menimpa yaw/pitch player.
- Initial respawn sequence tidak lagi salah dilog sebagai kematian; posisi initial respawn dapat dipakai sebagai movement bootstrap.
- Jump raw press/release state ditambahkan (`jump_pressed_raw`, `jump_released_raw`).

### Indikator live test v1.3

Pada tanah datar, packet pertama seharusnya memiliki Y sekitar posisi correction server (contoh live test: `68.620010...`), bukan langsung turun menjadi `68.541610...`. `inputData` saat grounded biasanya juga memuat `block_breaking_delay_enabled` dan `vertical_collision`.

Correction sesekali masih normal untuk server-authoritative movement; yang tidak normal adalah correction terus-menerus yang mengunci X/Z pada koordinat yang sama.


## v1.4 swimming / drowning fix

Live test v1.3 membuktikan movement core sudah bekerja: server correction menunjukkan bot berpindah puluhan blok. Namun bot kemudian mati karena `death.attack.drown` tanpa pernah mencetak log `Masuk air`. Akar masalahnya ada di deteksi liquid/world mirror.

Perbaikan v1.4:

- World reader sekarang mempertahankan informasi **superimposed/liquid layer** dari Bedrock chunk (`block.superimposed` dan `computedStates.waterlogged`). Ini penting untuk kelp, seagrass, dan waterlogged block yang sebelumnya hanya terlihat sebagai nama primary block.
- Collision tidak lagi menganggap semua non-air/non-water sebagai kubus solid; `boundingBox`/`shapes` dipakai agar tanaman air seperti kelp/seagrass tidak dianggap tembok.
- Water detection sekarang memakai `world.isWater(x,y,z)`, termasuk waterlogged/superimposed water, bukan hanya membandingkan nama block primary.
- Ditambahkan fallback authoritative dari packet `set_entity_data`: metadata player `air` dan `max_airdata_max_air` dipantau. Jika napas mulai berkurang, bot langsung memaksa mode berenang/surface walaupun decoder chunk gagal mengenali air.
- Saat kepala terdeteksi di bawah air, bot mengirim intent naik (`want_up`, `ascend`, `auto_jumping_in_water`) beserta jump raw flags dan memberi upward velocity yang dibatasi agar segera mencapai permukaan.
- Mode swimming dilatch sampai bot benar-benar kembali mendapat support/keluar air, supaya satu tick decoder miss di permukaan tidak membuat bot langsung tenggelam lagi.
- Ditambahkan log diagnostik `🫧 Napas x/y` dan `🌊 Probe air` yang menampilkan primary block, superimposed block, dan status waterlogged.

Jika v1.4 masih gagal di air, kirim bagian log mulai dari `🏊 Masuk air`, `🌊 Probe air`, atau `🫧 Napas ...` sampai correction server berikutnya. Log ini akan menunjukkan apakah masalah tersisa ada pada world decoder atau vertical swimming physics.


## v1.5 collision-aware recovery + diagnostic mode

Live test v1.4 menunjukkan bot sudah dapat berjalan jauh secara nyata, tetapi log masih terlalu sering memicu `Stuck -> JUMP`. v1.5 memisahkan antara **movement yang benar-benar macet** dan jeda/correction server biasa.

Perubahan utama:

- Obstacle scanner dan local planner sekarang memakai `world.isSolid(x,y,z)` yang mempertimbangkan collision shape, bukan lagi aturan lama “semua blok non-air/non-liquid = solid”. Rumput, bunga, kelp, seagrass, dan blok non-collision tidak lagi dianggap tembok.
- Vertical jump/fall collision juga memakai collision-aware `world.isSolid`, sehingga tanaman/non-full block tidak salah menghentikan gerakan vertikal.
- Stuck detector menggunakan posisi **authoritative server** jika masih fresh, horizontal server velocity, dan beberapa sampel berturut-turut. Sampel lambat pertama hanya dicatat; bot tidak langsung melompat.
- Recovery bertahap: observe -> replan -> strafe+replan -> hard replan. Jump hanya dipakai jika obstacle rendah yang memang punya collision terkonfirmasi.
- `received_server_data` sekarang one-shot: dikirim pada `PlayerAuthInput` berikutnya setelah correction lalu langsung di-clear.
- Local planner diberi continuity bonus agar tidak berganti arah terlalu agresif, plus penalti ringan untuk memilih air saat ada jalur darat yang setara.
- Diagnostic mode aktif default selama tahap pematangan. Setiap 10 detik akan muncul `🔬 DIAG [periodic]` berisi:
  - posisi prediksi + posisi authoritative server;
  - yaw/pitch, mode AI, jump/swim/recovery state;
  - local velocity dan delta server;
  - status air/napas;
  - block di kaki/badan/kepala serta probe 0.5/1/1.5/2 blok di depan;
  - entity terdekat;
  - empat kandidat arah planner terbaik;
  - rate movement packet dan correction server;
  - average/max prediction correction error;
  - jumlah chunk/world state.
- Snapshot tambahan otomatis dibuat ketika kandidat stuck terdeteksi, correction server besar terjadi, bot masuk/keluar air, atau bot mati.

Diagnostic mode bisa dimatikan setelah bot matang dengan mengubah `debug.diagnostics` menjadi `false` di `config.js`.

Untuk laporan bug berikutnya, bagian paling berguna adalah log mulai dari `🔬 DIAG [...]` sebelum gejala sampai snapshot sesudahnya. Tidak perlu mengaktifkan `DEBUG=*` kecuali diminta, karena output tersebut terlalu berisik.


## v1.6 live-test fixes

- Auto-respawn now follows the Bedrock client sequence: send `respawn` state `ClientReadyToSpawn`, wait for server `ReadyToSpawn`, then send `player_action: respawn`.
- Respawn has retries and a reconnect fallback instead of hanging forever on the death screen.
- Tracks health changes, raw death-info/messages, nearest hostile at damage time, and death chat parameters.
- Hostile mobs trigger a flee mode before they get into melee range.
- Swimming no longer immediately enters full emergency-rise on the first lost air point; normal surfacing is gentler and emergency surfacing is staged.
- Swimming holds a heading and avoids idle/random-turn churn while trying to reach land.
- Correction diagnostics compare the server correction against the prediction saved for the same client tick when available.
- Planner cliff detection is disabled when the compatibility chunk decoder cannot even prove the block directly under the bot, avoiding the old “every direction has 0.5 clearance” failure mode.


## v1.7 authoritative rebase + server-collision memory

Live test v1.6 menemukan pola baru: server dapat menahan posisi X/Z tepat pada collision boundary sementara local prediction terus berjalan ~0.8 blok dan compatibility world decoder tetap melaporkan `air`. Akibatnya planner merasa jalan lurus clear walaupun server menolak gerakan.

Perubahan v1.7:

- Menambahkan **authoritative rebase mode**. Bila correction besar berulang pada posisi server yang hampir tidak bergerak, local prediction di-snap kembali ke authoritative position dan drift lokal dibatasi (`maxPredictionDrift`).
- Stuck detector juga mengaktifkan rebase mulai sampel macet kedua, sehingga obstacle scan/recovery tidak lagi dilakukan dari posisi prediksi yang sudah menembus collision secara imajiner.
- Menambahkan **server-collision memory**: heading yang berulang kali ditolak server disimpan sementara dan diberi penalti besar oleh local planner. Ini menjadi fallback saat decoder chunk 1.21 compatibility layer tidak melihat collision 1.26.x dengan benar.
- Collision/path probe sekarang memakai footprint player (~0.6 block width), bukan hanya satu ray di tengah. Corner/edge collision lebih kecil kemungkinan lolos dari scanner.
- Diagnostic snapshot sekarang menampilkan `authoritativeRebase`, `serverBlockedYaw`, sisa lock, alasan lock, `correctionRejectStreak`, dan `serverBlockedPenalty` per kandidat planner.
- Recovery sequence tetap bertahap; jump hanya dipakai saat low obstacle benar-benar terdeteksi. Server-rejected heading lebih dulu dihindari dengan replan/strafe.

### Indikator yang perlu dilihat pada live test v1.7

Saat gejala yang dulu disebut “I/O stuck” muncul, log yang sehat sekarang seharusnya menampilkan urutan seperti:

```text
🧱 Server menolak movement berulang ...
🧲 Authoritative rebase (...) ...
🔬 DIAG [server-movement-rejected] ...
🧭 Planner: ±45°/±60° ...
```

Setelah heading berganti, `authoritativeDistanceTotal` seharusnya mulai bertambah lagi. `correctionError` juga diharapkan turun karena local prediction tidak lagi dibiarkan drift hampir satu block dari posisi server.

Catatan: nama default bot pada project ini adalah `I/O`, jadi pesan manusia seperti “I/O stuck” kemungkinan berarti **player bernama I/O sedang nyangkut**, bukan error disk/stdio `EIO`.


## v1.7.1 persistent per-install instance identity

Beberapa pengguna dapat menjalankan bot offline yang sama secara bersamaan. Agar dua instalasi default tidak sama-sama join sebagai `I/O`, v1.7.1 membuat **instance ID random 8-hex-character sekali per instalasi** dan menyimpannya di `.bot-instance-id`.

Contoh dua instalasi independen:

```text
Laptop Amiya : I/O-7A2F91C4
Armbian      : I/O-B8130D6E
```

Semua reconnect dan restart pada instalasi yang sama tetap memakai ID tersebut. File `.bot-instance-id` tidak disertakan di ZIP dan di-ignore oleh Git, jadi ekstraksi baru pada mesin lain menghasilkan ID berbeda.

Override manual tersedia tanpa mengedit source:

```cmd
set "BOT_INSTANCE_ID=AMIYA"
npm start
```

PowerShell:

```powershell
$env:BOT_INSTANCE_ID = 'AMIYA'
npm start
```

Linux/Armbian:

```bash
BOT_INSTANCE_ID=NEXUS npm start
```

Dengan override tersebut username menjadi `I/O-AMIYA` atau `I/O-NEXUS`. `MC_USERNAME` tetap mengubah **base name**, lalu instance suffix tetap ditambahkan.

Untuk meregenerasi identity instalasi, matikan bot lalu hapus `.bot-instance-id` dan start lagi. Jika file ID tidak dapat ditulis karena permission/read-only filesystem, bot menggunakan ID ephemeral untuk proses itu dan mencetak warning.

Catatan: patch ini menghindari bentrok **username/instance bot offline antar instalasi**. Jika server masih mengembalikan `server_id_conflict` walaupun dua instance memiliki suffix berbeda, berarti collision berasal dari identifier/protocol layer lain dan log tersebut perlu dianalisis terpisah.


## v1.7.2: strict non-destructive safety guard

Versi ini menambahkan lapisan keselamatan outbound yang terpisah dari AI. Default-nya bot **tidak diizinkan** mengirim packet/action yang dapat merusak atau mengubah world maupun menyerang entity. Guard dipasang pada `client.queue()` dan `client.write()`, sehingga kode fitur baru yang lupa melakukan pengecekan tetap ditahan sebelum packet dikirim.

Yang diblokir antara lain:

- `inventory_transaction` (placement/use-on-block/entity attack path pada Bedrock modern),
- `item_stack_request`,
- destructive `player_action` seperti start/stop/continue/predict break dan creative destroy,
- `PlayerAuthInput` yang membawa `block_action`, `item_interact`, atau item-stack request,
- command/world-edit surfaces seperti `command_request`, `command_block_update`, `structure_block_update`, `block_entity_data`, dan `agent_action`.

Movement normal, swimming, handshake, correction acknowledgement, dan `player_action: respawn` tetap diperbolehkan. Jika guard menahan sesuatu, log akan berbentuk `🛑 SAFETY: ... diblokir`. Diagnostic snapshot juga menampilkan total outbound packet yang diblokir.

Guard default aktif. Menonaktifkannya sengaja membutuhkan environment variable eksplisit berikut:

```text
BOT_ALLOW_DESTRUCTIVE=I_UNDERSTAND_AND_ACCEPT_WORLD_CHANGES
```

Untuk penggunaan AFK normal, jangan set variable tersebut.

## v1.7.3: disconnect black-box diagnostics

Aternos server log menunjukkan bot dapat disconnect berulang lalu reconnect 4–5 detik kemudian, tetapi server hanya menulis `Player disconnected` tanpa alasan. v1.7.3 **tidak mengubah transport/reconnect policy dulu**; versi ini menambahkan recorder ringan agar penyebab disconnect berikutnya dapat dibedakan secara objektif.

Setiap koneksi merekam:

- transport yang diminta (`auto`/`raknet`/`nethernet`) dan transport final;
- umur sesi dan fase terakhir (`connect`, `join`, `spawn`);
- waktu sejak packet server terakhir dan heartbeat terakhir;
- ring buffer nama 24 packet inbound terakhir (payload besar tidak disimpan);
- kick reason terakhir;
- error terakhir beserta code/message;
- apakah disconnect dipanggil bot sendiri, termasuk `respawn-timeout` dan shutdown;
- health/death/respawn state;
- posisi predicted + authoritative, correction/stuck/recovery state;
- jumlah movement packet/correction dan status safety guard.

Saat koneksi tutup akan muncul:

```text
📦 DISCONNECT BLACK BOX { ... }
📝 Disconnect report disimpan: .../disconnect-blackbox.jsonl
```

`classification` yang mungkin muncul antara lain:

- `local-respawn-timeout` — bot sendiri memutus koneksi karena handshake respawn gagal;
- `local-shutdown` — proses memang sedang dihentikan;
- `server-kick` — server mengirim kick reason;
- `transport-or-protocol-error` — ada event error tepat sebelum close;
- `server-silence-or-timeout-suspected` — tidak ada packet server lebih lama daripada `pingTimeout`;
- `remote-close-or-transport-unknown` — close tanpa bukti cukup; packet history membantu investigasi berikutnya.

Report juga ditulis sebagai JSONL ke `disconnect-blackbox.jsonl` di folder project. Jika file melewati 1 MiB, versi lama diputar menjadi `disconnect-blackbox.jsonl.1`. File ini **bukan config/identity** dan aman dihapus setelah selesai diagnosis.

Override opsional:

```cmd
set "BOT_DISCONNECT_BLACKBOX=0"              rem matikan recorder
set "BOT_DISCONNECT_LOG=nama-file.jsonl"     rem ubah lokasi/nama log
set "BOT_DISCONNECT_PACKET_HISTORY=40"       rem ubah jumlah nama packet terakhir
```

Untuk laporan disconnect berikutnya, kirim blok `📦 DISCONNECT BLACK BOX` atau file `disconnect-blackbox.jsonl`. Tidak perlu mengaktifkan `DEBUG=*` terlebih dahulu.


## v1.7.11 respawn correction

- Reverts the experimental v1.7.10 PlayerAuthInput `block_action` respawn path. The target BDS advertises `server_authoritative_block_breaking=false`, and v1.7.10 received a `packet_violation_warning` immediately after that packet.
- Uses the working Mineflayer-Bedrock sequence: client `Respawn(state=2)` -> server `ReadyToSpawn(state=1)` -> standalone `PlayerAction(respawn)` with zero positions and `face=-1`.
- Keeps the protocol-2193 ordinal compatibility probe so the on-wire respawn action remains `6` even if the installed symbolic mapper is shifted.
- Does not wait indefinitely for a fourth explicit completion packet after the final PlayerAction. Authoritative movement/health/entity events can finish immediately; otherwise a short grace fallback accepts the ReadyToSpawn position and resumes locally.
- Logs full `packet_violation_warning` payloads and records them in the disconnect black box.
