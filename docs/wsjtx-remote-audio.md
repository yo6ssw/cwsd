# Remote WSJT-X over the LAN — IC-7300 on a headless host

How to run **WSJT-X (or any soundcard digimode app) on a workstation** while the
**IC-7300 is physically attached to a different, headless Linux host**. CAT/PTT goes
over cwsd's `rigctld`; **audio is bridged with PipeWire-Pulse over TCP** (RX and TX).

This is "Option B": WSJT-X stays on the workstation, and the rig host keeps running
cwsd for CAT/CW. cwsd's own (RX-only) audio server is turned **off** here, because the
USB CODEC capture device can only be opened once and PipeWire needs it.

> **LAN vs WAN.** Parts 1–5 use a PipeWire-Pulse tunnel, which is correct on a LAN. Over a
> **WAN/VPN** that tunnel's clock recovery breaks and decoded signals drift in frequency —
> carry RX audio with **ROC** instead (**Part 6**).

---

## Architecture

```
 WORKSTATION (desktop, PipeWire)                 RIG HOST  (headless, PipeWire + cwsd)
 ┌───────────────────────────┐                  ┌───────────────────────────────────┐
 │ WSJT-X                     │                  │ cwsd ─ rigctld  TCP 4532  ── CAT   │
 │  ├ Input  = rig_rx ────────┼── pulse TCP ─────┤ pipewire-pulse  TCP 4713           │
 │  ├ Output = rig_tx ────────┼── 4713 ──────────┤   ├ source: …analog-stereo (RX)    │
 │  └ Radio  = Hamlib NET ────┼── TCP 4532 ──────┤   └ sink:   …analog-stereo (TX)    │
 │ pipewire-pulse             │                  │ ALSA hw:0 = IC-7300 USB CODEC      │
 └───────────────────────────┘                  │ /dev/icom7300 = IC-7300 serial     │
                                                 └───────────────────────────────────┘
```

cwsd uses the rig's **serial** interface; PipeWire uses the rig's **USB-audio** interface.
They are different USB interfaces of the same device and coexist without conflict.

---

## Site values (this deployment — substitute your own)

| Thing | Value here |
|-------|-----------|
| Rig host | `brain.local` (LAN `192.168.3.41/24`), Ubuntu 24.04, headless |
| Workstation | `192.168.3.147/24`, desktop with PipeWire + WSJT-X |
| Operating user on rig host | `benny`, UID **1000** (so `XDG_RUNTIME_DIR=/run/user/1000`) |
| `audio` group GID | **29** |
| Rig ALSA card | `hw:0` → use **`plughw:0,0`** (see gotchas) |
| PipeWire device names | `alsa_input/alsa_output.usb-Burr-Brown_from_TI_USB_Audio_CODEC-00.analog-stereo` |
| Pulse TCP port | **4713** |
| cwsd rigctld port | **4532** |

> All SSH commands below assume key-based login to the rig host as the operating user.

---

## Part 1 — Rig host (server)

### 1.1 Free the capture device (disable cwsd audio)

cwsd's audio server opens `plughw:0,0` *directly*, and a raw ALSA capture device can
only be opened once — so it must not hold the card while PipeWire owns it.

```bash
# in ~/.config/cwsdrc, set the audio section's flag:
#   audio:
#     enabled: false
sudo systemctl restart cwsd.service
# confirm cwsd no longer binds UDP 7355 and the PCM is free:
sudo ss -lntup | grep cwsd                  # 7355 should be gone
sudo fuser -v /dev/snd/pcmC0D0c             # should report "not held"
```

> You don't have to give up cwsd's Opus stream permanently: once PipeWire owns the
> card you can point cwsd's capture *at PipeWire* and run **both** cwsd audio and
> WSJT-X at the same time. See **Part 5**. (For initial bring-up, keep cwsd audio off.)

### 1.2 Install PipeWire

```bash
sudo apt install pipewire pipewire-pulse pipewire-audio wireplumber pulseaudio-utils
```

### 1.3 Run PipeWire headless (linger) — and the critical group gotcha

PipeWire runs as the user's `systemd --user` services. On a headless box, enable
**linger** so they start at boot with no login session:

```bash
sudo loginctl enable-linger <user>
```

**The #1 gotcha:** the user's `systemd --user` manager only picks up group membership
at the time it (re)starts. If the operating user was added to the **`audio`** group
*after* their user manager was already running, PipeWire/WirePlumber cannot open
`/dev/snd/*` and you get an empty card list (only a "Dummy Output"), with logs like
`ALSA lib …Cannot get card index for 0` / `capture open failed: No such file or directory`.

