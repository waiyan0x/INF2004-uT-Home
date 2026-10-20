# Bundled fonts

`roboto-latin.woff2` and `roboto-greek.woff2` are subsets of **Roboto**, served
locally so the review interface keeps its typography on a machine with no
network access.

- Upstream: Google Fonts (`https://fonts.google.com/specimen/Roboto`), version 51
- Licence: Apache License 2.0
- Subsets: latin (`U+0000-00FF`, …) and greek (`U+0370-03FF`)

The greek subset is required: the product name "μT-Home" contains `U+03BC`.

Both files are variable fonts covering weights 100–900, so one file per subset
serves every weight the interface uses. To refresh them, request the Google
Fonts CSS with a modern browser user agent, take the `latin` and `greek`
`@font-face` sources, and replace these files. The `unicode-range` declarations
in `styles/fonts.css` must be kept in step.
