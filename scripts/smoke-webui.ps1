param(
    [string]$Executable
)

$ErrorActionPreference = "Stop"
$ProjectRoot = Split-Path -Parent $PSScriptRoot
if ([string]::IsNullOrWhiteSpace($Executable)) {
    $Executable = Join-Path $ProjectRoot "build\native\IsaacSeedSeeker.exe"
} else {
    $Executable = [IO.Path]::GetFullPath($Executable)
}
if (-not (Test-Path -LiteralPath $Executable -PathType Leaf)) {
    throw "WebUI executable not found: $Executable"
}
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
    $Token = $Matches[2]
    $Headers = @{"X-Isaac-Token" = $Token}
    $Page = Invoke-WebRequest $Url -UseBasicParsing
    $TreatmentPage = Invoke-WebRequest ($BaseUrl + "experimental-treatment.html?token=" + $Token) -UseBasicParsing
    $InspectorPage = Invoke-WebRequest ($BaseUrl + "seed-inspector.html?token=" + $Token) -UseBasicParsing
    $DailyGoodPage = Invoke-WebRequest ($BaseUrl + "daily-good.html?token=" + $Token) -UseBasicParsing
    $DailyBadPage = Invoke-WebRequest ($BaseUrl + "daily-bad.html?token=" + $Token) -UseBasicParsing
    $ClientScript = Invoke-WebRequest ($BaseUrl + "app.js") -UseBasicParsing
    $InspectorScript = Invoke-WebRequest ($BaseUrl + "seed-inspector.js") -UseBasicParsing
    $DailyGoodScript = Invoke-WebRequest ($BaseUrl + "daily-good.js") -UseBasicParsing
    $IsaacFont = Invoke-WebRequest ($BaseUrl + "assets/isaacsans.ttf") -UseBasicParsing
    $LanaPixelFont = Invoke-WebRequest ($BaseUrl + "assets/lanapixel.ttf") -UseBasicParsing
    $SeekerTitle = Invoke-WebRequest ($BaseUrl + "assets/isaac-seed-seeker-title.png") -UseBasicParsing
    $EmbeddedChecks = [ordered]@{
        "generic red hearts" = $Page.Content -match 'id="red-hearts-min"'
        "generic coins" = $Page.Content -match 'id="coins-min"'
        "treatment link" = $Page.Content -match 'id="treatment-page-link"'
        "inspector link" = $Page.Content -match 'id="seed-inspector-link"'
        "daily-good link" = $Page.Content -match 'id="daily-good-page-link"'
        "daily-bad link" = $Page.Content -match 'id="daily-bad-page-link"'
        "generic damage sort" = $Page.Content -match 'data-sort-key="damage"'
        "treatment page" = $TreatmentPage.Content -match 'data-page="treatment"'
        "treatment damage" = $TreatmentPage.Content -match 'id="experimental-damage"'
        "treatment post damage" = $TreatmentPage.Content -match 'id="post-damage-min"'
        "treatment constraint state" = $TreatmentPage.Content -match 'id="treatment-constraint-state"'
        "treatment stat model" = $TreatmentPage.Content -match 'data-treatment-stat="damage"'
        "treatment base stats" = $TreatmentPage.Content -match 'class="base-stats-panel"'
        "treatment back link" = $TreatmentPage.Content -match 'id="generic-page-link"'
        "treatment tagline" = $TreatmentPage.Content -match 'class="treatment-tagline"'
        "inspector page" = $InspectorPage.Content -match 'data-page="inspector"'
        "inspector input" = $InspectorPage.Content -match 'id="seed-input"'
        "inspector result" = $InspectorPage.Content -match 'id="inspector-result"'
        "inspector active" = $InspectorPage.Content -match 'id="result-active"'
        "inspector normalization" = $InspectorScript.Content -match 'normalizeSeed'
        "inspector request" = $InspectorScript.Content -match 'JSON.stringify'
        "daily-good page" = $DailyGoodPage.Content -match 'data-page="daily-good"'
        "daily-good seed" = $DailyGoodPage.Content -match 'id="daily-seed"'
        "daily-good reroll" = $DailyGoodPage.Content -match 'id="reroll-daily-seed"'
        "daily-good reveal" = $DailyGoodPage.Content -match 'id="reveal-daily-seed"'
        "daily-good details" = $DailyGoodPage.Content -match 'id="daily-details"'
        "daily-good random variant" = $DailyGoodScript.Content -match 'randomVariant'
        "daily-good persistence" = $DailyGoodScript.Content -match 'localStorage'
        "daily-good inspect" = $DailyGoodScript.Content -match '/api/v1/inspect'
        "daily-bad page" = $DailyBadPage.Content -match 'data-page="daily-bad"'
        "daily-bad seed" = $DailyBadPage.Content -match 'id="daily-seed"'
        "daily-bad poop icon" = $DailyBadPage.Content -match '/game-assets/active/36.png'
        "daily-bad endpoint" = $DailyGoodScript.Content -match '/api/v1/daily-bad'
        "daily-bad challenge button" = $DailyBadPage.Content -match 'id="challenge-daily-seed"'
        "daily-bad challenge endpoint" = $DailyGoodScript.Content -match '/api/v1/daily-bad-challenge'
        "daily-bad challenge scan modal" = $DailyBadPage.Content -match 'id="challenge-scan-modal"'
        "daily-bad challenge scan toggle" = $DailyGoodScript.Content -match 'showChallengeScan'
        "generic page size" = $Page.Content -match 'id="page-size"'
        "generic catalog state" = $Page.Content -match 'id="catalog-state"'
        "generic pills" = $ClientScript.Content -match "pill_effect_ids"
        "generic post-item stats" = $ClientScript.Content -match "post_item_stats_available"
        "generic treatment mode" = $ClientScript.Content -match "treatmentMode"
        "generic sort direction" = $ClientScript.Content -match "sort_direction"
        "generic result comparator" = $ClientScript.Content -match "compareMatches"
        "generic catalog picker" = $ClientScript.Content -match "class CatalogPicker"
        "Isaac font" = $IsaacFont.RawContentLength -ge 10000
        "LanaPixel font" = $LanaPixelFont.RawContentLength -ge 1000000
        "title MIME" = $SeekerTitle.Headers["Content-Type"] -match "image/png"
        "title image" = $SeekerTitle.RawContentLength -ge 100000
    }
    $FailedEmbeddedChecks = @($EmbeddedChecks.GetEnumerator() | Where-Object { -not $_.Value } | ForEach-Object Key)
    if ($FailedEmbeddedChecks.Count) {
        throw "embedded WebUI check failed: $($FailedEmbeddedChecks -join ', ')"
    }
    if ($Page.Content -match 'id="experimental-damage"' -or
        $Page.Content -match 'id="post-damage-min"' -or
        $Page.Content -match '<th>黄针结果</th>') {
        throw "generic WebUI still embeds the Experimental Treatment controls"
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
    if ($Catalog.catalog_id -ne "huiji-j460-169621-169298" -or $Catalog.counts.entries -ne 1056) {
        throw "embedded item catalog has unexpected metadata"
    }
    $BadGasCatalog = @($Catalog.entries | Where-Object { $_.kind -eq "pill" -and $_.search_id -eq 0 })
    if ($BadGasCatalog.Count -ne 1 -or -not $BadGasCatalog[0].available_for_eden -or
        $Catalog.counts.available_for_eden.pill -ne 50) {
        throw "embedded item catalog does not expose all J460 pill effects"
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

    $DailyBody = @{date = "2026-08-06"; variant = 0} | ConvertTo-Json -Compress
    $DailyFirst = Invoke-RestMethod `
        ($BaseUrl + "api/v1/daily-good") `
        -Method Post `
        -ContentType "application/json" `
        -Headers $Headers `
        -Body $DailyBody
    $DailyFirstAgain = Invoke-RestMethod `
        ($BaseUrl + "api/v1/daily-good") `
        -Method Post `
        -ContentType "application/json" `
        -Headers $Headers `
        -Body $DailyBody
    $DailyVariant = Invoke-RestMethod `
        ($BaseUrl + "api/v1/daily-good") `
        -Method Post `
        -ContentType "application/json" `
        -Headers $Headers `
        -Body (@{date = "2026-08-06"; variant = 305419896} | ConvertTo-Json -Compress)
    if ($DailyFirst.rules_version -ne "daily-good-v1" -or
        $DailyFirst.seed -ne $DailyFirstAgain.seed -or
        $DailyFirst.seed -eq $DailyVariant.seed -or
        $DailyVariant.variant -ne 305419896) {
        throw "daily-good endpoint did not provide a stable first seed and distinct reroll"
    }
    $DailyBadFirst = Invoke-RestMethod `
        ($BaseUrl + "api/v1/daily-bad") `
        -Method Post `
        -ContentType "application/json" `
        -Headers $Headers `
        -Body $DailyBody
    $DailyBadFirstAgain = Invoke-RestMethod `
        ($BaseUrl + "api/v1/daily-bad") `
        -Method Post `
        -ContentType "application/json" `
        -Headers $Headers `
        -Body $DailyBody
    $DailyBadVariant = Invoke-RestMethod `
        ($BaseUrl + "api/v1/daily-bad") `
        -Method Post `
        -ContentType "application/json" `
        -Headers $Headers `
        -Body (@{date = "2026-08-06"; variant = 305419896} | ConvertTo-Json -Compress)
    if ($DailyBadFirst.rules_version -ne "daily-bad-v5" -or
        $DailyBadFirst.seed -ne $DailyBadFirstAgain.seed -or
        $DailyBadFirst.seed -eq $DailyBadVariant.seed -or
        $DailyBadVariant.variant -ne 305419896) {
        throw "daily-bad endpoint did not provide a stable first seed and distinct reroll"
    }
    $DailyBadInspected = Invoke-RestMethod `
        ($BaseUrl + "api/v1/inspect") `
        -Method Post `
        -ContentType "application/json" `
        -Headers $Headers `
        -Body (@{seed = $DailyBadFirst.seed} | ConvertTo-Json -Compress)
    $DailyBadActiveAllowed = $DailyBadInspected.active_quality -eq 0 -or
        $DailyBadInspected.active_id -in @(33, 38, 45, 298, 522, 639, 729)
    $DailyBadPassiveAllowed = $DailyBadInspected.passive_quality -eq 0 -or
        $DailyBadInspected.passive_id -in @(149, 222, 329, 529, 561)
    if (-not $DailyBadActiveAllowed -or
        -not $DailyBadPassiveAllowed -or
        $DailyBadInspected.active_id -in @(19, 59, 137, 161) -or
        $DailyBadInspected.active_id -eq 482 -or
        $DailyBadInspected.passive_id -in @(19, 59, 137, 161) -or
        $DailyBadInspected.bombs -ne 0 -or
        $DailyBadInspected.move_speed -ge 1.0 -or
        $DailyBadInspected.tears -ge 2.5 -or
        $DailyBadInspected.damage -ge 3.0) {
        throw "daily-bad endpoint returned a seed outside the v5 item, bomb, and stat gates"
    }
    $ChallengeBody = @{
        date = "2026-08-06"
        variant = 0
        candidates = 10000000
    } | ConvertTo-Json -Compress
    $ChallengeFirst = Invoke-RestMethod `
        ($BaseUrl + "api/v1/daily-bad-challenge") `
        -Method Post `
        -ContentType "application/json" `
        -Headers $Headers `
        -Body $ChallengeBody
    $ChallengeFirstAgain = Invoke-RestMethod `
        ($BaseUrl + "api/v1/daily-bad-challenge") `
        -Method Post `
        -ContentType "application/json" `
        -Headers $Headers `
        -Body $ChallengeBody
    $ChallengeVariant = Invoke-RestMethod `
        ($BaseUrl + "api/v1/daily-bad-challenge") `
        -Method Post `
        -ContentType "application/json" `
        -Headers $Headers `
        -Body (@{
            date = "2026-08-06"
            variant = 305419896
            candidates = 10000000
        } | ConvertTo-Json -Compress)
    if ($ChallengeFirst.rules_version -ne "daily-bad-challenge-v0" -or
        $ChallengeFirst.cache_hit -ne $false -or
        $ChallengeFirst.seed -ne $ChallengeFirstAgain.seed -or
        $ChallengeFirst.seed -eq $ChallengeVariant.seed -or
        $ChallengeVariant.variant -ne 305419896) {
        throw "daily-bad challenge endpoint was not stable or did not reroll"
    }
    $ChallengeInspected = Invoke-RestMethod `
        ($BaseUrl + "api/v1/inspect") `
        -Method Post `
        -ContentType "application/json" `
        -Headers $Headers `
        -Body (@{seed = $ChallengeFirst.seed} | ConvertTo-Json -Compress)
    $ChallengePocketAllowed = $ChallengeInspected.pocket_kind -eq "none" -or
        ($ChallengeInspected.pocket_kind -eq "pill" -and
            $ChallengeInspected.pocket_id -in @(1, 6, 11, 13, 15, 17))
    $ChallengeLowPanelBranch = $ChallengeInspected.passive_id -in @(561, 697) -and
        $ChallengeInspected.move_speed -lt 1.0 -and
        $ChallengeInspected.damage -lt 3.0 -and
        $ChallengeInspected.tears -lt 2.0
    $ChallengeTreatmentBranch = $ChallengeInspected.passive_id -eq 240 -and
        $ChallengeInspected.post_item_stats_available -and
        $ChallengeInspected.post_damage -lt 2.0 -and
        $ChallengeInspected.post_tears -lt 1.5
    if ($ChallengeInspected.active_id -notin @(36, 39, 41, 177, 287, 290, 294, 325, 475, 480, 481, 582) -or
        -not $ChallengePocketAllowed -or
        $ChallengeInspected.coins -ne 0 -or
        $ChallengeInspected.keys -ne 0 -or
        $ChallengeInspected.bombs -ne 0 -or
        (-not $ChallengeLowPanelBranch -and -not $ChallengeTreatmentBranch)) {
        throw "daily-bad challenge endpoint returned a seed outside the strict challenge rules"
    }
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
    if ($TextResults -notmatch "seed_u32" -or $TextResults -notmatch "`tcoins`t" -or
        $TextResults -notmatch "`tdamage`t" -or
        $TextResults -notmatch "`tpost_item_stats_available`t") {
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
    if ($Inspected.pocket_kind -ne "card" -or $Inspected.pocket_id -ne 12) {
        throw "inspect endpoint returned the wrong pocket item"
    }
    if ($Inspected.active_quality -lt 0 -or $Inspected.passive_quality -lt 0 -or
        $Inspected.total_quality -ne ($Inspected.active_quality + $Inspected.passive_quality)) {
        throw "inspect endpoint returned invalid item qualities"
    }
    if ($Inspected.red_hearts -ne 2 -or $Inspected.coins -ne 0 -or
        $Inspected.keys -ne 0 -or $Inspected.bombs -ne 1 -or
        [Math]::Abs($Inspected.range - 7.194700883) -gt 0.000001) {
        throw "inspect endpoint returned the wrong base rolls"
    }
    if ([Math]::Abs($Inspected.damage - 2.877413690) -gt 0.000001 -or $null -eq $Inspected.tears) {
        throw "inspect endpoint returned the wrong Found HUD stats"
    }

    $LabelInspectBody = @{seed = "TEXZ WDS0"} | ConvertTo-Json -Compress
    $LabelInspected = Invoke-RestMethod `
        ($BaseUrl + "api/v1/inspect") `
        -Method Post `
        -ContentType "application/json" `
        -Headers $Headers `
        -Body $LabelInspectBody
    if ($LabelInspected.seed_u32 -ne 2261264115 -or $LabelInspected.seed -ne "TEXZ WDS0" -or
        $LabelInspected.pocket_kind -ne "card" -or $LabelInspected.pocket_id -ne 2 -or
        $LabelInspected.active_id -ne 347 -or $LabelInspected.passive_id -ne 402) {
        throw "inspect endpoint did not decode the game seed label"
    }

    $InvalidLabelRejected = $false
    try {
        Invoke-RestMethod `
            ($BaseUrl + "api/v1/inspect") `
            -Method Post `
            -ContentType "application/json" `
            -Headers $Headers `
            -Body (@{seed = "TEXZ WDS1"} | ConvertTo-Json -Compress) | Out-Null
    } catch {
        if ($_.Exception.Response.StatusCode.value__ -eq 400) {
            $InvalidLabelRejected = $true
        } else {
            throw
        }
    }
    if (-not $InvalidLabelRejected) { throw "inspect endpoint accepted an invalid seed checksum" }

    $PillInspectBody = @{seed_u32 = 230816840} | ConvertTo-Json -Compress
    $PillInspected = Invoke-RestMethod `
        ($BaseUrl + "api/v1/inspect") `
        -Method Post `
        -ContentType "application/json" `
        -Headers $Headers `
        -Body $PillInspectBody
    if ($PillInspected.pocket_kind -ne "pill" -or $PillInspected.pocket_id -ne 6 -or
        $PillInspected.pill_color -ne 6) {
        throw "inspect endpoint did not rebuild the run-specific pill mapping"
    }

    $GenericBody = @{
        card_ids = @(12)
        active_ids = @(639)
        passive_ids = @(393)
        bombs_min = 1
        bombs_max = 1
        red_hearts_min = 2
        red_hearts_max = 2
        damage_min = 2.87
        range_min = 7.19
        range_max = 7.20
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

    $TreatmentInspectBody = @{seed_u32 = 20} | ConvertTo-Json -Compress
    $TreatmentInspected = Invoke-RestMethod `
        ($BaseUrl + "api/v1/inspect") `
        -Method Post `
        -ContentType "application/json" `
        -Headers $Headers `
        -Body $TreatmentInspectBody
    if ($TreatmentInspected.passive_id -ne 240 -or
        -not $TreatmentInspected.post_item_stats_available -or
        $TreatmentInspected.experimental_treatment_up_mask -ne 77 -or
        $TreatmentInspected.experimental_treatment_down_mask -ne 34 -or
        [Math]::Abs($TreatmentInspected.post_damage - 5.0035222145) -gt 0.000001) {
        throw "inspect endpoint returned the wrong Experimental Treatment roll"
    }

    $TreatmentBody = @{
        experimental_health = "up"
        experimental_damage = "up"
        experimental_range = "unchanged"
        post_damage_min = 5.0
        post_damage_max = 5.01
        start = 20
        end = 20
        threads = 1
        max_results = 10
    } | ConvertTo-Json -Compress
    Invoke-RestMethod `
        ($BaseUrl + "api/v1/search") `
        -Method Post `
        -ContentType "application/json" `
        -Headers $Headers `
        -Body $TreatmentBody | Out-Null
    do {
        Start-Sleep -Milliseconds 50
        $TreatmentStatus = Invoke-RestMethod ($BaseUrl + "api/v1/search/status")
    } while ($TreatmentStatus.state -eq "running")
    $TreatmentResults = Invoke-RestMethod ($BaseUrl + "api/v1/search/results")
    if ($TreatmentResults.total_count -ne 1 -or $TreatmentResults.matches[0].seed_u32 -ne 20) {
        throw "Experimental Treatment search endpoint returned the wrong seed"
    }

    $BadGasBody = @{
        pill_effect_ids = @(0)
        start = 1
        end = 1000
        threads = 2
        max_results = 10
    } | ConvertTo-Json -Compress
    Invoke-RestMethod `
        ($BaseUrl + "api/v1/search") `
        -Method Post `
        -ContentType "application/json" `
        -Headers $Headers `
        -Body $BadGasBody | Out-Null
    do {
        Start-Sleep -Milliseconds 50
        $BadGasStatus = Invoke-RestMethod ($BaseUrl + "api/v1/search/status")
    } while ($BadGasStatus.state -eq "running")
    $BadGasResults = Invoke-RestMethod ($BaseUrl + "api/v1/search/results")
    if ($BadGasResults.total_count -ne 2 -or $BadGasResults.matches[0].seed_u32 -ne 791 -or
        $BadGasResults.matches[0].pocket_id -ne 0 -or $BadGasResults.matches[0].pill_color -ne 8) {
        throw "pill effect zero search returned the wrong run-specific mapping"
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