```bash
# ensure the user is in the audio group:
sudo usermod -aG audio <user>
# then RESTART THE USER MANAGER so it re-reads groups (this does NOT drop your ssh):
sudo systemctl restart user@$(id -u <user>).service
# verify gid 29 (audio) is now present:
grep ^Groups: /proc/$(pgrep -u <user> -x systemd | head -1)/status
```

> Note: restarting `user@UID.service` SIGTERMs other services oddly tied to the user
> session. After doing it, **restart cwsd** and re-check CAT (`sudo systemctl restart cwsd`).

Start/enable the PipeWire user services (all `--user` commands over SSH need the
runtime/D-Bus env exported):

```bash
export XDG_RUNTIME_DIR=/run/user/$(id -u) \
       DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/$(id -u)/bus
systemctl --user enable --now pipewire.socket pipewire-pulse.socket wireplumber.service
# the rig should now appear as a real source+sink (not just Dummy Output):
pactl list short sources
pactl list short sinks
```

You should see `alsa_input.…analog-stereo` (RX) and `alsa_output.…analog-stereo` (TX).

### 1.4 Expose the Pulse server over TCP

Drop-in `~/.config/pipewire/pipewire-pulse.conf.d/20-network.conf`:

```
pulse.properties = {
    server.address = [
        "unix:native"
        {   address = "tcp:0.0.0.0:4713"
            max-clients = 32
            listen-backlog = 32
            client.access = "unrestricted"   # no cookie auth — see SECURITY
        }
    ]
}
```

```bash
systemctl --user restart pipewire-pulse.service
ss -lnt | grep 4713                                   # should be LISTEN
sudo ufw allow 4713/tcp comment "PipeWire-Pulse rig audio"
```

> **SECURITY:** `unrestricted` + ufw `Anywhere` means *any host that can route to the
> rig host can capture/inject audio with no authentication*. Acceptable only on a
> trusted LAN. To tighten: scope ufw to your subnet
> (`sudo ufw allow from 192.168.3.0/24 to any port 4713 proto tcp`), or avoid exposure
> entirely with an SSH tunnel (see Alternatives).

---

## Part 2 — Workstation (client)

### 2.1 Tooling

```bash
sudo apt install pulseaudio-utils          # provides pactl (PipeWire-pulse compatible)
# sanity check you can reach the rig host's pulse server:
pactl -s tcp:brain.local:4713 info
pactl -s tcp:brain.local:4713 list short sources
```

### 2.2 Persistent tunnel devices

Drop-in `~/.config/pipewire/pipewire-pulse.conf.d/30-rig-tunnel.conf` — `pulse.cmd`
runs these `load-module` commands every time pipewire-pulse starts, creating local
devices **`rig_rx`** (source) and **`rig_tx`** (sink):

```
# latency_msec lowers the tunnel jitter buffer from the ~100 ms default (=50 here
# gives ~25 ms each side). Smaller = lower latency, more dropout risk on a jittery LAN.
pulse.cmd = [
    { cmd = "load-module" args = "module-tunnel-source server=tcp:brain.local:4713 source=alsa_input.usb-Burr-Brown_from_TI_USB_Audio_CODEC-00.analog-stereo source_name=rig_rx latency_msec=50" }
    { cmd = "load-module" args = "module-tunnel-sink   server=tcp:brain.local:4713 sink=alsa_output.usb-Burr-Brown_from_TI_USB_Audio_CODEC-00.analog-stereo sink_name=rig_tx latency_msec=50" }
]
```

```bash
systemctl --user restart pipewire-pulse.service
pactl list short sources | grep rig_rx     # present?
pactl list short sinks   | grep rig_tx     # present?
```

(For a quick, non-persistent test you can run the same two `pactl load-module …`
commands directly; `pactl unload-module module-tunnel-source` to remove.)

---

## Part 3 — WSJT-X configuration

Open **File → Settings** (F2).

