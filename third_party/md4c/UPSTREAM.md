# MD4C upstream provenance

Ask the Model vendors the MD4C parser as an unmodified third-party source dependency.

- Upstream repository: https://github.com/mity/md4c
- Upstream commit: `7fc1815a5eeba2af7d6120a76202bf59f3b6e6e4`
- Upstream branch at import time: `master`
- Imported files:
  - `src/md4c.c` -> `third_party/md4c/md4c.c`
  - `src/md4c.h` -> `third_party/md4c/md4c.h`
  - `LICENSE.md` -> `third_party/md4c/LICENSE.md`
- License: MIT
- Local modifications to vendored source: none

The vendored commit is pinned deliberately so Flatpak and offline builds do not depend on a system MD4C package or on mutable upstream state.

PRES-01 only adds the pinned source and Meson build wiring. Parser configuration and presentation behavior are introduced and qualified in later Presentation Layer gates.
