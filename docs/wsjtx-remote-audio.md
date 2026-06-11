# Remote WSJT-X over the LAN — IC-7300 on a headless host

How to run **WSJT-X (or any soundcard digimode app) on a workstation** while the
**IC-7300 is physically attached to a different, headless Linux host**. CAT/PTT goes
over cwsd's `rigctld`; **audio is bridged with PipeWire-Pulse over TCP** (RX and TX).

This is "Option B": WSJT-X stays on the workstation, and the rig host keeps running
cwsd for CAT/CW. cwsd's own (RX-only) audio server is turned **off** here, because the
USB CODEC capture device can only be opened once and PipeWire needs it.

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
  synchronized: yes"*. Keep both machines NTP-synced.

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
| PipeWire daemon won't start after editing `context.objects` | A bad node spec is fatal to the whole daemon; `journalctl --user -u pipewire`, fix/remove the drop-in, `systemctl --user reset-failed pipewire`. |
| `rig_rx`/`rig_tx` missing after rig-host reboot | Tunnels load only if the rig host's pulse server is reachable at the time; `systemctl --user restart pipewire-pulse` on the workstation to retry. |
| CAT dead after touching the user manager | `sudo systemctl restart user@UID.service` can stop cwsd; `sudo systemctl restart cwsd`. |

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

## Alternatives (not used here)

- **SSH tunnel instead of open TCP** — keep pulse on `127.0.0.1:4713`, no firewall
  change, reach it via `ssh -N -L 4713:localhost:4713 brain.local` and point the
  workstation tunnels at `tcp:localhost:4713`. Encrypted, authenticated by SSH keys.
- **USB/IP** — forward the rig's whole USB (serial + audio) to the workstation so
  everything is local; but this *detaches* the rig from the host, so cwsd loses it.
- **Run WSJT-X on the rig host, remote the display** (X11/VNC) — audio stays local,
  but needs a GUI stack on the headless box.
```
