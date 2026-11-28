# Third-party notices for `demo/markdown`

This demo links against the following third-party components. Versions are the
ones used for the Phase 3.0 validation on Arch Linux (2026-11-28).

## MD4C

- Version: 0.6.0 (Arch package `md4c 0.6.0-2`)
- License: MIT
- Upstream: https://github.com/mity/md4c
- Copyright: Martin Mitáš and contributors.
- Usage: Markdown (CommonMark/GFM) parsing; `md4c-html` HTML output.
- MIT license text is available in the upstream repository and in the
  distribution's license files.

## JKQTPlotter / JKQTMathText

- Version: 5.0.0git20251013-2 (Arch package `jkqtplotter`)
- License: LGPL-2.1-or-later
- Upstream: https://github.com/jkriege2/JKQtPlotter
- Usage: `libJKQTMathText6_RelWithDebInfo.so` and
  `libJKQTCommon6_RelWithDebInfo.so`, dynamically linked; only the math-text
  sublibrary is used.
- The complete license text is shipped by the distribution package as
  `/usr/share/licenses/jkqtplotter/LICENSE` (in the extracted prefix:
  `usr/share/licenses/jkqtplotter/LICENSE`).

## Qt

- Version: Qt 6.12.0 SDK for the demo.
- License: LGPLv3/GPLv3 (see the Qt distribution for details).
- Upstream: https://www.qt.io/
- Usage: Qt Widgets, QtGui, QtCore. `QTextDocument` renders the generated HTML
  and formula images.

## Sample image

`examples/assets/response.png` was created for this validation demo and is
distributed under the repository MIT license. It is loaded only through a
relative local path; the demo never loads remote images or executes HTML.
