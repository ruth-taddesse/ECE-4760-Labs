# Galton background DMA

`initVGAWithBackground(galton_background)` selects the background pipeline.
`initVGA()` retains the original driver for other demos. The background mode
requires `DOUBLE_BUFFER_60` and owns DMA IRQ 1 and the XIP streaming FIFO.
It claims six DMA channels; audio claims its two separately.

At RGB transfer completion, a short RAM-resident interrupt handler selects the
next display/draw buffers and prepares a background copy if a back buffer is free.
The control DMA chain is:

```text
Display-pointer DMA -> Draw-pointer DMA -> Dispatch DMA
                                            |        |
                                            |        +-> Background DMA -> Start-flag DMA
                                            +-> RGB DMA
```

Dispatch writes a channel mask to the DMA multi-channel trigger register.
RGB restarts without waiting for the background copy. The copy reads 9,600
32-bit words from `XIP_AUX_BASE`, paced by `DREQ_XIP_STREAM`, and writes them
to the back buffer. Flash streaming supplies the FIFO in flash idle cycles;
DMA does not issue long-latency reads against the memory-mapped flash window.
This uses neither a pacing timer nor a third RAM framebuffer.

The copy completion chains to a one-word DMA transfer setting `start_flag`.
The animation thread therefore receives a buffer containing the pegs and fixed
labels. It draws balls and then waits for the count thread to draw the numbers
and histogram. Only then does `vga_frame_done()` publish the completed frame.

If the copy or renderer takes longer than one display interval, the display
repeats its front buffer. The back buffer stays exclusively owned by the copy
or renderer, and no second copy is launched into it. Buffer swaps happen only
after publication. This avoids overwriting an unfinished frame, though it
cannot guarantee 60 rendered frames per second or recover a PIO FIFO underrun.
Physics N counts rendered animation iterations, not repeated display refreshes.

The generator builds the flash image from the current peg geometry and GLCD
font. Pegs and labels are magenta; the rest is black. Balls render
on top of this background. Numbers are formatted only when changed, but must
still be drawn after each restoration.

## Hardware validation

Watch these symbols without leaving the CPU halted (PIO continues while halted):

- `vga_rgb_stall_count`: sampled intervals with RGB FIFO starvation, excluding
  the initial sample. It should remain zero. It is not an exact stall count.
- `vga_repeated_frames`: display intervals in which the next frame was not ready.
  Growth under load is expected; it indicates reduced rendered frame rate.

Measure the rendered frame rate and CPU load on the Pico before claiming a
speedup. The flash bandwidth cost remains, even with this improved DMA path.

PIO0 HSync and VSync provide timing; PIO0 SM2 (`line_sync.pio`) bridges
active-line IRQs onto GPIO22 for PIO1. GPIO22 requires no external wire but
must not be connected to another active signal. PIO1 and GPIO22 are reserved.
The physical VGA resistor wiring is unchanged.

## Optional timing overlay

Set `GALTON_TIMING_DISPLAY` in `galton_config.h` to `true` or `false`, then
rebuild. It is enabled by default. Both the application and driver share this
compile-time switch. When false, the timing calls, state, diagnostic counters,
formatting, and overlay rendering are omitted from compilation.

The four lines below the normal statistics show presented FPS, repeated display
frames per second, average completed-frame work time, and worst completed-frame
work time in the last sample interval. Rates use measured elapsed time rather
than assuming every update arrives exactly one second apart. Work time begins
just before background streaming and ends in `vga_frame_done()`, including the
count thread and timing-overlay drawing. It excludes waiting to present a ready
frame. The first interval shows placeholders; metrics update about once per
second. A hung renderer cannot refresh an on-screen overlay.

With the switch enabled, the HUD itself has some overhead, included in measured
work time. Disable it for final performance runs. The debugger counters mentioned
above also exist only when the switch is enabled.

## Current two-color format

The active target uses `VGA/rgb2.pio`: eight 1-bit pixels per byte, least
significant bit first. Zero is black; one outputs GPIO pin value 12 (magenta).
Legacy nonblack drawing colors all map to magenta. Each 640x480 framebuffer
uses 38,400 bytes (80 bytes per row); double buffering uses 76,800 bytes.
The flash background also uses 38,400 bytes. DMA transfers 9,600 32-bit words.
The decoder uses 12 system cycles per pixel at 300 MHz for a 25 MHz pixel rate.
The ball array now holds 54,100 eight-byte balls; the initial count is unchanged.
Run `python tools/check_vga4.py` for the active two-color PIO/background checks
(the test filename is retained for compatibility).
