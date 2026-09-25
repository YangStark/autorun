# Source provenance and licenses

This branch extends the existing Autorun/Wine source tree. The Wine-derived
files remain under the Wine project's LGPL-2.1-or-later terms; the local
[Wine/Autorun notice](work/wa2-complete-nsp/LICENSE-Autorun) and
[LGPL text](work/wa2-complete-nsp/COPYING.LIB) are included for review.

The loader's homebrew startup/trampoline work retains the nx-hbloader ISC
[notice](work/wa2-complete-nsp/loader/LICENSE.md). The source comment in
`packaging/forwarder.c` identifies the relevant sphaira `src/owo.cpp`
(ISC, TotalJustice) forwarder lineage and its stated hacbrewpack/yati
background. Preserve upstream notices when reusing or redistributing those
parts. The new glue, scripts, and tests in this experiment should be read
alongside these component notices, not as a claim that one new license
replaces the upstream licenses.

No WA2 game content, executable, original icon, cryptographic key, save,
compiled runtime, or package is granted or distributed by this repository.
Provide those separately only when you have rights to use them.
