# Stock vs Turbo — measured results (same board, same card, same WiFi)

Board #2 (MAC d4:8c:49:02:52:38, 192.168.0.120, RSSI -61..-66), 29 GB SDHC card,
2 MB random payload, curl from a WiFi PC via VPN_AP. Stock = factory firmware
(now archived as firmware/stock_backup_d48c49025238.bin). Turbo = 2.1-turbo
build Aug 6 07:51 with custom lwIP (8×MSS). 2026-08-06.

| Test | Stock | Turbo | Gain |
|---|---|---|---|
| GET 2 MB (download) | 61.8 KB/s (34.0 s) | **~1,000 KB/s (2.1 s)**¹⁶ | **×16.2** |
| PUT 2 MB (upload) | 165 KB/s (12.7 s) | **~390 KB/s (5.1 s)** | **×2.4** |
| PROPFIND / Depth:1 ×10 | 105 ms avg / 123 worst | **59 ms avg / 77 worst** | ×1.8 |
| 10 × 4 KB PUT storm | 7.5 files/s | **9.9 files/s** | ×1.3 |
| MD5 integrity 2 MB | match | match | |
| GET /nonexistent | 404 in 57 ms | 404 in 31 ms | |
| **1 idle connection parked** | **board hangs: every request times out** | 207 in 62 ms | **fixed** |
| after idle conn closes again | **still dead — permanent until power cycle** | healthy | **fixed** |
| Boot to ready | ~20 s hard delay in code (`delay(20000)`) | ~3-5 s | ×4-6 |
| SD card hot-insert | needs reboot | auto-mounts in ≤3 s | fixed |
| Heap during transfers | n/a | 33.9k idle / 14.5k min / frag 30 % | healthy |

¹⁶ Final build uses TCP_SND_BUF = 16×MSS: GET measured 997 & 1,017 KB/s in
back-to-back runs, min free heap 12.2 KB under sustained load (frag 6 %).
The earlier 8×MSS build measured 656-698 KB/s.

Board #1 turbo 8×MSS (different session, RSSI -58..-65): GET 698 KB/s,
PUT 437 KB/s, PROPFIND 108 ms - consistent within WiFi variance.

The idle-connection row is the everyday killer: browsers routinely open spare
connections, so simply *browsing* the stock board wedges it until power cycle.
That is the "hung for a very long time" experience on stock; turbo recycles idle
connections in 150 ms.

Reproduce: `bench.ps1 -Ip <board-ip>`, plus the idle-conn probe (python socket
open + concurrent curl) documented in the project notes.
