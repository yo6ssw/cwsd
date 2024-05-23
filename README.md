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

## Usage:

`cwsd` will simply run in foreground. Config will be loaded from `~/.config/cwsdrc`

`cwsd -d` will make it daemonize.

`cwsd --version` will print the current running version.


## Authors

* YO6SSW - Adrian Scripcă <benishor@gmail.com>
* YO3GEK - Matei Conovici <cmatei@gmail.com>