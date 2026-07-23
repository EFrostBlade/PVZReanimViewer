# PVZ Reanim Viewer

Windows viewer for PopCap Plants vs. Zombies reanimation files. It opens XML
animations and PC `.reanim.compiled` files without launching the game.

## Open an animation

- Drag a reanimation file onto `PVZCapture.exe`.
- Use `Files > Open` inside the viewer.
- Run `PVZCapture.exe <animation> [image-search-directory ...]`.

PNG resources are searched beside the animation first. Additional image
directories can be supplied on the command line or through the
`PVZ_REANIM_FALLBACK_DIR` environment variable.

## Export GIF or MP4

Open an animation, select its `anim_*` range in the layer list, then choose
`Files > Export...`. The exporter supports canvas size, playback FPS, scale,
frame range, a transparent GIF background, and a solid MP4 background.

The GitHub Actions artifact includes `ffmpeg.exe`; no separate installation is
required. Command-line export is also supported:

```powershell
PVZCapture.exe animation.reanim.compiled --export preview.gif
PVZCapture.exe animation.reanim.compiled --export preview.mp4 --width 512 --height 512 --fps 12 --scale 1.0 --opaque
```

Command-line export closes the viewer when encoding finishes and returns a
non-zero exit code on failure.