### General tab
- Set your **Call sign** and **Grid Locator** (WSJT-X won't transmit without them).

### Radio tab — CAT/PTT over the network

| Field | Value |
|-------|-------|
| **Rig** | `Hamlib NET rigctl` (top of the list) |
| **CAT Control → Network Server** | `brain.local:4532` |
| **PTT Method** | `CAT` |
| **Mode** | `Data/Pkt` (recommended for IC-7300) — or `None` |
| **Split Operation** | `Fake It` (recommended for FT8) — or `None` |
| Poll Interval | default (1 s) |

The serial-port fields (Baud/Data bits/…) are ignored for NET rigctl — leave them.

Click **Test CAT** → button turns **green**. Click **Test PTT** → rig keys, click again to
unkey. *Only PTT-test with antenna/dummy load connected and power low.*

### Audio tab

WSJT-X lists devices by their PulseAudio **description**, so select:

- **Soundcard → Input:** `Tunnel to tcp:brain.local:4713/alsa_input…analog-stereo` (this is `rig_rx`)
- **Soundcard → Output:** `Tunnel to tcp:brain.local:4713/alsa_output…analog-stereo` (this is `rig_tx`)

These are the only "Tunnel to …brain.local…" entries. Channels `Mono` is fine (WSJT-X
uses the left channel). Click **OK**.

### Pin the audio buffer latency — or WSJT-X DT sits at +2 s

A network audio source handed to a generic client gets PipeWire's **pulse-compat default
buffer of ~2.0 s** (`node.latency = 88200/44100`), *not* a low-latency one — and WSJT-X
(Qt → PulseAudio) takes that default unless told otherwise. WSJT-X tolerates a **constant**
latency (it just appears as a DT offset) but the FT8 decode window is only ~±2.5 s, so a 2 s
buffer eats almost the whole margin and pushes already-offset stations out of the window.
What WSJT-X needs is latency that is **constant and bounded**, not necessarily small — NTP
fixes the *clock*, this pins the *channel buffer*.

Pin WSJT-X's buffer to a known value with `PULSE_LATENCY_MSEC` in its launch environment:

```bash
PULSE_LATENCY_MSEC=150 wsjtx          # node.latency drops 88200/44100 (2 s) -> 7200/48000 (150 ms)
```

To make it permanent on **both** launch paths:
- **App menu:** a user override `~/.local/share/applications/wsjtx.desktop` (shadows the deb's
  `/usr/share/applications` entry, survives package upgrades) with
  `Exec=env PULSE_LATENCY_MSEC=150 wsjtx`.
- **Terminal:** an alias — `alias wsjtx='PULSE_LATENCY_MSEC=150 command wsjtx'` in `~/.zshrc`.

150 ms matches the ROC channel's fixed buffer (`roc.latency-tuner.profile=intact`,
`sess.latency.msec=150`), so the total RX path is a constant ~300 ms → DT shifts a harmless
+0.3 s and **holds** (verify on a steady carrier/beacon: DT should not walk over minutes). Do
**not** chase lower latency with ROC's `gradual`/`responsive` tuner — that rate-matches but
wobbles the pitch, which hurts decoding more than a slightly looser, *stable* latency. For
WSJT-X, frequency stability > latency tightness.

### Rig front-panel settings (IC-7300, for FT8/digital)
- Mode **USB-D** (data) on the FT8 frequency (WSJT-X also sets it if Mode = `Data/Pkt`).
- Menu **Set → Connectors**: `DATA OFF/DATA MOD = USB`, `USB MOD Level` ~30–50%, set
  `USB SEND`/keying as you use them — so TX audio arrives via the USB CODEC that `rig_tx` feeds.

### Levels & clock
- **RX level:** aim the bottom green meter at ~30 dB with no signals. Adjust the rig's USB
  output gain: `pactl -s tcp:brain.local:4713 set-source-volume alsa_input.usb-Burr-Brown_from_TI_USB_Audio_CODEC-00.analog-stereo 80%`.
- **TX drive:** start with the **Pwr** slider low; raise until full power with **ALC near
  zero** (clean drive, no ALC action). Leave `rig_tx` near 100% (`pactl set-sink-volume
  rig_tx …`) and control drive with the rig's USB MOD level + the Pwr slider.
- **Clock:** decoding needs accurate time — `timedatectl` should show *"System clock
  synchronized: yes"*. Keep both machines NTP-synced. (This fixes WSJT-X **DT**, the time
  offset — it has nothing to do with a **frequency** shift; see below.)

### Sample rate — pin both ends to 48000

A **frequency shift** on decoded signals is *not* a network-quality symptom (jitter/loss
show up as dropouts and failed decodes, not a clean pitch offset, and bigger buffers do
not fix it). It comes from the **audio sample clock**: WSJT-X on the workstation reads a
different clock than the rig's USB CODEC, and the tunnel resamples between them. If either
PipeWire graph rate-switches, the resampler can land on a wrong ratio and every audio
frequency is scaled — a pitch shift in the waterfall.

Pin **both** machines to one fixed rate so the graph never switches. Drop-in
`~/.config/pipewire/pipewire.conf.d/10-rate.conf` (identical on workstation **and** rig host):

```
context.properties = {
    default.clock.rate          = 48000
    default.clock.allowed-rates = [ 48000 ]
}
```

```bash
# apply on each machine (rig host first, then workstation so the tunnel reconnects):
systemctl --user restart pipewire.service pipewire-pulse.service wireplumber.service
pw-metadata -n settings | grep -E "clock.rate|allowed"   # both → 48000 / [ 48000 ]
```

