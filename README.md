# wlrsetroot

Wayland wallpaper utility using wlr-layer-shell. Sets tiled XBM patterns or solid colors as your desktop background.

## Usage

```
wlrsetroot [options]
```

## Options

| Option | Description |
|--------|-------------|
| `-bitmap <file>` | XBM file to tile as wallpaper |
| `-mod <x> <y>` | Plaid grid pattern with spacing x,y |
| `-gray`, `-grey` | Checkerboard pattern |
| `-solid <color>` | Solid color background |
| `-fg <color>` | Foreground color (hex: `#rrggbb`) |
| `-bg <color>` | Background color (hex: `#rrggbb`) |
| `-rv`, `-reverse` | Swap foreground and background |
| `-scale <n>` | Scale pattern by factor (0.1-32) |

Only one of `-bitmap`, `-mod`, `-gray`, or `-solid` may be specified.

## Examples

```sh
wlrsetroot -bitmap pattern.xbm -bg "#1a1a2e" -fg "#e94560"
wlrsetroot -gray -bg "#282a36" -fg "#44475a" -scale 2
wlrsetroot -mod 16 16 -bg "#000000" -fg "#333333"
wlrsetroot -solid "#282a36"
```

## Building

Requires: wayland-client, wayland-protocols, meson, ninja

```sh
meson setup build
ninja -C build
```

## License

GPL-3.0. See [LICENSE](LICENSE).
