# cicibase

MIT-licensed foundation shared by the public plugin and private runtime.
It provides logging, filesystem/process helpers, scheduling, synchronization,
IPC/shared memory, locale, JSON utilities, and basic image utilities.

The library does not depend on Qt, OBS, MRE, curl, or OpenSSL.
Call `initCiciBase()` to initialize its process environment.

Windows builds `cicibase` with the DLL CRT and `cicibase_mt` with the static CRT
from the same sources. macOS builds `cicibase` only. Both repositories use the
public `cmake/CiciBuild.cmake` setup before adding this directory.

See `LICENSE` and `THIRD_PARTY_NOTICES.md`. Dependencies retain their own notices.
