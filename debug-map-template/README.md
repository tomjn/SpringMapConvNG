# Mipmap debug map - archive template

`mapcompile -debugmips -dw 8 -dh 8 -o debugmap` produces `debugmap.smf` +
`debugmap.smt` (binary map files only). To make a loadable map, package them
into an archive (`.sd7`/`.sdd`) with this template:

```
debugmap.sdd/
  maps/debugmap.smf
  maps/debugmap.smt
  mapinfo.lua            <- from this template (edit `mapfile` to match)
  neutral_detail.png     <- from this template
```

`mapinfo.lua` neutralises the ground detail texture (solid RGB(128,128,128),
which adds nothing under the engine's signed-additive detail blend) and sets
white ground lighting, so the four per-mip colours show cleanly without grain.

Load in Recoil with default settings (texture streaming OFF). Zooming out, the
terrain should colour-morph red -> green -> blue -> yellow. If it stays one
colour at all zooms, texture streaming is enabled (only mip0 is uploaded).
