# Material Icons

Icons vendored from Google's [Material Icons](https://github.com/google/material-design-icons),
licensed under the Apache License 2.0 (see `LICENSE` in this directory).

Vendored files (all used as `d`/path data copied by hand into
`SelectionToolbar.qml`'s `ToolbarSvgIcon` instances — nothing is fetched at
build or runtime, these exist only as the vendored source/provenance record):

| File | Source path in upstream repo | Used for |
|---|---|---|
| `content_paste-24px.svg` | `src/content/content_paste/materialicons/24px.svg` | Paste button |
| `content_cut-24px.svg` | `src/content/content_cut/materialicons/24px.svg` | Cut button |
| `content_copy-24px.svg` | `src/content/content_copy/materialicons/24px.svg` | Copy button |
| `format_list_bulleted-24px.svg` | `src/editor/format_list_bulleted/materialicons/24px.svg` | Bullet List button |
| `format_list_numbered-24px.svg` | `src/editor/format_list_numbered/materialicons/24px.svg` | Numbered List button |
| `checklist-24px.svg` | `src/editor/checklist/materialicons/24px.svg` | Checkbox List button |
| `grid_on-24px.svg` | `src/image/grid_on/materialicons/24px.svg` | Insert Table button |
| `grid_off-24px.svg` | `src/image/grid_off/materialicons/24px.svg` | Delete Table button |
| `table_rows-24px.svg` | `src/editor/table_rows/materialicons/24px.svg` | Delete Row button base (plus a hand-added diagonal strike — see below) |
| `view_column-24px.svg` | `src/action/view_column/materialicons/24px.svg` | Delete Column button base (plus a hand-added diagonal strike) |
| `add_row_below-symbols-24px.svg` | `symbols/web/add_row_below/materialsymbolsoutlined/add_row_below_24px.svg` | Insert Row button |
| `add_column_right-symbols-24px.svg` | `symbols/web/add_column_right/materialsymbolsoutlined/add_column_right_24px.svg` | Insert Column button |

Notes:

- The last two files are from Material *Symbols* (the newer icon set, same
  upstream repo, same Apache 2.0 license), not classic Material Icons — their
  SVGs use a `viewBox="0 -960 960 960"` origin rather than `0 0 24 24`.
  `ToolbarSvgIcon`'s `viewBoxOriginY` property compensates for that so both
  families render through the same QML component.
- There is no dedicated Material "delete row" / "delete column" glyph in
  either icon set. Those two buttons instead reuse the plain `table_rows` /
  `view_column` glyphs above with an added diagonal stroke drawn by
  `ToolbarSvgIcon`'s `strikethrough` property — the same "diagonal slash
  means delete/off" convention Material's own `grid_off` (used for the
  Delete Table button) already uses, rather than hand-splicing a knockout
  badge into each base glyph's path data.
