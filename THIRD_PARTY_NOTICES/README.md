# Third-Party Notices

This directory contains attribution and license documentation for third-party
data bundled with or fetched by suopNotes at build time.

## en_US-dictionary-README.txt

Upstream README for the en_US Hunspell dictionary (spelling word list + affix
rules) used as suopNotes' bundled fallback spell-checking dictionary when no
system dictionary is available. Source: https://github.com/LibreOffice/dictionaries
(commit 38d96a4d54ec3449cf7f28cddae1fce32e2b15a7, en/ directory). The actual
dictionary data files (en_US.aff, en_US.dic) are NOT committed to this repo —
they are fetched at CMake configure time (see CMakeLists.txt) and cached in
the build directory, to keep this repository's git history free of large
binary word-list blobs. Per this README, the dictionary+affix data itself is
under a mix of permissive licenses (SCOWL, BSD-style Ispell, WordNet,
public-domain sources) — NOT the GPLv2 that applies to the separate
Lightproof grammar-checker component also found in that upstream directory,
which suopNotes does not fetch or use.