This removes rate-switch ratio errors. It does **not** remove the few-ppm crystal
difference between the two clocks — `module-tunnel-source` adaptively resamples to track
the rig host. If a **constant Hz offset** remains across the whole band, it is the rig's
reference oscillator (IC-7300 *Menu → Set → Function → REF Adjust*), not the audio path.

> **Over a LAN this is enough. Over a WAN/VPN it is not.** The Pulse tunnel's adaptive
> resampler estimates the remote clock from packet arrival timing, and on a jittery,
> high-latency link that estimate **never locks** — it ramps continuously, so every audio
> frequency drifts (measured here: a 6000 Hz tone arrived at 6008 Hz and climbing,
> ~+1400 ppm and rising; a bigger `latency_msec` made it *worse*). Pinning the rate cannot
> fix this because the error is in dynamic clock *tracking*, not nominal rate. If you are
> remote over a VPN and see signals drifting in frequency, **replace the Pulse tunnel with
> ROC** — see *Part 6*.

---

## Part 4 — Verification

```bash
# RX: capture 3 s from the tunnel and confirm it is not silence
timeout 4 parecord -d rig_rx --channels=1 --format=s16le --rate=48000 --raw /tmp/rx.raw
python3 - <<'PY'
import struct;d=open('/tmp/rx.raw','rb').read();n=len(d)//2
s=struct.unpack('<%dh'%n,d[:n*2]) if n else []
print("samples",n,"peak",max(abs(x) for x in s) if s else "NO DATA")
PY

# CAT: read the rig frequency through cwsd's rigctld
printf 'f\n' | nc -w3 brain.local 4532          # prints e.g. 18077000.000000
```

- **TX** was not on-air verified during setup (RF safety). Test with a low-power
  transmission once configured. Adjust levels with
  `pactl -s tcp:brain.local:4713 set-source-volume <rig source> <pct>` (rig RX gain into
  the decoder) and the WSJT-X **Pwr** slider / `set-sink-volume rig_tx` (TX drive).
- Decoding needs an accurate **system clock** on both machines — keep them NTP-synced.

---

## Troubleshooting

