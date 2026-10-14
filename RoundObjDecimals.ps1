[CmdletBinding(SupportsShouldProcess = $true)]
param(
    [string]$AssetsPath = (Join-Path $PSScriptRoot 'Assets')
)

$ErrorActionPreference = 'Stop'
$culture = [System.Globalization.CultureInfo]::InvariantCulture
# A byte-preserving encoding keeps comments, names, BOMs and line endings intact.
$encoding = [System.Text.Encoding]::GetEncoding(28591)
$recordPattern = [regex]'(?m)^(?<prefix>[ \t]*(?:v|vt|vn|vp)[ \t]+)(?<values>[^\r\n#]*)'
$numberPattern = [regex]'(?<!\S)[+-]?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][+-]?\d+)?(?!\S)'

if (-not (Test-Path -LiteralPath $AssetsPath -PathType Container)) {
    throw "Assets directory does not exist: $AssetsPath"
}

$files = @(Get-ChildItem -LiteralPath $AssetsPath -Filter '*.obj' -File -Recurse)
$changedCount = 0
$failedCount = 0

foreach ($file in $files) {
    try {
        $bytes = [System.IO.File]::ReadAllBytes($file.FullName)
        $original = $encoding.GetString($bytes)
        $converted = $recordPattern.Replace($original, [System.Text.RegularExpressions.MatchEvaluator]{
            param($record)
            $values = $numberPattern.Replace($record.Groups['values'].Value, [System.Text.RegularExpressions.MatchEvaluator]{
                param($number)
                $value = [decimal]::Parse($number.Value, [System.Globalization.NumberStyles]::Float, $culture)
                $rounded = [decimal]::Round($value, 6, [System.MidpointRounding]::AwayFromZero)
                return $rounded.ToString('0.######', $culture)
            })
            return $record.Groups['prefix'].Value + $values
        })

        if ($converted -cne $original) {
            if ($PSCmdlet.ShouldProcess($file.FullName, 'Overwrite OBJ with at most 6 decimal places')) {
                [System.IO.File]::WriteAllBytes($file.FullName, $encoding.GetBytes($converted))
                $changedCount++
                Write-Host "Updated: $($file.FullName)"
            }
        }
    }
    catch {
        $failedCount++
        Write-Warning "Failed: $($file.FullName): $($_.Exception.Message)"
    }
}

Write-Host "Scanned: $($files.Count), updated: $changedCount, failed: $failedCount"
if ($failedCount -gt 0) {
    throw "$failedCount OBJ file(s) could not be processed."
}
