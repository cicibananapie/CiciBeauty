<!--
SPDX-FileCopyrightText: BANANAPIE LIMITED
SPDX-License-Identifier: MIT
See LICENSES/MIT.txt for the full license text.
-->

# Open-source license scope

Project-authored OBS plugin and loader code in `cicibeauty/` and
`cicibeautyStub/` is licensed under GPL-2.0-or-later. The corresponding
translations and OBS integration tests carry the same license. See
`LICENSES/GPL-2.0-or-later.txt` and individual file notices.

Project-authored `cicibase/`, `protocol/`, and build/test tools marked MIT are
licensed under MIT. See `LICENSES/MIT.txt` and `cicibase/LICENSE`.
MIT permits use in both open-source and proprietary software subject to its
copyright and notice requirements.

Third-party code retains its upstream copyright and license notices. See
`cicibase/THIRD_PARTY_NOTICES.md`, the notices in `3rd/include/`, and the licenses
provided with the Qt and OBS SDKs. A linked binary must satisfy the applicable
dependency terms. In particular, Qt under LGPLv3 is not compatible with GPLv2
alone; the GPL-2.0-or-later grant allows use under GPLv3 for that combination.

The independent binary runtime, installer and helper implementations, model and
effect packages, artwork, and trademarks have separate terms. This repository's
source licenses do not relicense those components. Final packaging combines the
public plugin with a separately supplied runtime archive.