| Symptom | Cause / fix |
|---------|-------------|
| Only "Dummy Output", no rig card | User manager lacks `audio` group → `usermod -aG audio` then `systemctl restart user@UID.service`. Verify with `grep ^Groups: /proc/$(pgrep -u <user> -x systemd)/status` (need gid 29). |
| `Cannot get card index for 0` / `capture open failed` | Use `plughw:0,0`, not `hw:0`. Also a symptom of the missing `audio` group. |
| `pactl` over ssh: `Connection refused` | Export `XDG_RUNTIME_DIR=/run/user/UID` and `DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/UID/bus`. |
| Remote audio lags ~2 s / WSJT-X DT stuck near +2 s | Not the network — the client took PipeWire's pulse-compat **default ~2 s buffer** (`node.latency = 88200/44100`). Pin it: launch with `PULSE_LATENCY_MSEC=150` (desktop override + shell alias). See *Pin the audio buffer latency*. Check a reader's buffer with `pactl list source-outputs \| grep node.latency`. |
| PipeWire daemon won't start after editing `context.objects` | A bad node spec is fatal to the whole daemon; `journalctl --user -u pipewire`, fix/remove the drop-in, `systemctl --user reset-failed pipewire`. |
| `rig_rx`/`rig_tx` missing after rig-host reboot | Tunnels load only if the rig host's pulse server is reachable at the time; `systemctl --user restart pipewire-pulse` on the workstation to retry. |
| CAT dead after touching the user manager | `sudo systemctl restart user@UID.service` can stop cwsd; `sudo systemctl restart cwsd`. |
| WSJT-X: *"emulated split mode requires rig to be in simplex mode"* | "Fake It" split needs the rig reporting **simplex**. First make sure SPLIT is **off** on the IC-7300. cwsd's rigctld must also report it: older cwsd emitted a bogus split-ON (`s` → `1\nUNKNOWN`) because it didn't check `rig_get_split_vfo()`'s return; now fixed to default simplex and report `0\nVFOA`. Verify: a socket query of `s` to `:4532` returns `0\n<vfo>`. |
| CAT dead after a rig-host **OS or hamlib upgrade** (WSJT-X connects then times out every ~10 s) | cwsd links `libhamlib` dynamically; hamlib breaks ABI within 4.x while keeping the `.so.4` soname, so the old binary still runs (audio even works) but rigctld silently returns nothing. **Rebuild cwsd** against the new hamlib (`cmake .. && make && sudo make install`, restart). Verify with a socket query to `:4532` returning the dial freq. See CLAUDE.md → *Build / run*. |
| Decoded signals at a shifted/drifting frequency | **Not** the network — it's the sample clock. First pin both ends to 48000 (see *Sample rate — pin both ends*). A constant whole-band Hz offset that survives pinning = rig REF Adjust, not the tunnel. Buffers (`latency_msec`) cure dropouts, **not** pitch. Over a **WAN/VPN** the Pulse tunnel's clock tracking ramps without bound — pinning can't fix it; switch RX to **ROC** (*Part 6*). |
| ROC: `no codec available for fec scheme 'rs8m'` (daemon exits 234) | The packaged `libroc` was built without FEC. Set `fec.code = disable` on both ends (plain RTP, source port only). |
| ROC source loads but isn't recordable (shows `Stream/Output/Audio`) | Add `media.class = Audio/Source` to the module's `source.props`. |
| ROC: `failed to select packet_encoding matching frame_encoding` (daemon exits 234) | libroc 0.3 (Ubuntu 24.04) only supports its built-in 44100 PCM encoding — do **not** set `audio.rate` on the ROC sink/source; let it run at 44100 (PipeWire resamples to 48000 locally, exact ratio). |
| ROC: no audio at all on `rig_rx_roc` after a network change | ROC pushes to a **fixed receiver IP**. If the workstation's address changes (VPN drops → LAN, or a new DHCP/VPN lease), the rig host's RX `remote.ip` is stale and packets go nowhere. Verify on the rig host (`tcpdump -ni any 'udp and dst port 10001'` shows it sending) and on the workstation (`tcpdump` shows **nothing** arriving). Fix: set the rig host's `remote.ip` to the workstation's current address and restart PipeWire. |
| ROC: RX works but **TX is silent** (`rig_tx_in` peak 0 on rig host) | The rig host's **ufw rule for the TX port (10005) allows only the workstation's old source IP**. TX packets leave the workstation (`tcpdump … dst port 10005` confirms) but ufw drops them. Fix: `sudo ufw allow proto udp from <workstation current IP> to any port 10005`. Both ROC directions break on a workstation IP change — RX via stale `remote.ip`, TX via the stale ufw source rule. |
| ROC: frequency *wobbles* ±1000 ppm or worse | Default `gradual` latency-tuner over a jittery WAN over-corrects; set `roc.latency-tuner.profile = intact` on the **receiver** to disable clock-rate resampling (stable pitch, rare buffer-drift glitch). Do **not** enable the RTCP `control.port` between mismatched libroc versions (0.3↔0.4) — it makes tracking *worse*. |

---

## Teardown / revert

```bash
# workstation
rm ~/.config/pipewire/pipewire-pulse.conf.d/30-rig-tunnel.conf
systemctl --user restart pipewire-pulse.service

# rig host
rm ~/.config/pipewire/pipewire-pulse.conf.d/20-network.conf
sudo ufw delete allow 4713/tcp
# re-enable cwsd audio if desired: set audio.enabled: true in ~/.config/cwsdrc
sudo systemctl restart cwsd.service
```

---

## Part 5 — Run cwsd audio streaming AND WSJT-X at the same time

A PipeWire **source fans out to many readers**, so once PipeWire owns the card you can
have cwsd's Opus stream and WSJT-X's network tunnel both read the rig RX concurrently.
The trick: make cwsd capture from PipeWire (via the ALSA→PipeWire plugin) instead of
opening the hardware directly.

Requires `pipewire-alsa` (provides `/usr/share/alsa/alsa.conf.d/50-pipewire.conf`, which
defines the `pipewire` ALSA PCM, and the `libasound_module_pcm_pipewire.so` plugin).
It is pulled in by `pipewire-audio`; confirm with `apt policy pipewire-alsa`.

**1) Point cwsd's audio at PipeWire** — in `~/.config/cwsdrc`:

```yaml
audio:
  enabled: true
  device: pipewire        # ALSA→PipeWire plugin; records the default PipeWire source
  port: 7355
  channels: 1
  sample_rate: 48000
  # …rest unchanged…
```

**2) Give the cwsd unit access to the user's PipeWire** — cwsd is a *system* service, so
it needs the operating user's runtime dir to find the PipeWire socket. Add to
`/etc/systemd/system/cwsd.service` under `[Service]` (cwsd already runs as that user):

```ini
Environment=XDG_RUNTIME_DIR=/run/user/1000
Environment=PIPEWIRE_NODE=alsa_input.usb-Burr-Brown_from_TI_USB_Audio_CODEC-00.analog-stereo
```

