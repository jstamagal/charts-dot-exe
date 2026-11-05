# Native Supvan printing (Linux, C89)

The entire print path is inside `charts`: device discovery, raw USB HID I/O,
label fitting, monochrome raster packing, LZMA1 range encoding, and TCP sockets.
No `supvan-print`, SSH executable, Python, Cairo, Rust, libusb, liblzma, CUPS or
other helper/library is invoked. The normal build uses only libc/libm, as before;
the static musl build has no shared-library requirements. Production code remains
C89. Other targets compile explicit unsupported-printer stubs.

## Commands

```sh
charts data.csv --printer supvan
charts data.csv --printer /dev/hidraw5
charts data.csv --printer supvan://vader:9280
charts deck.json --printer supvan://vader --slide 2
charts data.csv --printer supvan --printer-preview label.png
charts --printer-status
charts --printer supvan://vader --printer-status
```

`--printer supvan` discovers the local USB device unless `CHARTS_PRINTER` selects
another default target. A URL selects native TCP; the default port is 9280.
`--printer-preview -` writes a PNG to stdout without printing. `--print` remains
terminal text. Status takes no input and returns JSON with stock dimensions,
remaining labels, printing/busy flags, and raw registers.

One command prints one selected slide. Multi-slide input requires `--slide`.
The paper profile remains white, monochrome patterned, flat, and without viewer
chrome. The default source canvas is 608 × 352 pixels; `--size COLSxROWS`
overrides it. Native nearest-neighbor fitting preserves bitmap-font pixels,
rotates if that fits better, and centers without cropping. Grays use a 4×4
ordered dither. The preview contains the exact binary pixels sent to the head.

The current roll on vader is 50 × 80 mm die-cut stock. The head is 384 dots
(48 mm) wide at 8 dots/mm; narrower rolls are centered on the full-width head.
The fitter reserves 2 mm margins. Continuous stock is rejected until a length
interface is supplied; there is no guessed feed length.

## USB access

Discovery probes hidraw identity using `HIDIOCGRAWINFO` and accepts only USB
vendor `1820`, product `2076`. Explicit device paths undergo the same identity
check. Multiple matching devices require an explicit path. The caller needs
read/write permission on that device, not root or permission for every HID.
A matching-device udev rule can provide it; the existing rule on vader grants
only the homelab account access to this printer.

An advisory lock at `$XDG_CACHE_HOME/supvan-print/queue.lock` (default
`~/.cache/supvan-print/queue.lock`) serializes native status, preview and print
operations, and also cooperates with the separate SVG utility on this host.
All submitting agents use the same USB-host account. Raw tools that bypass this
lock are not part of the queue.

If the device is offline, hold the front power button about two seconds and
release at the startup chime. Red can mean charging. Query status rather than
inferring power from LED color. No firmware/calibration/RFID provisioning or
power-setting writes are performed.

## Native network server

The **same binary** serves the USB printer:

```sh
charts --printer-listen 10.0.0.218:9280
```

It accepts only an explicit private IPv4 or loopback bind address: wildcard and
public binds are rejected. Use a supervisor to keep that command running if
needed; the supervisor is not part of the application or required by its CLI.
Vader's optional runit service is `charts-printer`, executing the installed
single binary as the homelab account. No runtime code points into agent scratch.

Both ends read `~/.config/charts/printer.token`, or `CHARTS_PRINTER_TOKEN_FILE`.
The token file must be a regular file owned by the invoking account, with no
group/other permission bits, containing exactly 64 hexadecimal characters and
optionally one trailing newline. Generate it once from 32 random bytes and
copy it securely to authorized clients, preserving mode 0600. For example,
setup can use ordinary system tools:

```sh
umask 077
mkdir -p ~/.config/charts
od -An -N32 -tx1 /dev/urandom | tr -d ' \n' > ~/.config/charts/printer.token
```

Do not regenerate over an existing configured token unless rotating all clients.
USB-only printing does not need a token.

**Trusted LAN only:** the token and pixels travel unencrypted. This is not TLS,
IPP, Internet printing, or authentication resistant to LAN sniffing. No encryption
library is smuggled into the zero-dependency program. Keep the endpoint off
untrusted networks.

