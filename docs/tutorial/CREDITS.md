# Credits and provenance

Samplotron: [jakubthedeveloper/Samplotron](https://github.com/jakubthedeveloper/Samplotron).

The OLED frames were captured from the Samplotron firmware's [display renderer](../../src/display_ssd1309.cpp) and [UI](../../src/ui.cpp), using a host capture harness and the U8g2 graphics library. Their native resolution is 128 × 64 pixels. `screens.js` embeds only the 39 frames used by this guide. Sample names, assignments and playback state are demonstration data; the frames are snapshots rather than a running firmware emulator.

The U8g2 copyright and license notice is included in [U8G2-LICENSE.txt](U8G2-LICENSE.txt).

Module SVG illustrations and the black steel device animation were prepared for this guide. The assembled component arrangement follows the supplied device reference photograph: OLED above two encoders, with a 4 × 4 keypad below. The drawings are functional illustrations and do not certify physical pin positions or exact board appearance. Raw reference photographs and authoring tools are not part of the published site.
