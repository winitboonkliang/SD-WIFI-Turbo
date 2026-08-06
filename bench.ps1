# SD-WIFI benchmark - run against stock and turbo firmware for an A/B shootout.
# Usage:  .\bench.ps1 -Ip 192.168.1.51 [-SizeMB 8] [-Label turbo]
param(
  [Parameter(Mandatory=$true)][string]$Ip,
  [int]$SizeMB = 8,
  [string]$Label = "run"
)

$ErrorActionPreference = 'Stop'
$base = "http://$Ip"
$tmp  = Join-Path $env:TEMP "sdwifi_bench_$SizeMB`MB.bin"

# test payload (random once, reused)
if (-not (Test-Path $tmp) -or (Get-Item $tmp).Length -ne $SizeMB*1MB) {
  Write-Host "creating $SizeMB MB random payload..."
  $buf = New-Object byte[] (1MB)
  $rng = [Random]::new(1234)
  $fs = [IO.File]::Create($tmp)
  for ($i=0; $i -lt $SizeMB; $i++) { $rng.NextBytes($buf); $fs.Write($buf,0,$buf.Length) }
  $fs.Close()
}

Write-Host "`n=== SD-WIFI bench [$Label] target $base payload $SizeMB MB ===" -ForegroundColor Cyan

# 1) PROPFIND latency x20 (Explorer "feel")
$sw = [Diagnostics.Stopwatch]::new(); $times = @()
for ($i=0; $i -lt 20; $i++) {
  $sw.Restart()
  curl.exe -s -o NUL -X PROPFIND -H "Depth: 1" "$base/" | Out-Null
  $sw.Stop(); $times += $sw.Elapsed.TotalMilliseconds
}
$avg = ($times | Measure-Object -Average).Average
$max = ($times | Measure-Object -Maximum).Maximum
Write-Host ("PROPFIND /  x20 : avg {0,7:N1} ms   worst {1,7:N1} ms" -f $avg, $max)

# 2) upload (PUT)
$sw.Restart()
curl.exe -s -o NUL -T $tmp "$base/bench.bin"
$sw.Stop()
$up = $SizeMB*1MB / $sw.Elapsed.TotalSeconds / 1KB
Write-Host ("PUT  {0} MB     : {1,7:N1} s  =  {2,7:N0} KB/s" -f $SizeMB, $sw.Elapsed.TotalSeconds, $up)

# 3) download (GET)
$sw.Restart()
curl.exe -s -o NUL "$base/bench.bin"
$sw.Stop()
$down = $SizeMB*1MB / $sw.Elapsed.TotalSeconds / 1KB
Write-Host ("GET  {0} MB     : {1,7:N1} s  =  {2,7:N0} KB/s" -f $SizeMB, $sw.Elapsed.TotalSeconds, $down)

# 4) small-file storm: 15 x 4KB PUT (metadata/latency bound)
$small = Join-Path $env:TEMP "sdwifi_small.bin"
if (-not (Test-Path $small)) { [IO.File]::WriteAllBytes($small, (New-Object byte[] 4096)) }
$sw.Restart()
for ($i=0; $i -lt 15; $i++) { curl.exe -s -o NUL -T $small "$base/s$i.bin" }
$sw.Stop()
Write-Host ("15x 4KB PUT     : {0,7:N1} s  =  {1,7:N1} files/s" -f $sw.Elapsed.TotalSeconds, (15/$sw.Elapsed.TotalSeconds))

# cleanup remote
curl.exe -s -o NUL -X DELETE "$base/bench.bin"
for ($i=0; $i -lt 15; $i++) { curl.exe -s -o NUL -X DELETE "$base/s$i.bin" }
Write-Host "done (remote test files deleted)`n"
