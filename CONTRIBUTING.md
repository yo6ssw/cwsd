# Contributing to cwsd

Thanks for your interest — cwsd is built by and for radio amateurs, and
contributions (bug reports, fixes, new rig support, docs) are very welcome.

## Reporting bugs

Open a GitHub issue and include:

- your distribution and `cmake --version`,
- your hamlib version (`rigctld --version`) and rig model number,
- which service was involved (`rigctld`, `cwdaemon`, `audio`, `remote_key`),
- the relevant lines from the log (`logging.filename` in your `cwsdrc`).

> **Heads-up:** CAT can break *silently* after a hamlib upgrade because cwsd
> links `libhamlib` dynamically. If `rigctld` stops responding after an update,
> rebuild cwsd against the new hamlib before filing a bug — see `CLAUDE.md`.

## Building

See the [README](README.md) for dependencies and the build steps. In short:

```sh
sudo apt install libhamlib-dev libasound2-dev libopus-dev
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

Requires a C++20 compiler and CMake ≥ 3.25.

## Coding style

- Code is formatted with **clang-format, Google style** — the `.clang-format`
  at the repo root is authoritative. Run it on your changes before committing:
  ```sh
  clang-format -i <files you touched>
  ```
- **Do not reformat `src/libs/`** — those are vendored third-party libraries
  (easylogging++, fkYAML) under their own MIT license; a `DisableFormat`
  `.clang-format` there keeps them untouched.
- Keep pure-reformatting changes in their **own commit**, separate from
  behavioral changes, so `git blame` stays useful (see `.git-blame-ignore-revs`).

## Licensing of contributions

cwsd is **GPL-3.0-or-later**. By submitting a contribution you agree to license
it under those terms. Add an SPDX header to any new source file:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: <year> <your name / callsign>
```

Please sign off your commits (`git commit -s`) to certify the
[Developer Certificate of Origin](https://developercertificate.org/).

## Pull requests

- Branch off `master`; keep PRs focused.
- There is no unit-test framework; verify against real hardware where you can,
  and describe how you tested in the PR.
- CI builds every PR — make sure it's green.

Architecture notes for contributors live in [`CLAUDE.md`](CLAUDE.md).
