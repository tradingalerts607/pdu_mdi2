
$src  = "pdu_mdi2.c"
$lines = [System.IO.File]::ReadAllLines($src)
$out  = [System.Collections.Generic.List[string]]::new()

$inConflict = $false
$keepLine   = $true   # true = upstream half (keep), false = stashed half (drop)

foreach ($line in $lines) {
    if ($line -match '^<<<<<<<') { $inConflict = $true;  $keepLine = $true;  continue }
    if ($line -match '^=======') { $keepLine = $false; continue }
    if ($line -match '^>>>>>>>') { $inConflict = $false; $keepLine = $true;  continue }
    if ($inConflict -and -not $keepLine) { continue }

    # Remove bogus uridLock block injected by a previous AI session
    if ($line -match 'uridLock|// ADD RIGHT HERE:|// EVERYTHING BELOW STAYS') { continue }

    $out.Add($line)
}

[System.IO.File]::WriteAllLines($src, $out, [System.Text.Encoding]::UTF8)
Write-Host ("Done. Lines written: " + $out.Count)

# Verify no conflict markers remain
$remaining = Select-String -Path $src -Pattern "^<<<<<<<|^=======|^>>>>>>>" 
if ($remaining) {
    Write-Host ("WARNING: " + $remaining.Count + " conflict markers still present!")
} else {
    Write-Host "OK: No conflict markers remain."
}
