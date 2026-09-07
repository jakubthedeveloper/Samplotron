# Interactive build guide

Open [index.html](index.html) in a browser, or serve this folder from the repository root:

```sh
python3 -m http.server 8000 --directory docs/tutorial
```

Visit `http://localhost:8000`. The guide is entirely in English and starts paused. Choose a step, press Play, scrub the timeline, or use the arrow keys. Play on the welcome screen immediately starts the device demonstration. Playback stops at the end of each step unless Auto-advance is enabled. Use Full screen to expand the presentation and controls; press Escape or Exit full screen to return. Expand “Step instructions and notes” for wiring details and download links.

The guide contains 21 steps, including firmware downloads, sample assignment, a panic key and optional MIDI input. The animations are silent. The black and yellow SVG presentation scales to the available viewport, including 4K displays.

## Hosting

Publish the **contents of `docs/tutorial/`** as a static site artifact in your GitHub Actions deployment. `index.html` must be at the artifact root. All local URLs are relative, so the same files work at a domain root or under a GitHub Pages project path. No build step, package installation, backend or external fonts are required. This change does not publish the site or configure a deployment workflow.

The public site contains only the player, presentation data and required assets. There are no editing, import, export, upload or persistence controls. The previous authoring workspace remains outside the repository.

## Maintenance

Presentation text and timing live in `content.js`; SVG scenes in `renderer.js`; playback in `player.js`; layout in `style.css`. These are source files for maintainers, with no in-browser editor. Deep links such as `index.html?scene=assign&t=56` open a paused point within a step.

OLED images in `screens.js` are native firmware framebuffer captures with example data, not live device output. Module drawings illustrate functional connections; confirm physical connectors and board revisions before wiring. See [credits and provenance](CREDITS.md).

From the repository root, check data, assets and playback:

```sh
python3 tools/tutorial/check.py
python3 tools/tutorial/check.py --browser /usr/bin/google-chrome
```

The browser check uses a temporary HTML harness, deterministic animation timing and a 1366 × 640 window to check that the animation and controls fit on a laptop screen below the browser and system bars. It does not modify the published files.
