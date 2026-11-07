# StreamCam frames IPC v1

Windows named pipe `\\.\pipe\StreamCamFrames-v1`, created by QLocalServer with UserAccessOption. A second receiver must fail to bind rather than take over a running receiver. OBS connects as a client under the same Windows user. The pipe carries frames only, never pairing credentials, video network packets or commands.

Each packet contains a fixed 40-byte little-endian header followed by tightly bounded BGRA bytes:

| Offset | Type | Meaning |
|---|---|---|
| 0 | u32 | Magic 0x314d4353 (SCM1) |
| 4 | u32 | Version 1 |
| 8 | u32 | Width, 1..1920 |
| 12 | u32 | Height, 1..1080 |
| 16 | u32 | Stride, width*4..7680 |
| 20 | u32 | Data size, exactly stride*height, max 8294400 |
| 24 | u64 | Sender video timestamp, microseconds |
| 32 | u64 | Reserved, must equal zero |

Reader rejects malformed headers before allocating pixels. A source holds only the most recent frame. Writer skips frames when a client still has a queued packet. Pipe close clears preview/source rather than retaining the last camera image. Reader uses cancellable IO and bounded reconnect delays; source destruction cancels IO and joins its worker. OBS must not host signaling or decoder.
