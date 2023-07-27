# REX Subsystems

## PPUX (PPU-integrated rendering extensions)
PPUX allows WebAssembly to render custom graphics integrated with the SNES PPU and its concept of rendering.
```c
ppux_write_cmd(uint32_t *data, uint32_t size) -> int32_t;
ppux_write_vram(uint32_t addr, uint8_t *data, uint32_t size) -> int32_t;
ppux_write_cgram(uint32_t addr, uint8_t *data, uint32_t size) -> int32_t;
```
The interface is very simple; the bulk of the work is done via `ppux_write_cmd`. This function uploads a list of draw
commands to the PPUX engine to be rendered on start of the next frame.

### Extra VRAM and CGRAM
`ppux_write_vram` writes a chunk of 16bpp bitmap data into an extra VRAM buffer, 64 MiB in size (1024x larger than
PPU's 64 KiB). The `draw_vram_tile_4bpp` draw command addresses into this memory to read bitmap data.

`ppux_write_cgram` writes a chunk of 16bpp palette data into an extra CGRAM buffer, 512 KiB in size (1024x larger than
PPU's 512 bytes). The `draw_vram_tile_4bpp` draw command addresses into this memory to read palette data.

### Draw Commands
The command list is represented as a list of uint32_t (32-bit words). Each draw command starts with a single uint32_t
descriptor which describes the command opcode number to execute as well as the size of data in subsequent uint32_ts for
the command to consume (or ignore, if the command is unrecognized). The format is as follows, from most-significant bit
on the left to least-significant bit on the right:
```
  MSB                                             LSB
  1111 1111     1111 1111     0000 0000     0000 0000
[ fedc ba98 ] [ 7654 3210 ] [ fedc ba98 ] [ 7654 3210 ]
  1ooo oooo     ---- ----     ssss ssss     ssss ssss    o = opcode
                                                         s = size of command data in uint32_ts
```

As of this writing there are 3 opcodes:
```
0. end_drawing
1. draw_bitmap_15bpp
2. draw_vram_tile_4bpp
```

### 0: End drawing
This opcode ends the drawing and halts execution of subsequent draw commands, if any. Its size is ignored.

### 1. draw_bitmap_15bpp
This command takes 2 uint32_ts as parameters, described as follows:
```
  MSB                                             LSB
  1111 1111     1111 1111     0000 0000     0000 0000
[ fedc ba98 ] [ 7654 3210 ] [ fedc ba98 ] [ 7654 3210 ]
  ---- --yy     yyyy yyyy     ---- --xx     xxxx xxxx    x = x-coordinate (0..1023) of top-left
  -o-- slll     ---- ----     ---- --ww     wwww wwww    y = y-coordinate (0..1023) of top-left
                                                         w = width in pixels (1..1024)
                                                         l = PPU layer
                                                         s = main or sub screen; main=0, sub=1
                                                         o = per pixel overlay = 0, replace = 1
```
Subsequent uint32_ts are the pixel data to draw, effectively determined by the `size` of the command data. Draws
horizontal runs of 15bpp RGB555 pixels starting at `(x,y)`, wrapping at `width` until `size-2` pixels in total are
drawn.

### 2. draw_vram_tile_4bpp
This command takes 4 uint32_ts as parameters, described as follows:
```
  MSB                                             LSB
  1111 1111     1111 1111     0000 0000     0000 0000
[ fedc ba98 ] [ 7654 3210 ] [ fedc ba98 ] [ 7654 3210 ]
  ---- --yy     yyyy yyyy     ---- --xx     xxxx xxxx    x = x coordinate (0..1023)
  --pp slll     ---- --vf     hhhh hhhh     wwww wwww    y = y coordinate (0..1023)
  ---- ----     dddd dddd     dddd dddd     dddd dddd    d = bitmap data address in extra ram
  ---- ----     cccc cccc     cccc cccc     cccc cccc    c = cgram/palette address in extra ram (points to color 0 of palette)
                                                         w = width in pixels
                                                         h = height in pixels
                                                         f = horizontal flip
                                                         v = vertical flip
                                                         l = PPU layer
                                                         s = main or sub screen; main=0, sub=1
                                                         p = priority (0..3 for OBJ, 0..1 for BG)

```
This command draws a set of 4bpp 8x8 tiles from an extra pair of VRAM-like and CGRAM-like memories.

Bitmap data is consumed from PPUX VRAM which is independent of normal PPU VRAM.

Palette data is consumed from PPUX CGRAM which is independent of normal PPU CGRAM.

The bitmap data address can address an individual 8x8 tile or partway through an 8x8 tile's bitmap data if so desired.
The data format expected is the same as the PPU expects with VRAM bitmap data and 8x8 4bpp tiles.

The cgram data address points to color 0 of the palette data and expects 16-bit RGB555 color data. Up to 16 colors can
be used with 4bpp sprite rendering. Similar to PPU sprite rendering, color 0 of the palette is treated as transparent.

The PPU layers are defined as:
```c
enum layer {
    BG1 = 0,
    BG2,
    BG3,
    BG4,
    OBJ,
}
```

## IOVM

TBD.
