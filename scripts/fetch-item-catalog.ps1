param(
    [string]$Python = "python",
    [switch]$FetchOnly,
    [switch]$Force
)

$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
$lockPath = Join-Path $root "data\catalog\sources.lock.json"
$sourceDirectory = Join-Path $root "data\catalog\sources"
$generator = Join-Path $PSScriptRoot "generate-item-catalog.py"
$userAgent = "IsaacSeedSeeker/0.1 (offline catalog snapshot builder)"

if (-not (Get-Command curl.exe -ErrorAction SilentlyContinue)) {
    throw "curl.exe is required to fetch pinned MediaWiki revisions."
}

$lock = Get-Content -Raw -Encoding UTF8 $lockPath | ConvertFrom-Json
New-Item -ItemType Directory -Force $sourceDirectory | Out-Null

foreach ($entry in $lock.sources) {
    $revisionId = [int64]$entry.revision
    $apiUrl = "$($entry.api_url)?action=query&prop=revisions&revids=$revisionId&rvprop=ids%7Ctimestamp%7Csha1%7Ccontent&rvslots=main&formatversion=2&format=json"
    $outputPath = Join-Path $sourceDirectory "huiji-$($entry.key).$revisionId.json"

    if ((-not $Force) -and (Test-Path -LiteralPath $outputPath)) {
        try {
            $existing = [IO.File]::ReadAllText($outputPath, [Text.Encoding]::UTF8) | ConvertFrom-Json
            if (
                $existing.source.key -ne $entry.key -or
                $existing.source.title -ne $entry.title -or
                $existing.source.page_url -ne $entry.page_url -or
                [int64]$existing.source.revision -ne $revisionId -or
                $existing.source.timestamp -ne $entry.timestamp -or
                $existing.source.sha1 -ne $entry.sha1 -or
                $existing.source.license.spdx -ne $entry.license.spdx -or
                $existing.source.license.url -ne $entry.license.url
            ) {
                throw "snapshot metadata does not match sources.lock.json"
            }
            $rendered = $existing | ConvertTo-Json -Depth 100 -Compress
            [IO.File]::WriteAllText($outputPath, $rendered + "`n", [Text.UTF8Encoding]::new($false))
            Write-Host "Reused pinned $($entry.title) revision $revisionId -> $outputPath"
            continue
        }
        catch {
            Write-Warning "Cannot reuse $outputPath ($($_.Exception.Message)); fetching it again."
        }
    }

    $temporaryPath = [IO.Path]::GetTempFileName()

    try {
        & curl.exe --fail --silent --show-error --location `
            --user-agent $userAgent --output $temporaryPath $apiUrl
        if ($LASTEXITCODE -ne 0) {
            throw "curl.exe failed for $($entry.title) with exit code $LASTEXITCODE"
        }

        $response = [IO.File]::ReadAllText($temporaryPath, [Text.Encoding]::UTF8) | ConvertFrom-Json
        if ($response.error) {
            throw "MediaWiki returned an error for $($entry.title): $($response.error.info)"
        }

        $page = $response.query.pages[0]
        $revision = $page.revisions[0]
        $actualTimestamp = ([datetime]$revision.timestamp).ToUniversalTime().ToString(
            "yyyy-MM-ddTHH:mm:ssZ",
            [Globalization.CultureInfo]::InvariantCulture
        )

        if ($page.title -ne $entry.title) {
            throw "Expected title '$($entry.title)', received '$($page.title)'"
        }
        if ([int64]$revision.revid -ne $revisionId) {
            throw "Expected revision $revisionId, received $($revision.revid)"
        }
        if ($revision.sha1 -ne $entry.sha1) {
            throw "SHA-1 mismatch for $($entry.title): expected $($entry.sha1), received $($revision.sha1)"
        }
        if ($actualTimestamp -ne $entry.timestamp) {
            throw "Timestamp mismatch for $($entry.title): expected $($entry.timestamp), received $actualTimestamp"
        }

        $document = $revision.slots.main.content | ConvertFrom-Json
        $snapshot = [ordered]@{
            source = [ordered]@{
                key = $entry.key
                title = $entry.title
                page_url = $entry.page_url
                api_url = $apiUrl
                revision = $revisionId
                timestamp = $actualTimestamp
                sha1 = $revision.sha1
                license = $entry.license
            }
            document = $document
        }
        $rendered = $snapshot | ConvertTo-Json -Depth 100 -Compress
        [IO.File]::WriteAllText($outputPath, $rendered + "`n", [Text.UTF8Encoding]::new($false))
        Write-Host "Fetched $($entry.title) revision $revisionId -> $outputPath"
    }
    finally {
        if (Test-Path -LiteralPath $temporaryPath) {
            Remove-Item -LiteralPath $temporaryPath -Force
        }
    }
}

if (-not $FetchOnly) {
    & $Python $generator
    if ($LASTEXITCODE -ne 0) {
        throw "Catalog generation failed with exit code $LASTEXITCODE"
    }
}
