# Release tools

Used when the emulator goes to the site and when a card goes into the disk catalog
(the records: hw/oodos.md and the site's deploy notes).

- `make_gwbasic_card.sh DEMO.img[.gz] GWDIR BASICDIR README_SECTION LABEL OUT.img.gz` builds a
  GW-BASIC card for the catalog: the demo card, GWBASIC.COM and .OVL, `basic/*.bas` (names cut to
  8.3; `SKIP=` names programs to leave off) and `gwbasic-readme-section.txt` in README.TXT. Needs
  Docker.
- `gwbasic-readme-section.txt` is the card README's GW-BASIC section, as on the 0.7f card.
- `romscan.py LIST ROOT` looks for any 64-byte run of a protected file in a site build before it
  is deployed; it prints the matches' names and counts, nothing from the files themselves.
