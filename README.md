# CWSD

CW sender daemon.

## Instructions:


```
mkdir build
cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j $(nproc)
sudo make install
```

## Configuration

By default _cwsd_ will read configuration from `~/.config/cwsdrc`. Here is a sample configuration:

```
rig:
  port: /dev/icom7300
  model: 3073
cwdaemon:
  enabled: true
  port: 6789
  initial_wpm: 40
rigctld:
  enabled: true
  port: 4532
logging:
  level: info
  filename: /tmp/cwsd.log
  max_size: 1048576
```

## Usage:

`cwsd` will simply run in foreground. Config will be loaded from `~/.config/cwsdrc`

`cwsd -d` will make it daemonize.

`cwsd --version` will print the current running version.

## Making a fixed symlink for the USB device the rig connects to

- `sudo cp 80-ic7300.rules /etc/udev/rules.d/`
- `sudo service udev restart`

After plugging in the Icom 7300 USB cable a `/dev/icom7300` symlink will be created pointing to the actual `/dev/ttyUSBx` device that the rig was allocated.

## Installing as a systemd service

- edit shared/cwsd.service and set the appropriate user/group
- `sudo cp cwsd.service /etc/systemd/system/`
- `sudo systemctl enable cwsd.service`
- `sudo systemctl daemon-reload`
- `sudo systemctl start cwsd.service`

## Authors

* YO6SSW - Adrian Scripcă <benishor@gmail.com>
* YO3GEK - Matei Conovici <mconovici@gmail.com>
