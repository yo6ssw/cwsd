# Third-party components

cwsd itself is licensed under GPL-3.0-or-later (see [`LICENSE`](LICENSE)). It
bundles the following third-party libraries, which keep their own licenses and
are **not** reformatted or relicensed (see `src/libs/.clang-format`):

| Component | Location | License | Upstream |
|-----------|----------|---------|----------|
| easylogging++ | `src/libs/easylogging++.{h,cc}` | MIT | https://github.com/abumq/easyloggingpp |
| fkYAML | `src/libs/node.hpp` | MIT | https://github.com/fktn-k/fkYAML |

Both are permissively licensed (MIT) and GPL-compatible.

At runtime cwsd links, but does not bundle, system libraries: **hamlib**
(LGPL/GPL), **ALSA** (`libasound`, LGPL) and **Opus** (`libopus`, BSD). These
are provided by your distribution.