`PIPEWIRE_NODE` targets the rig source explicitly; if the rig is already the default
PipeWire source you can omit it. Then:

```bash
sudo systemctl daemon-reload && sudo systemctl restart cwsd.service
```

**3) Verify both readers coexist.** cwsd should log `audio: capturing from pipewire`,
and on the rig host the rig source shows **two** consumers:

```bash
pactl list short source-outputs        # expect a "Tunnel for …" (WSJT-X) AND
                                        # an "alsa_capture.cwsd" (cwsd) on the same source
# from the workstation, both at once:
printf 'sub' | timeout 5 nc -u brain.local 7355 | wc -c          # cwsd Opus bytes > 0
timeout 5 parecord -d rig_rx --raw /tmp/rx.raw && ls -l /tmp/rx.raw  # WSJT-X tunnel data
```

**Notes / caveats**
- cwsd's Opus stream now rides through PipeWire (a few ms more latency than raw ALSA —
  irrelevant for monitoring).
- cwsd is a system service and may start before the user's PipeWire at boot; its audio
  worker retries the open, so it self-heals once PipeWire is up. (Ordering a system unit
  on a `--user` service is awkward; the retry is simpler and sufficient.)
- Keep `audio.enabled: false` only if you don't want the Opus stream. The TX/playback
  side is untouched — only WSJT-X uses the rig sink.

---

## Part 6 — Frequency-stable RX audio over a WAN/VPN with ROC

