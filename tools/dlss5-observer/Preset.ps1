function New-FirestormControlPreset {
    param([string]$Text, [string]$Techniques)
    # ReShade 6.8 reads selection from the unnamed root section. Imported presets
    # can also contain legacy [GENERAL] keys; do not count them as duplicate root keys.
    $taskSection = ''
    $taskLines = foreach ($taskLine in ($Text -split '\r?\n')) {
        if ($taskLine -match '^\s*\[([^]]+)\]\s*$') { $taskSection = $Matches[1] }
        if ($taskSection -in @('', 'GENERAL') -and $taskLine -match '^\s*(Techniques|TechniqueSorting)\s*=') { continue }
        $taskLine
    }
    return "Techniques=$Techniques`r`nTechniqueSorting=$Techniques`r`n" + ($taskLines -join "`r`n")
}
