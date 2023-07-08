# Exported Host Functions

The following sections describe the set of functions exported to WebAssembly modules, described in a C++ syntax.

## Logging
```c
log_stdout(const char *text, uint32_t len) -> void;
log_stderr(const char *text, uint32_t len) -> void;
log_trace(uint32_t flags, const char *text, uint32_t len) -> void;
```

`log_stdout` and `log_stderr` both work identically and will write the text buffer of specified length to the respective
standard output or standard error files.

`log_trace` will write to standard output iff (trace mask & flags) != 0.

For debugging purposes, when a newline character is encountered in any of the above log functions, the current line will
be prepended with the number of microseconds elapsed since the ROM was loaded, measured from the time of the function
invocation.

## Emulation event subsystem
```c
event_wait_for(uint32_t timeout_usec, uint32_t *o_event) -> int32_t;
event_ack_last() -> void;
```

```c
event_register_break(uint32_t pc, uint32_t timeout_nsec) -> uint32_t;
event_unregister_break(uint32_t pc) -> void;
```

The main pair of functions for this subsystem is `event_wait_for` and `event_ack_last`.

`event_wait_for` blocks the WebAssembly thread until the emulation core fires an event. Once fired, the emulator thread
is blocked until `event_ack_last` is called by the WebAssembly thread. The value at `*o_event` is set to the event id
that was fired.

`event_ack_last` unblocks the emulator thread when the WebAssembly thread is done responding to the last fired event.

This pair of functions effectively creates a critical section where the emulator thread is blocked until either the
WebAssembly thread calls `event_ack_last` explicitly or a short timeout occurs. The timeout is dependent on the type of
event fired. Ideally, the sum of all timeouts for all event types should not exceed 16ms in the worst case.

`event_register_break` registers a break event to fire when the PC reaches the given 24-bit bus address but before the
instruction there is executed. The timeout parameter is how long the emulator will wait (in nanoseconds) for the
WebAssembly thread to acknowledge the event. This acts essentially like a timed debugger breakpoint, giving the
WebAssembly thread a short window of time to do critical work like reading/writing memory that may be sensitive to code
at or after the specific PC address. The return value is the event id output by `event_wait_for`.

`event_unregister_break` un-registers any existing break event at the given PC address.

A maximum of 16 break events are allotted for.

The standard set of events fired are described by the following enum:

```c
enum wasm_event_kind : uint32_t {
    ev_none = 0,

    ev_shutdown,           // 1
    ev_snes_nmi,           // 2
    ev_snes_irq,           // 3
    ev_ppu_frame_start,    // 4
    ev_ppu_frame_end,      // 5
    ev_ppu_frame_skip,     // 6
};
```

`ev_shutdown` is fired when the ROM is being closed or the emulator is shutting down. The emulator waits until this
event is acknowledged by WebAssembly to allow it sufficient time to shut down cleanly and release any resources.

`ev_snes_nmi` is fired immediately prior to when an NMI interrupt vector is jumped to; timeout is 1000us.

`ev_snes_irq` is fired immediately prior to when an IRQ interrupt vector is jumped to; timeout is 1000us.

`ev_ppu_frame_start` is fired immediately prior to when the PPU scanline is set to 0 and rendering of a frame begins;
timeout is 2000us.

`ev_ppu_frame_end` is fired immediately after PPU rendering is complete and before presenting the screen;
timeout is 0us.

`ev_ppu_frame_skip` is an alternative event to `ev_ppu_frame_start`, fired if the current frame is to be skipped;
timeout is 2000us.

## Emulation memory access subsystem
These functions provide read/write access to various emulated memories.
```c
mem_read_rom(uint8_t *dest, uint32_t dest_len, uint32_t offset) -> int32_t;
mem_read_sram(uint8_t *dest, uint32_t dest_len, uint32_t offset) -> int32_t;
mem_write_sram(uint8_t *src, uint32_t src_len, uint32_t offset) -> int32_t;
mem_read_wram(uint8_t *dest, uint32_t dest_len, uint32_t offset) -> int32_t;
mem_write_wram(uint8_t *src, uint32_t src_len, uint32_t offset) -> int32_t;
mem_read_vram(uint8_t *dest, uint32_t dest_len, uint32_t offset) -> int32_t;
mem_read_oam(uint8_t *dest, uint32_t dest_len, uint32_t offset) -> int32_t;
```

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