The PipeWire Pulse tunnel (Parts 1–2) is fine on a LAN but **fails over a WAN/VPN**: its
adaptive resampler can't lock onto the remote clock through internet jitter, so the audio
**drifts in frequency without bound** and WSJT-X decodes sit at the wrong, moving DF.
Pinning the rate (above) does not help — the error is in clock *tracking*. The fix is to
carry RX audio with **[ROC](https://roc-streaming.org/)** (`roc-toolkit`), which is built
for real-time audio over lossy/jittery networks and does proper clock-drift recovery.

This section replaces **both audio paths** (RX rig → WSJT-X, and TX WSJT-X → rig) with ROC.
CAT/PTT stays on rigctld. RX is the path that *needs* ROC (decode accuracy); TX benefits too
but has a latency caveat (see *6.5*).

### Measured, on this OpenVPN link (~55 ms RTT)

A known **6000.000 Hz** tone, generated on the rig host and measured on the workstation:

| Transport | Result |
|-----------|--------|
| PipeWire Pulse tunnel | **+1400 ppm and climbing** (6008 Hz → worse over time); bigger `latency_msec` made it worse |
| ROC, default `gradual` tuner | flat for ~8 s then **±1000–5000 ppm** sustained excursions |
| **ROC, `intact` tuner** | **flat 0 ppm**, stable indefinitely (within-window drift 0.000 Hz) ✓ |

### 6.1 Prerequisites (both machines)

The ROC PipeWire modules and `libroc` ship with the distro audio stack:

```bash
ls /usr/lib/*/pipewire-0.3/libpipewire-module-roc-{sink,source}.so   # must exist
ldconfig -p | grep libroc                                            # libroc present
# if missing:  sudo apt install libroc0.4   (or libroc0.3 on Ubuntu 24.04)
```

> **Version skew is real.** Ubuntu 24.04 ships **libroc 0.3**; newer desktops have **0.4**.
> They interoperate over plain RTP, but (a) these builds have **no FEC** compiled, and
> (b) the **RTCP control channel is incompatible across 0.3↔0.4** — enabling it makes clock
> tracking *worse*. So the config below uses `fec.code = disable`, no control port, and the
> `intact` tuner. Matching libroc versions on both ends would let you re-enable the control
> port for smoother sync, but `intact` is what made it rock-solid here.
>
> **Changing rate or channel count needs PipeWire ≥1.6 on *both* ends — not just libroc 0.4.**
> The wire encoding is set by the PipeWire roc *module*, not the library. Modules older than
> 1.6 (e.g. Ubuntu 24.04's **1.0.x** and 25.10's **1.4.7**) import no
> `roc_context_register_encoding` and ignore `audio.position`, so they are locked to ROC's
> built-in **L16 @ 44100 stereo** — you cannot select **16 kHz** *or* **mono**. PipeWire ≥1.6
> (Ubuntu 26.04) registers a matching custom encoding and honors both. Both ends must agree,
> so the *oldest* roc module in the path sets the ceiling.
>
> **Arg vs prop gotcha (≥1.6):** `audio.position` is a top-level module **arg**, but
> `audio.rate` is read from the node **props** (`sink.props`/`source.props`) — putting
> `audio.rate` at the args level is silently ignored (node stays 44100). See the config below.

This deployment runs **16 kHz mono** (both ends Ubuntu 26.04 / PipeWire 1.6.2): a custom L16
16 kHz mono encoding, **~264 kbps** on the wire vs ~1.4 Mbps for stereo 44100 (≈5× less), and
8 kHz Nyquist easily covers FT8/CW/SSB.

ROC is a **sender → receiver push**, so each direction has its own sender, receiver, and
UDP port (FEC off → one RTP port per direction, no repair port):

| Direction | Sender → Receiver | Port | Receiver must accept from |
|-----------|-------------------|------|---------------------------|
| **RX** (rig audio → WSJT-X) | rig host → workstation (`10.8.0.6`) | **10001** | rig host `192.168.3.41` |
| **TX** (WSJT-X → rig) | workstation → rig host (`192.168.3.41`) | **10005** | workstation `10.8.0.6` |

```bash
# rig host -> workstation reachability, and (if the rig host runs ufw) open the TX port:
ping -c2 192.168.3.41                                                    # from workstation
sudo ufw allow proto udp from 10.8.0.6 to any port 10005 comment "ROC rig TX"   # on rig host
# workstation: if it runs a firewall, open the RX port from the rig host:
sudo ufw allow proto udp from 192.168.3.41 to any port 10001 comment "ROC rig RX"
```

### 6.2 Workstation drop-in

`~/.config/pipewire/pipewire.conf.d/40-roc-rig.conf` — a ROC **source** for RX (WSJT-X
Input) and a ROC **sink** for TX (WSJT-X Output):

```
context.modules = [
  # RX: receive rig audio  ->  WSJT-X Input = "ROC rig RX"
  { name = libpipewire-module-roc-source
    args = {
        local.ip                   = 0.0.0.0
        local.source.port          = 10001
        fec.code                   = disable
        sess.latency.msec          = 150
        roc.latency-tuner.profile  = intact
        audio.position             = [ MONO ]
        source.name                = rig_rx_roc
        source.props               = { node.description = "ROC rig RX" media.class = Audio/Source audio.rate = 16000 }
    }
  }
  # TX: send WSJT-X output to the rig host  ->  WSJT-X Output = "ROC rig TX"
  { name = libpipewire-module-roc-sink
    args = {
        remote.ip          = 192.168.3.41
        remote.source.port = 10005
        fec.code           = disable
        audio.position     = [ MONO ]
        sink.name          = rig_tx_roc
        sink.props         = { node.description = "ROC rig TX" audio.rate = 16000 }
    }
  }
]
```

(For built-in stereo 44100 — the only option on PipeWire <1.6 — drop the `audio.position`
and `audio.rate` lines.)

- `roc.latency-tuner.profile = intact` **disables clock-rate resampling** — that is what
  removes the pitch drift. The trade-off: with no rate-matching, the buffer is not refilled,
  so `sess.latency.msec` must be **deep enough to ride out network jitter** — that sets the
  latency floor, and the floor tracks the link. On this **WiFi LAN** (jitter mostly ~4 ms
  but bursting to ~50 ms) measured underruns over 25 s at 16 kHz mono were: **60 ms → 2.2%
  silence**, **100 ms → 0.3%** (a few sub-30 ms gaps), **150 ms → 0 gaps**, **200 ms → ~0**.
  `100` is about the floor; we run **`150`** for a little margin against jitter bursts
  (clean in testing). A busier/remoter link (or wired Ethernet) shifts the sweet spot.
  Going lower needs the `gradual`/`responsive` tuner, which rate-matches but **wobbles the
  pitch** — unacceptable for WSJT-X. So `intact` trades latency for frequency stability.
- `media.class = Audio/Source` is required or the node loads as a playback stream you
  can't record from.
- Do **not** set `audio.rate` — libroc 0.3 only supports its built-in 44100 PCM encoding;
  forcing 48000 crashes the daemon. The stream runs at 44100 and PipeWire resamples to
  WSJT-X's 48000 with an exact, clock-clean ratio.

### 6.3 Rig-host drop-in

`~/.config/pipewire/pipewire.conf.d/40-roc-rig.conf` — mirror of the workstation: a ROC
**sink** + loopback for RX (rig capture → network), and a ROC **source** + loopback for TX
(network → rig playback). A roc-source defaults to a *playback* stream, so each side that
must route to/from a specific device uses a `libpipewire-module-loopback`:

```
context.modules = [
  # RX: capture rig audio and send it to the workstation
  { name = libpipewire-module-roc-sink
    args = {
        remote.ip          = 10.8.0.6
        remote.source.port = 10001
        fec.code           = disable
        audio.position     = [ MONO ]
        sink.name          = rig_audio_send
        sink.props         = { node.description = "rig audio -> ROC" audio.rate = 16000 }
    }
  }
  { name = libpipewire-module-loopback
    args = {
        node.description = "rig RX -> ROC sink"
        capture.props  = { node.target = "alsa_input.usb-Burr-Brown_from_TI_USB_Audio_CODEC-00.analog-stereo" }
        playback.props = { node.target = "rig_audio_send" }
    }
  }
  # TX: receive workstation audio and play it into the rig sink
  { name = libpipewire-module-roc-source
    args = {
        local.ip                   = 0.0.0.0
        local.source.port          = 10005
        fec.code                   = disable
        sess.latency.msec          = 150
        roc.latency-tuner.profile  = intact
        audio.position             = [ MONO ]
        source.name                = rig_tx_in
        source.props               = { node.description = "ROC TX from workstation" media.class = Audio/Source audio.rate = 16000 }
    }
  }
  { name = libpipewire-module-loopback
    args = {
        node.description = "ROC TX -> rig sink"
        capture.props  = { node.target = "rig_tx_in" }
        playback.props = { node.target = "alsa_output.usb-Burr-Brown_from_TI_USB_Audio_CODEC-00.analog-stereo" }
    }
  }
]
```

(The loopbacks omit `stream.dont-remix` so they down/up-mix the rig's stereo 48 kHz to the
mono 16 kHz ROC nodes. For built-in stereo 44100, drop `audio.position`/`audio.rate` here too.)

### 6.4 Apply and verify

```bash
# both machines (receiver first so it is listening before the sender starts):
systemctl --user restart pipewire.service pipewire-pulse.service wireplumber.service

# rig host: the ROC sink is RUNNING (loopback feeding it from the rig source)
pactl list short sinks | grep rig_audio_send

# workstation: RX source carries real rig audio; TX sink exists for WSJT-X Output
pactl list short sources | grep rig_rx_roc
pactl list short sinks   | grep rig_tx_roc
timeout 4 parecord -d rig_rx_roc --channels=1 --format=s16le --rate=48000 --raw /tmp/r.raw
python3 -c "import numpy as np;d=np.fromfile('/tmp/r.raw','<i2');print('peak',int(abs(d).max()))"

# TX end-to-end: play a tone into rig_tx_roc here, capture rig_tx_in on the rig host
# (rig host) timeout 6 parecord -d rig_tx_in --raw /tmp/tx.raw &
paplay --device=rig_tx_roc /path/to/1kHz.wav        # rig host /tmp/tx.raw peak should be > 0
```

Then in **WSJT-X → Settings → Audio → Soundcard**:
- **Input** = **"ROC rig RX"** (`rig_rx_roc`) — a steady reference (beacon/WWV/carrier)
  should now hold a constant DF.
- **Output** = **"ROC rig TX"** (`rig_tx_roc`).

The Pulse `rig_rx`/`rig_tx` tunnels can be left loaded as a fallback or removed from
`30-rig-tunnel.conf`.

### 6.5 TX latency / PTT-tail caveat

PTT is keyed over CAT (rigctld), which is **instant**, but the TX audio crosses the ROC
buffer + network (~200 ms + RTT). So WSJT-X drops PTT the moment it *finishes* playing,
while the last fraction of a second of audio is still in flight — the tail of the
transmission can be clipped. FT8 usually tolerates this; if the far end reports your last
symbols missing, **lower the TX `sess.latency.msec`** (on the rig host's `rig_tx_in`
source — e.g. 150) at the cost of more underrun risk, or add a small PTT tail on the rig.
The clock skew on transmitted tones is a fixed sub-Hz offset (negligible).

### Teardown (revert to the Pulse tunnel)

```bash
rm ~/.config/pipewire/pipewire.conf.d/40-roc-rig.conf      # on BOTH machines
systemctl --user restart pipewire.service pipewire-pulse.service wireplumber.service
# then re-select the Pulse "Tunnel to …" input in WSJT-X
```

---

## Alternatives (not used here)

- **SSH tunnel instead of open TCP** — keep pulse on `127.0.0.1:4713`, no firewall
  change, reach it via `ssh -N -L 4713:localhost:4713 brain.local` and point the
  workstation tunnels at `tcp:localhost:4713`. Encrypted, authenticated by SSH keys.
- **USB/IP** — forward the rig's whole USB (serial + audio) to the workstation so
  everything is local; but this *detaches* the rig from the host, so cwsd loses it.
- **Run WSJT-X on the rig host, remote the display** (X11/VNC) — audio stays local,
  so there is no cross-clock resampling at all (the most robust fix for frequency
  stability), but it needs a GUI stack on the headless box.
