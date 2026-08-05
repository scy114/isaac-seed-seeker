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
    $IsaacFont = Invoke-WebRequest ($BaseUrl + "assets/isaacsans.ttf") -UseBasicParsing
    $LanaPixelFont = Invoke-WebRequest ($BaseUrl + "assets/lanapixel.ttf") -UseBasicParsing
    $SeekerTitle = Invoke-WebRequest ($BaseUrl + "assets/isaac-seed-seeker-title.png") -UseBasicParsing
    if (
        $Page.Content -notmatch 'id="red-hearts-min"' -or
        $Page.Content -notmatch 'data-sort-key="damage"' -or
        $Page.Content -notmatch 'id="page-size"' -or
        $Page.Content -notmatch 'id="catalog-state"' -or
        $ClientScript.Content -notmatch "pill_effect_ids" -or
        $ClientScript.Content -notmatch "sort_direction" -or
        $ClientScript.Content -notmatch "compareMatches" -or
        $ClientScript.Content -notmatch "class CatalogPicker" -or
        $IsaacFont.RawContentLength -lt 10000 -or
        $LanaPixelFont.RawContentLength -lt 1000000 -or
        $SeekerTitle.Headers["Content-Type"] -notmatch "image/png" -or
        $SeekerTitle.RawContentLength -lt 100000
    ) {
        throw "embedded WebUI does not expose the generic Eden filters"
    }
    if (
        $Page.Content -match 'id="preset-target"' -or
        $Page.Content -match '<option value="trinket" selected' -or
        $Page.Content -match 'id="pocket-ids" value=' -or
        $Page.Content -match 'id="active-ids" value=' -or
        $Page.Content -match 'id="passive-ids" value=' -or
        $ClientScript.Content -match 'applyTargetPreset'
    ) {
        throw "embedded WebUI should start with generic empty filters"
    }
    $Catalog = Invoke-RestMethod ($BaseUrl + "catalog.json")
    if ($Catalog.catalog_id -ne "huiji-j460-169621-169298" -or $Catalog.counts.entries -ne 1078) {
        throw "embedded item catalog has unexpected metadata"
    }
    $Drawing = @($Catalog.entries | Where-Object { $_.kind -eq "trinket" -and $_.search_id -eq 169 })
    if (
        $Drawing.Count -ne 1 -or
        $Drawing[0].name_en -ne "Kid's Drawing" -or
        -not (@($Drawing[0].pinyin) -contains "maopian")
    ) {
        throw "embedded item catalog is missing the target trinket search keys"
    }
    $Profile = Invoke-RestMethod ($BaseUrl + "api/v1/profile")
    $Assets = Invoke-RestMethod ($BaseUrl + "api/v1/assets")
    if ($Profile.local_game_icons) {
        $Icon = Invoke-WebRequest ($BaseUrl + "game-assets/trinket/169.png") -UseBasicParsing
        if ($Icon.Headers["Content-Type"] -notmatch "image/png" -or $Icon.RawContentLength -lt 100) {
            throw "local game icon endpoint did not return a PNG"
        }
        if ($Assets.collectible_icons -lt 700 -or $Assets.trinket_icons -lt 180) {
            throw "local game icon index is unexpectedly incomplete"
        }
        if ($Assets.basement_texture) {
            $Basement = Invoke-WebRequest ($BaseUrl + "game-assets/ui/basement-floor.png") -UseBasicParsing
            if ($Basement.Headers["Content-Type"] -notmatch "image/png" -or $Basement.RawContentLength -lt 1000) {
                throw "local basement texture endpoint did not return a PNG"
            }
        }
        if ($Assets.basement_walls) {
            $BasementWalls = Invoke-WebRequest ($BaseUrl + "game-assets/ui/basement-walls.png") -UseBasicParsing
            if ($BasementWalls.Headers["Content-Type"] -notmatch "image/png" -or $BasementWalls.RawContentLength -lt 1000) {
                throw "local basement wall endpoint did not return a PNG"
            }
        }
        if ($Assets.seed_paper) {
            $SeedPaper = Invoke-WebRequest ($BaseUrl + "game-assets/ui/seed-paper.png") -UseBasicParsing
            if ($SeedPaper.Headers["Content-Type"] -notmatch "image/png" -or $SeedPaper.RawContentLength -lt 100) {
                throw "local seed paper endpoint did not return a PNG"
            }
        }
    }
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
    if ($Inspected.active_quality -lt 0 -or $Inspected.passive_quality -lt 0 -or
        $Inspected.total_quality -ne ($Inspected.active_quality + $Inspected.passive_quality)) {
        throw "inspect endpoint returned invalid item qualities"
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

    $SortedBody = @{
        pocket_kind = "none"
        sort_key = "damage"
        sort_direction = "desc"
        start = 1
        end = 5000
        threads = 4
        max_results = 7
    } | ConvertTo-Json -Compress
    Invoke-RestMethod `
        ($BaseUrl + "api/v1/search") `
        -Method Post `
        -ContentType "application/json" `
        -Headers $Headers `
        -Body $SortedBody | Out-Null
    do {
        Start-Sleep -Milliseconds 50
        $SortedStatus = Invoke-RestMethod ($BaseUrl + "api/v1/search/status")
    } while ($SortedStatus.state -eq "running")
    $SortedResults = Invoke-RestMethod ($BaseUrl + "api/v1/search/results")
    if ($SortedResults.count -ne 7 -or -not $SortedResults.truncated -or
        $SortedResults.sort_key -ne "damage" -or $SortedResults.sort_direction -ne "desc") {
        throw "sorted Top-K endpoint returned invalid metadata"
    }
    for ($Index = 1; $Index -lt $SortedResults.matches.Count; $Index++) {
        if ($SortedResults.matches[$Index - 1].damage -lt $SortedResults.matches[$Index].damage) {
            throw "sorted Top-K endpoint did not return descending damage"
        }
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