## Network subsystem
```c
net_tcp_socket() -> int32_t;
net_udp_socket() -> int32_t;
net_connect(int32_t slot, uint32_t ipv4_addr, uint16_t port) -> int32_t;
net_bind(int32_t slot, uint32_t ipv4_addr, uint16_t port) -> int32_t;
net_listen(int32_t slot) -> int32_t;
net_accept(int32_t slot, int32_t *o_accepted_slot, uint32_t *o_ipv4_addr, uint16_t *o_port) -> int32_t;
net_poll(net_poll_slot *poll_slots, uint32_t poll_slots_len) -> int32_t;
net_send(int32_t slot, uint8_t *data, uint32_t data_len) -> int32_t;
net_sendto(int32_t slot, uint8_t *data, uint32_t data_len, uint32_t ipv4_addr, uint16_t port) -> int32_t;
net_recv(int32_t slot, uint8_t *data, uint32_t data_len) -> int32_t;
net_recvfrom(int32_t slot, uint8_t *data, uint32_t data_len, uint32_t *o_ipv4_addr, uint16_t *o_port) -> int32_t;
net_close(int32_t slot) -> int32_t;

typedef struct {
    int32_t     slot;
    uint32_t    events;
    uint32_t    revents;
} net_poll_slot;
```
The network subsystem exposes the familiar yet slightly simplified BSD Sockets API.

IPv4 is assumed for all addresses; IPv6 is not supported.

TCP and UDP sockets are supported.

All I/O is non-blocking; this is fundamental to the design of WebAssembly running in a single dedicated thread and this
cannot be changed.

For TCP sockets, TCP_NODELAY option is always enabled to reduce latency.

All functions return an `int32_t` with a negative value to indicate failure. The negative value is a standardized
`errno` value albeit negated. For all functions a `0` return value indicates success.

```c
net_tcp_socket() -> int32_t;
```
Creates a new TCP socket and returns its unique slot number.

```c
net_udp_socket() -> int32_t;
```
Created a new UDP socket and returns its unique slot number.

```c
net_connect(int32_t slot, uint32_t ipv4_addr, uint16_t port) -> int32_t;
```
Connects a socket to a remote IPv4 endpoint described by `ipv4_addr` and `port`.

`ipv4_addr` is an IPv4 address represented as a 32-bit word.

`port` is a 16-bit port number.

```c
net_bind(int32_t slot, uint32_t ipv4_addr, uint16_t port) -> int32_t;
```
Binds a socket to a local IPv4 endpoint described by `ipv4_addr` and `port`. The `ipv4_addr` can be the value `0` to
bind to all network interfaces, or can be `0x7F000001` to bind to the classic `127.0.0.1` loop-back address, or can be
any legitimate network interface address available to listen on.

`ipv4_addr` is an IPv4 address represented as a 32-bit word.

`port` is a 16-bit port number.

```c
net_listen(int32_t slot) -> int32_t;
```
Indicates the TCP socket is ready to accept client connections.

```c
net_accept(int32_t slot, uint32_t *o_ipv4_addr, uint16_t *o_port) -> int32_t;
```
Accepts an incoming TCP connection and returns its slot number, remote IPv4 address, and remote port.

```c
net_poll(net_poll_slot *poll_slots, uint32_t poll_slots_len) -> int32_t;
```
Polls a set of sockets for various events.

For each socket entry, `slot` must be set to an open socket's slot number. `events` should be set to `1` to poll for
input events. After the call, the `revents` field will contain a bitfield that represents which events have occurred.

` (revents & 1) != 0` -> socket is readable; i.e. data is available to read
` (revents & 4) != 0` -> socket is writable; i.e. data can be written to
` (revents & 8) != 0` -> an error has occurred (always check this first before checking readable)
`(revents & 16) != 0` -> the socket has been closed
`(revents & 32) != 0` -> `events` was an invalid argument

```c
net_send(int32_t slot, uint8_t *data, uint32_t data_len) -> int32_t;
```
Sends data to the remote endpoint associated with this socket with `net_connect`.

```c
net_sendto(int32_t slot, uint8_t *data, uint32_t data_len, uint32_t ipv4_addr, uint16_t port) -> int32_t;
```
Sends data to a specific UDP IPv4 remote endpoint.

```c
net_recv(int32_t slot, uint8_t *data, uint32_t data_len) -> int32_t;
```
Receives data from the remote endpoint associated with this socket with `net_connect`.

```c
net_recvfrom(int32_t slot, uint8_t *data, uint32_t data_len, uint32_t *o_ipv4_addr, uint16_t *o_port) -> int32_t;
```
Receives data and returns the UDP IPv4 remote endpoint it came from.

```c
net_close(int32_t slot) -> int32_t;
```
Closes the socket.
