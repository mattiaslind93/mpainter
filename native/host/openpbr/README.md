# OpenPBR reference data

Unmodified upstream files from ASWF OpenPBR **1.1.1**, revision
`f8d6d947dfae4c9b599965a86c22826ea7a8dbfb`:

- [reference/open_pbr_surface.mtlx](https://github.com/AcademySoftwareFoundation/OpenPBR/blob/f8d6d947dfae4c9b599965a86c22826ea7a8dbfb/reference/open_pbr_surface.mtlx), SHA-256 `c15674aaa82ef4bf0a27388b3b3a8f0f171982f53df6ebbceed679ce63699f51`
- [LICENSE](https://github.com/AcademySoftwareFoundation/OpenPBR/blob/f8d6d947dfae4c9b599965a86c22826ea7a8dbfb/LICENSE), SHA-256 `c71d239df91726fc519c6eb72d318ec65820627232b2f796219e87dcf35d0ab4`

Apache-2.0. Export embeds the reference node definition and graph, renaming only their top-level identifiers and updating the graph's nodedef reference to avoid collisions with a host library's older OpenPBR definition. Exported packages must include `OpenPBR-LICENSE.txt`, returned by `openpbr_materialx_license()`.

This graph is material transport. It does not make the viewport's Principled compatibility shader a conformant OpenPBR renderer.
