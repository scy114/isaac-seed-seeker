$ErrorActionPreference = "Stop"
$ProjectRoot = Split-Path -Parent $PSScriptRoot
$Executable = Join-Path $ProjectRoot "build\native\IsaacSeedSeeker.exe"
$Stdout = New-TemporaryFile
$Stderr = New-TemporaryFile
$Process = $null

try {
    $Process = Start-Process `
        -FilePath $Executable `
        -ArgumentList @("serve", "--no-browser", "1") `
        -WindowStyle Hidden `
        -PassThru `
        -RedirectStandardOutput $Stdout `
        -RedirectStandardError $Stderr

    $Url = $null
    for ($Attempt = 0; $Attempt -lt 50 -and -not $Url; $Attempt++) {
        Start-Sleep -Milliseconds 100
        $Line = Get-Content $Stdout -ErrorAction SilentlyContinue | Select-Object -First 1
        if ($Line -match "(http://127\.0\.0\.1:\d+/\?token=[0-9a-f]+)") {
            $Url = $Matches[1]
        }
    }
    if (-not $Url) {
        throw "server did not report a URL: $(Get-Content $Stderr -Raw)"
    }

    if ($Url -notmatch "^(http://127\.0\.0\.1:\d+/)\?token=([0-9a-f]+)$") {
        throw "server URL did not contain a session token"
    }
    $BaseUrl = $Matches[1]
    $Headers = @{"X-Isaac-Token" = $Matches[2]}
    $Page = Invoke-WebRequest $Url -UseBasicParsing
    $ClientScript = Invoke-WebRequest ($BaseUrl + "app.js") -UseBasicParsing
    if ($Page.Content -notmatch 'id="red-hearts-min"' -or $ClientScript.Content -notmatch "pill_effect_ids") {
        throw "embedded WebUI does not expose the generic Eden filters"
    }
    $Profile = Invoke-RestMethod ($BaseUrl + "api/v1/profile")
    $UnauthorizedBlocked = $false
    try {
        Invoke-RestMethod ($BaseUrl + "api/v1/search/cancel") -Method Post | Out-Null
    } catch {
        if ($_.Exception.Response.StatusCode.value__ -eq 403) {
            $UnauthorizedBlocked = $true
        } else {
            throw
        }
    }
    if (-not $UnauthorizedBlocked) { throw "POST endpoint accepted a request without its session token" }
    $Body = @{
        trinket_id = 169
        active_ids = @(145, 133)
        passive_ids = @(81, 134, 187, 212, 665)
        start = 1
        end = 20000000
        threads = 4
    } | ConvertTo-Json -Compress
    Invoke-RestMethod `
        ($BaseUrl + "api/v1/search") `
        -Method Post `
        -ContentType "application/json" `
        -Headers $Headers `
        -Body $Body | Out-Null

    do {
        Start-Sleep -Milliseconds 100
        $Status = Invoke-RestMethod ($BaseUrl + "api/v1/search/status")
    } while ($Status.state -eq "running")
    $Results = Invoke-RestMethod ($BaseUrl + "api/v1/search/results")

    if ($Profile.id -ne "j460-full-unlock") { throw "unexpected builtin Profile" }
    if ($Status.state -ne "completed") { throw "search did not complete: $($Status.state)" }
    if ($Status.scanned -ne 20000000) { throw "unexpected scan count: $($Status.scanned)" }
    if ($Results.count -ne 3) { throw "unexpected match count: $($Results.count)" }
    if ($Results.matches[0].seed -ne "B74H HQPR") { throw "unexpected first seed" }
    if ($Results.total_count -ne 3 -or $Results.truncated) { throw "unexpected result metadata" }
    $TextResults = Invoke-RestMethod ($BaseUrl + "api/v1/search/results.txt")
    if ($TextResults -notmatch "seed_u32" -or $TextResults -notmatch "`tdamage`t") {
        throw "TXT export is missing the generic result columns"
    }
    if (@($TextResults -split "`n" | Where-Object { $_ }).Count -ne 4) {
        throw "TXT export did not contain a header and three matches"
    }

    $InspectBody = @{seed_u32 = 2} | ConvertTo-Json -Compress
    $Inspected = Invoke-RestMethod `
        ($BaseUrl + "api/v1/inspect") `
        -Method Post `
        -ContentType "application/json" `
        -Headers $Headers `
        -Body $InspectBody
    if ($Inspected.pocket_kind -ne "pill" -or $Inspected.pocket_id -ne 12) {
        throw "inspect endpoint returned the wrong pocket item"
    }
    if ($Inspected.red_hearts -ne 2 -or [Math]::Abs($Inspected.range - 7.426721965) -gt 0.000001) {
        throw "inspect endpoint returned the wrong base rolls"
    }
    if ([Math]::Abs($Inspected.damage - 4.056472253) -gt 0.000001 -or $null -eq $Inspected.tears) {
        throw "inspect endpoint returned the wrong Found HUD stats"
    }

    $GenericBody = @{
        pill_effect_ids = @(12)
        active_ids = @(639)
        passive_ids = @(393)
        red_hearts_min = 2
        red_hearts_max = 2
        damage_min = 4.05
        range_min = 7.42
        range_max = 7.43
        start = 1
        end = 100
        threads = 2
        max_results = 10
    } | ConvertTo-Json -Compress
    Invoke-RestMethod `
        ($BaseUrl + "api/v1/search") `
        -Method Post `
        -ContentType "application/json" `
        -Headers $Headers `
        -Body $GenericBody | Out-Null
    do {
        Start-Sleep -Milliseconds 50
        $GenericStatus = Invoke-RestMethod ($BaseUrl + "api/v1/search/status")
    } while ($GenericStatus.state -eq "running")
    $GenericResults = Invoke-RestMethod ($BaseUrl + "api/v1/search/results")
    if ($GenericResults.total_count -ne 1 -or $GenericResults.matches[0].seed_u32 -ne 2) {
        throw "generic search endpoint returned the wrong seed"
    }

    $CancelBody = @{
        trinket_id = 169
        active_ids = @(145, 133)
        passive_ids = @(81, 134, 187, 212, 665)
        start = 1
        end = 4294967295
        threads = 1
    } | ConvertTo-Json -Compress
    Invoke-RestMethod `
        ($BaseUrl + "api/v1/search") `
        -Method Post `
        -ContentType "application/json" `
        -Headers $Headers `
        -Body $CancelBody | Out-Null
    Invoke-RestMethod ($BaseUrl + "api/v1/search/cancel") -Method Post -Headers $Headers | Out-Null
    do {
        Start-Sleep -Milliseconds 50
        $Cancelled = Invoke-RestMethod ($BaseUrl + "api/v1/search/status")
    } while ($Cancelled.state -eq "running")
    if ($Cancelled.state -ne "cancelled") { throw "cancel endpoint did not stop the search" }

    Invoke-RestMethod ($BaseUrl + "api/v1/shutdown") -Method Post -Headers $Headers | Out-Null
    $Process.WaitForExit(5000) | Out-Null
    $Process.Refresh()
    [pscustomobject]@{
        url = $Url
        profile = $Profile.id
        state = $Status.state
        scanned = $Status.scanned
        matches = $Results.count
        first = $Results.matches[0].seed
        cancel_state = $Cancelled.state
    }
} finally {
    if ($Process -and -not $Process.HasExited) {
        Stop-Process -Id $Process.Id -Force
    }
    Remove-Item -LiteralPath $Stdout, $Stderr -Force -ErrorAction SilentlyContinue
}