The server serializes requests, bounds source images to 16 million pixels and
8192 per axis, verifies the token before accepting pixels, and rejects unknown
operations, reserved fields, and palette values outside 0..15. An unauthenticated
header has a short timeout; transfers and hardware waits are bounded. No command
execution or arbitrary USB command forwarding is exposed.

Failures are **never retried automatically**: a network interruption after
submission may leave a completed paper label even if the reply was lost.
Inspect the label and stock count before resending. Completion waits for the
reported remaining count to decrease (when nonzero) and idle status to remain
stable for one second, not just a transient cleared printing bit.

## Network wire format, version 1

All network integers are big-endian. The request is 80 bytes, then optional
row-major VGA-indexed pixels (one byte per pixel, values 0..15, no row padding):

| Offset | Size | Meaning |
| --- | --- | --- |
| 0 | 8 | ASCII `CHPR0001` |
| 8 | 64 | Token file's 64 ASCII hex characters |
| 72 | 1 | 1=status, 2=print, 3=preview |
| 73 | 3 | Reserved, must be zero |
| 76 | 2 | Source width; zero for status |
| 78 | 2 | Source height; zero for status |
| 80 | W×H | Pixels for print/preview; none for status |

Response: 8-byte magic, 4-byte result (0=success, 1=error), 4-byte body length,
then exactly that body. Errors are text. Status and print return JSON. A preview
body is a 2-byte width, 2-byte height, then one-byte indexed pixels; clients use
the existing in-process PNG writer. Width is 384 and height is at most 2040.
A preview never submits print buffers. The wire format is deliberately not a
PNG decoder, SVG interpreter, or remotely callable shell.

## USB wire implementation

The protocol reference was checked against the working T50M Pro and
[the reverse-engineered Supvan protocol](https://github.com/heeen/supvan-cups/blob/master/docs/PROTOCOL.md).
The implementation here is independent C89, not a linked driver.

- Every output is 65 bytes: explicit report ID 0, then a zero-padded 64-byte
  payload. Omitting the report ID corrupts payloads beginning with zero.
- Command payload: `C0 40`, big-endian parameter, opcode, `00 08 00`;
  two-parameter commands append the second big-endian 16-bit value.
- Read-only discovery uses CHECK_DEVICE `12`, status `11`, material `30`.
- Print: wait idle, START_PRINT `13` with parameter 1, wait printing,
  wait for free buffer, NEXT_ZIPPEDBULK `5C` with compressed length, raw 64-byte
  chunks, bulk acknowledgement, 150 ms delay, BUF_FULL `10` with length and
  speed, then 100 ms delay. A failed started job receives STOP_PRINT `14`.
- The packed monochrome raster is 48 bytes per feed row, LSB-first. Fixed
  4096-byte print buffers contain a 14-byte header, up to 32 rows, zero tail,
  first/last/job flags and the vendor checksum (header bytes 2..13 plus bytes
  255, 511, ... through the raster data).
- `app/supvan.c` implements an adaptive **literal-only LZMA1 range encoder**.
  No match finder or external compression code. Its 13-byte alone header is
  property `5D`, dictionary 8192 LE, exact uncompressed size LE64, followed by
  the range-coded bytes **without an end marker**, as required by the firmware.
  Arithmetic is explicitly modulo 32 bits on ILP32 and LP64 without C99 types
  or `long long`.
- A whole page goes in one block when it fits the 4096-byte receive limit,
  at speed 60. Otherwise each 32-row buffer goes separately at speed 10;
  the smaller chunks leave substantial compression headroom and the slower
  feed covers the USB/timer overhead without starving the printhead.

## Verification

`make test-printer` performs no physical prints. A mock TCP peer checks native
rendering/transport, empty-PATH operation, preview PNGs, status, invalid frames,
authentication, bounds, error propagation and no automatic retry. It also starts
the actual native listener to check rejection before any hardware access.

The C codec tests validate flags/checksum/padding and capacity handling.
A separate Python standard-library LZMA decoder round-trips the handrolled
encoder on empty, repetitive, random, maximum-size, and random-raster buffers.
Python is only an independent **test oracle**, never a runtime dependency.
`make test-asan` covers the new parser, socket client and compressor too.
