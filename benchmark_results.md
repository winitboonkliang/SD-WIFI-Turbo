# Stock vs Turbo — measured results (same board, same card, same WiFi)

Board #2 (MAC d4:8c:49:02:52:38, 192.168.0.120, RSSI -58..-66), 29 GB SDHC card,
2 MB random payload, curl from a WiFi PC. Stock = the factory firmware that
shipped on this board (archived as firmware/stock_backup_d48c49025238.bin);
turbo = 2.1-turbo with the custom 16×MSS lwIP. Both runs on the same hardware
and card, 2026-08-06.

| Test | Stock | Turbo | Gain |
|---|---|---|---|
| GET 2 MB (download) | 61.8 KB/s (34.0 s) | **907 KB/s (2.3 s)**¹ | **×14.7** |
| PUT 2 MB (upload) | 165 KB/s (12.7 s) | **442 KB/s (4.7 s)** | **×2.7** |
| PROPFIND / Depth:1 ×10 | 105 ms avg / 123 worst | **71 ms avg / 91 worst** | ×1.5 |
| 10 × 4 KB PUT storm | 7.5 files/s | **9.9 files/s** | ×1.3 |
| MD5 integrity 2 MB | match | match | |
| GET /nonexistent | 404 in 57 ms | 404 in 31 ms | |
| **1 idle connection parked** | **board hangs: every request times out** | 207 in 62 ms | **fixed** |
| after idle conn closes again | **still dead — permanent until power cycle** | healthy | **fixed** |
| 20 rapid requests, no card | (n/a — hangs earlier) | 0 failed, 0.8 s total² | |
| Boot to ready | ~20 s hard delay in code (`delay(20000)`) | ~3-5 s | ×4-6 |
| SD card hot-insert | needs reboot | auto-mounts, no reboot | fixed |
| Heap during transfers | n/a | 33.1k idle / 7.7k min / frag 37 % | see below |

¹ Run-to-run spread on the same board was 907–1,017 KB/s depending on RSSI;
the table quotes the most recent (conservative) run. The 8×MSS build measured
656–698 KB/s, and the stock prebuilt lwIP (2×MSS) caps at ~145 KB/s.

² Regression suite run against a board with **no card inserted** — the worst
case. Before the empty-slot fixes this suite showed 8 s timeouts and 3/20
failures; see the "empty card slot" commit.

**Heap note:** 16×MSS buys the top-end download speed but the minimum free heap
observed under a sustained 2 MB transfer is 7.7 KB (guard trips at 4 KB).
That is a working margin, not a comfortable one. For boards that must run for
weeks unattended, rebuilding lwIP with 12×MSS trades roughly 10–15 % of
download speed for about 6 KB more headroom.

The idle-connection row is the everyday killer: browsers routinely open spare
connections, so simply *browsing* the stock board wedges it until power cycle.
That is the "hung for a very long time" experience on stock; turbo recycles idle
connections in 150 ms.

Reproduce: `bench.ps1 -Ip <board-ip>`, plus the idle-conn probe (python socket
open + concurrent curl) documented in the project notes.

## SD card raw speed (same card, USB 2.0 reader on the PC)

| Direction | Card via USB reader | Card via SD-WIFI (WiFi) | Board reaches |
|---|---|---|---|
| Read / download | 19.4 MB/s | ~1.0 MB/s | 5 % of card speed |
| Write / upload | 3.3 MB/s | ~0.39 MB/s | 12 % of card speed |

32 MB test file, unbuffered read (buffered reads report ~1.5 GB/s from OS cache -
meaningless). Conclusion: the SD card is **not** the bottleneck for WiFi transfers.
The ESP8285's single core (SPI + radio + TCP on one CPU) and the 2.4 GHz link are.
