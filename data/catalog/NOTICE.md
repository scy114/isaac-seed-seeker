# Item catalog provenance

The offline name catalog is mechanically generated from two pinned data-table revisions on The Binding of Isaac 中文维基（灰机 Wiki）:

- [`Data:Item.tabx`, revision 169621](https://isaac.huijiwiki.com/wiki/Data%3AItem.tabx)
- [`Data:ItemKeywords.tabx`, revision 169298](https://isaac.huijiwiki.com/wiki/Data%3AItemKeywords.tabx)

Both data pages identify their table data as available under [Creative Commons CC0 1.0](https://creativecommons.org/publicdomain/zero/1.0/). The exact revision IDs, timestamps and MediaWiki SHA-1 values are recorded in `sources.lock.json` and copied into every downloaded snapshot and generated catalog.

`profile-j460.json` is a reduced, generated index of item type and Eden-pool availability from this project's local full-unlock `v1.9.7.17.J460` Profile. The original runtime snapshots remain excluded from Git; their SHA-256 values are retained in the reduced index and catalog.

No artwork, sprites, rendered article pages or source code from the Wiki is included. The pinned source snapshots retain the complete structured CC0 tables so the generated catalog remains reproducible.
