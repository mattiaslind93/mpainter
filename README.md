# mPainter

Ready-made Early Access Alpha builds are on [Gumroad](https://mattiaslind.gumroad.com/l/mpainter).

[![mPainter launch](docs/marketing/2026-11-06-mpainter-launch.jpg)](docs/marketing/2026-11-06-mpainter-launch.mp4)

[Watch the launch video](docs/marketing/2026-11-06-mpainter-launch.mp4)

mPainter is a GPL-licensed 3D texture painting application by Mattias Lind. It combines a Qt workspace with Blender rendering in one process.

Paint in 3D and UV views, organize layers and masks, work with texture sets and UDIMs, import textures and Blender materials, and save editable projects as `.blend` files. Material Presets and Mask Generators provide reusable authoring workflows. Imports of user-owned `.spmsk` files are a compatibility feature; unsupported source operations are reported by the importer.

The application starts with an empty material library. [Download free materials from Poly Haven](https://polyhaven.com/textures): its materials are CC0. Unpack downloads, then import or drop the image files into Assets. Nothing is downloaded automatically.

Build and packaging instructions are in [docs/building.md](docs/building.md). Linux is the locally tested platform. Windows CI is prepared but unverified. The macOS native viewport requires a Metal integration port; no working macOS release is claimed.

The supplied splash artwork and Mattias Lind’s m-mark icon establish mPainter's own identity. Source and asset attributions are in [NOTICE.md](NOTICE.md); see [LICENSE](LICENSE) for GPL-3.0.

Not affiliated with or endorsed by Adobe. Substance 3D is a trademark of Adobe Inc.
