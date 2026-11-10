# SPDX-FileCopyrightText: BANANAPIE LIMITED
# SPDX-License-Identifier: GPL-2.0-or-later
# See LICENSES/GPL-2.0-or-later.txt for the full license text.

param(
    [Parameter(Mandatory)][string]$ObsRoot,
    [string]$PluginDll = (Join-Path $PSScriptRoot "..\..\build\artifacts\bin\Release\ciciobsformattest.dll"),
    [string]$InputFrame = (Join-Path $PSScriptRoot "format-test-frame-1280x720.bgra"),
    [string]$BaseCollection = ([regex]::Unescape("\u672a\u547d\u540d")),
    [string]$BaseSource = ([regex]::Unescape("\u6a2a\u5c4f")),
    [string]$WorkRoot = (Join-Path $PSScriptRoot "..\..\build-format-preflight"),
    [string]$DefaultSettingPath = "",
    [int]$ReadyTimeoutSeconds = 60,
    [int]$FormatWarmupMilliseconds = 2500,
    [switch]$AttachCiciFilter,
    [switch]$UseMainThreadFilterControl,
    [switch]$SkipUiAutomation,
    [switch]$KeepObsOpen
)

$ErrorActionPreference = "Stop"

$formats = @(
    "I420", "NV12", "YVYU", "YUY2", "UYVY", "RGBA", "BGRA", "BGRX",
    "Y800", "I444", "BGR3", "I422", "I40A", "I42A", "YUVA", "AYUV",
    "I010", "P010", "I210", "I412", "YA2L", "P216", "P416", "V210",
    "R10L")
$supportedFormats = @($formats)
$obsExe = Join-Path $ObsRoot "bin\64bit\obs64.exe"
$obsWorkingDirectory = Split-Path -Parent $obsExe
$sceneRoot = Join-Path $env:APPDATA "obs-studio\basic\scenes"
$baseCollectionPath = Join-Path $sceneRoot "$BaseCollection.json"
$testCollectionName = "CiCiFormatTest"
$testCollectionPath = Join-Path $sceneRoot "$testCollectionName.json"
$pluginInstallDirectory =
    Join-Path $env:ProgramData "obs-studio\plugins\ciciobsformattest\bin\64bit"
$pluginInstallPath = Join-Path $pluginInstallDirectory "ciciobsformattest.dll"
$controlPath = Join-Path $WorkRoot "control.txt"
$statusPath = Join-Path $WorkRoot "status.txt"
$filterControlPath = Join-Path $WorkRoot "filter-control.txt"
$filterStatusPath = Join-Path $WorkRoot "filter-status.txt"
$reportPath = Join-Path $WorkRoot "preflight-results.json"
$runStartedAt = Get-Date
$results = [System.Collections.Generic.List[object]]::new()
$ciciAttachStatus = if ($AttachCiciFilter) { "PENDING" } else { "NOT_REQUESTED" }
$testSourceUuid = ""
$temporaryCiciConfigPaths = @()
$script:FilterControlSequence = 0

Add-Type -AssemblyName UIAutomationClient
Add-Type -AssemblyName UIAutomationTypes
Add-Type @"
using System;
using System.Runtime.InteropServices;

public static class CiciFormatTestInput {
    [StructLayout(LayoutKind.Sequential)]
    public struct POINT {
        public int X;
        public int Y;
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct RECT {
        public int Left;
        public int Top;
        public int Right;
        public int Bottom;
    }

    [DllImport("user32.dll")]
    public static extern bool SetCursorPos(int x, int y);

    [DllImport("user32.dll")]
    public static extern bool SetForegroundWindow(IntPtr window);

    [DllImport("user32.dll")]
    public static extern IntPtr WindowFromPoint(POINT point);

    [DllImport("user32.dll")]
    public static extern bool ScreenToClient(IntPtr window, ref POINT point);

    [DllImport("user32.dll")]
    public static extern bool PostMessage(IntPtr window, uint message, IntPtr wParam, IntPtr lParam);

    [DllImport("user32.dll")]
    public static extern bool GetWindowRect(IntPtr window, out RECT rectangle);

    [DllImport("user32.dll")]
    public static extern void mouse_event(uint flags, uint dx, uint dy, uint data, UIntPtr extraInfo);
}
"@

function Copy-JsonValue {
    param([Parameter(Mandatory)]$Value)
    return ConvertFrom-Json -InputObject ($Value | ConvertTo-Json -Depth 100)
}

function Write-JsonFile {
    param(
        [Parameter(Mandatory)]$Value,
        [Parameter(Mandatory)][string]$Path
    )
    $json = $Value | ConvertTo-Json -Depth 100
    [System.IO.File]::WriteAllText(
        $Path,
        $json,
        [System.Text.UTF8Encoding]::new($false))
}

function New-TestCollection {
    if (-not (Test-Path -LiteralPath $baseCollectionPath -PathType Leaf)) {
        throw "OBS scene collection not found: $baseCollectionPath"
    }
    $collection = ConvertFrom-Json -InputObject (
        [System.IO.File]::ReadAllText($baseCollectionPath))
    $collection.name = $testCollectionName

    $base = $collection.sources |
        Where-Object { $_.name -eq $BaseSource } |
        Select-Object -First 1
    if ($null -eq $base) {
        throw "Base source '$BaseSource' was not found in $baseCollectionPath"
    }
    $scene = $collection.sources |
        Where-Object { $_.id -eq "scene" -and $_.name -eq $collection.current_scene } |
        Select-Object -First 1
    if ($null -eq $scene) {
        $scene = $collection.sources |
            Where-Object { $_.id -eq "scene" } |
            Select-Object -First 1
    }
    if ($null -eq $scene) {
        throw "No OBS scene source was found in $baseCollectionPath"
    }
    $baseItem = $scene.settings.items |
        Where-Object { $_.source_uuid -eq $base.uuid } |
        Select-Object -First 1
    if ($null -eq $baseItem) {
        throw "Scene item for '$BaseSource' was not found"
    }

    $sourceUuid = [Guid]::NewGuid().ToString()
    $testSource = Copy-JsonValue -Value $base
    $testSource.name = ([regex]::Unescape("\u6a2a\u5c4f-\u683c\u5f0f\u6d4b\u8bd5"))
    $testSource.uuid = $sourceUuid
    $testSource.id = "cici_obs_format_test_source"
    $testSource.versioned_id = "cici_obs_format_test_source"
    $testSource.settings = [pscustomobject]@{
        format = "NV12"
        padding = 64
        full_range = $false
        flip = $false
        frame_path = ""
    }
    $testSource.mixers = 0
    # A persisted CiCi filter is created while OBS is still loading the scene,
    # which makes initialization timing indistinguishable from a format fault.
    # The optional CiCi path attaches the filter after Loaded scenes instead.
    $testSource.filters = @()

    $testItem = Copy-JsonValue -Value $baseItem
    $testItem.name = $testSource.name
    $testItem.source_uuid = $sourceUuid
    $testItem.visible = $true
    $testItem.rot = 0.0
    $testItem.pos.x = 0.0
    $testItem.pos.y = 0.0
    $testItem.scale.x = 1.0
    $testItem.scale.y = 1.0
    $testItem.bounds_type = 2
    $testItem.bounds_align = 0
    $testItem.bounds.x = 1920.0
    $testItem.bounds.y = 1080.0
    foreach ($relativeProperty in @(
        "scale_ref", "pos_rel", "scale_rel", "bounds_rel")) {
        [void]$testItem.PSObject.Properties.Remove($relativeProperty)
    }
    $testItem.id = 1

    $testScene = Copy-JsonValue -Value $scene
    $testScene.uuid = [Guid]::NewGuid().ToString()
    $testScene.settings.items = @($testItem)
    $testScene.settings.id_counter = 1
    $testScene.hotkeys = [pscustomobject]@{}

    $collection.sources = @($testScene, $testSource)
    $collection.scene_order = @([pscustomobject]@{ name = $testScene.name })
    $collection.current_scene = $testScene.name
    $collection.current_program_scene = $testScene.name
    $collection.groups = @()
    Write-JsonFile -Value $collection -Path $testCollectionPath
    $script:testSourceUuid = $sourceUuid
    return $collection.current_scene
}

function New-TemporaryCiciConfigs {
    $configDirectories = @(Get-ChildItem `
        -LiteralPath (Join-Path $env:LOCALAPPDATA "cicibeauty\cache\local\users") `
        -Directory -ErrorAction SilentlyContinue |
        ForEach-Object { Join-Path $_.FullName "filterconfigs" } |
        Where-Object { Test-Path -LiteralPath $_ -PathType Container })
    if ($configDirectories.Count -eq 0) {
        throw "No logged-in CiCi user configuration directory was found"
    }

    $config = ConvertFrom-Json -InputObject (
        [System.IO.File]::ReadAllText($DefaultSettingPath))
    $config.basic.enableFacePoints = $true
    $config.beauty.enableBeauty = $true
    $config.beauty.solutions[0].enableFaceShaping = $true
    $config.beauty.solutions[0] | Add-Member -NotePropertyName slimFace `
        -NotePropertyValue 100 -Force
    $config.beauty.solutions[0] | Add-Member -NotePropertyName bigEye `
        -NotePropertyValue 80 -Force

    $paths = @()
    foreach ($directory in $configDirectories) {
        $path = Join-Path $directory "$testSourceUuid.json"
        Write-JsonFile -Value $config -Path $path
        $paths += $path
    }
    return $paths
}

function Wait-FormatReady {
    param(
        [Parameter(Mandatory)][int]$Sequence,
        [Parameter(Mandatory)][string]$Format,
        [Parameter(Mandatory)][System.Diagnostics.Process]$ObsProcess
    )
    $deadline = (Get-Date).AddSeconds($ReadyTimeoutSeconds)
    do {
        if ($ObsProcess.HasExited) {
            throw "OBS exited with code $($ObsProcess.ExitCode) while testing $Format"
        }
        if (Test-Path -LiteralPath $statusPath -PathType Leaf) {
            try {
                $status = (Read-SharedTextFile -Path $statusPath).Trim()
                $tokens = $status -split '\s+', 7
                if ($tokens.Count -ge 6 -and [int]$tokens[0] -eq $Sequence) {
                    if ($tokens[1] -ne $Format) {
                        throw "Status format mismatch: expected $Format, received $($tokens[1])"
                    }
                    if ($tokens[5] -ne "READY") {
                        throw "Test source returned: $status"
                    }
                    return $status
                }
            } catch [System.IO.IOException] {
            }
        }
        Start-Sleep -Milliseconds 100
    } while ((Get-Date) -lt $deadline)
    throw "Timed out waiting for test source READY: sequence=$Sequence format=$Format"
}

function Wait-ObsSceneLoaded {
    param([Parameter(Mandatory)][System.Diagnostics.Process]$ObsProcess)

    $deadline = (Get-Date).AddSeconds(120)
    $logRoot = Join-Path $env:APPDATA "obs-studio\logs"
    do {
        if ($ObsProcess.HasExited) {
            throw "OBS exited with code $($ObsProcess.ExitCode) while loading the test scene"
        }
        $log = Get-ChildItem -LiteralPath $logRoot -Filter "*.txt" `
            -ErrorAction SilentlyContinue |
            Where-Object { $_.LastWriteTime -ge $runStartedAt } |
            Sort-Object LastWriteTime -Descending |
            Select-Object -First 1
        if ($null -ne $log) {
            try {
                $stream = [System.IO.File]::Open(
                    $log.FullName,
                    [System.IO.FileMode]::Open,
                    [System.IO.FileAccess]::Read,
                    [System.IO.FileShare]::ReadWrite -bor
                        [System.IO.FileShare]::Delete)
                try {
                    $reader = [System.IO.StreamReader]::new(
                        $stream,
                        [System.Text.Encoding]::UTF8,
                        $true,
                        4096,
                        $true)
                    try {
                        $contents = $reader.ReadToEnd()
                    } finally {
                        $reader.Dispose()
                    }
                } finally {
                    $stream.Dispose()
                }
                if ($contents.Contains("Loaded scenes:")) {
                    return
                }
            } catch [System.IO.IOException] {
            }
        }
        Start-Sleep -Milliseconds 200
    } while ((Get-Date) -lt $deadline)
    throw "Timed out waiting for OBS to finish loading the test scene"
}

function Get-ProcessElements {
    param(
        [Parameter(Mandatory)][int]$ProcessId,
        [Windows.Automation.ControlType]$ControlType
    )

    $processCondition = [Windows.Automation.PropertyCondition]::new(
        [Windows.Automation.AutomationElement]::ProcessIdProperty,
        $ProcessId)
    $processWindow = [Windows.Automation.AutomationElement]::RootElement.FindAll(
        [Windows.Automation.TreeScope]::Children,
        $processCondition) |
        Where-Object { $_.Current.ClassName -eq "OBSBasic" } |
        Select-Object -First 1
    if ($null -eq $processWindow) {
        return @()
    }
    if ($null -eq $ControlType) {
        return $processWindow.FindAll(
            [Windows.Automation.TreeScope]::Descendants,
            [Windows.Automation.Condition]::TrueCondition)
    }
    $typeCondition = [Windows.Automation.PropertyCondition]::new(
        [Windows.Automation.AutomationElement]::ControlTypeProperty,
        $ControlType)
    return $processWindow.FindAll(
        [Windows.Automation.TreeScope]::Descendants,
        $typeCondition)
}

function Get-ProcessWindows {
    param([Parameter(Mandatory)][int]$ProcessId)

    $processCondition = [Windows.Automation.PropertyCondition]::new(
        [Windows.Automation.AutomationElement]::ProcessIdProperty,
        $ProcessId)
    return [Windows.Automation.AutomationElement]::RootElement.FindAll(
        [Windows.Automation.TreeScope]::Children,
        $processCondition)
}

function Get-ObsMainWindowHandle {
    param([Parameter(Mandatory)][System.Diagnostics.Process]$ObsProcess)

    $ObsProcess.Refresh()
    if ($ObsProcess.MainWindowHandle -ne [IntPtr]::Zero) {
        return $ObsProcess.MainWindowHandle
    }

    $window = Get-ProcessWindows -ProcessId $ObsProcess.Id |
        Where-Object {
            $_.Current.ClassName -eq "OBSBasic" -and
            -not $_.Current.IsOffscreen
        } |
        Select-Object -First 1
    if ($null -eq $window -or $window.Current.NativeWindowHandle -eq 0) {
        return [IntPtr]::Zero
    }
    return [IntPtr]$window.Current.NativeWindowHandle
}

function Invoke-PhysicalPointClick {
    param(
        [Parameter(Mandatory)][int]$X,
        [Parameter(Mandatory)][int]$Y
    )

    $point = [CiciFormatTestInput+POINT]::new()
    $point.X = $X
    $point.Y = $Y
    $window = [CiciFormatTestInput]::WindowFromPoint($point)
    if ($window -eq [IntPtr]::Zero) {
        throw "No native window exists at click point $X,$Y"
    }
    [void][CiciFormatTestInput]::ScreenToClient($window, [ref]$point)
    $lParam = [IntPtr](($point.Y -shl 16) -bor ($point.X -band 0xffff))
    [void][CiciFormatTestInput]::SetCursorPos($X, $Y)
    [void][CiciFormatTestInput]::PostMessage(
        $window, 0x0201, [IntPtr]1, $lParam)
    Start-Sleep -Milliseconds 40
    [void][CiciFormatTestInput]::PostMessage(
        $window, 0x0202, [IntPtr]::Zero, $lParam)
    Start-Sleep -Milliseconds 300
}

function Invoke-PhysicalClick {
    param([Parameter(Mandatory)][Windows.Automation.AutomationElement]$Element)

    $rectangle = $Element.Current.BoundingRectangle
    if ($rectangle.Width -le 0 -or $rectangle.Height -le 0) {
        throw "Cannot click an element with an empty rectangle"
    }
    Invoke-PhysicalPointClick `
        -X ([int]($rectangle.X + $rectangle.Width / 2)) `
        -Y ([int]($rectangle.Y + $rectangle.Height / 2))
}

function Invoke-NativeMouseClick {
    param([Parameter(Mandatory)][Windows.Automation.AutomationElement]$Element)

    $rectangle = $Element.Current.BoundingRectangle
    if ($rectangle.Width -le 0 -or $rectangle.Height -le 0) {
        throw "Cannot click an element with an empty rectangle"
    }
    $x = [int]($rectangle.X + $rectangle.Width / 2)
    $y = [int]($rectangle.Y + $rectangle.Height / 2)
    [void][CiciFormatTestInput]::SetCursorPos($x, $y)
    Start-Sleep -Milliseconds 100
    [CiciFormatTestInput]::mouse_event(0x0002, 0, 0, 0, [UIntPtr]::Zero)
    Start-Sleep -Milliseconds 40
    [CiciFormatTestInput]::mouse_event(0x0004, 0, 0, 0, [UIntPtr]::Zero)
    Start-Sleep -Milliseconds 300
}

function Invoke-AccessibilityClick {
    param([Parameter(Mandatory)][Windows.Automation.AutomationElement]$Element)

    $Element.GetCurrentPattern(
        [Windows.Automation.InvokePattern]::Pattern).Invoke()
}

function Invoke-UiElement {
    param([Parameter(Mandatory)][Windows.Automation.AutomationElement]$Element)

    # Qt's UIA InvokePattern runs the click through a synchronous accessibility
    # call. MRE initialization from that call chain can stall, while the same
    # button activated by normal mouse messages initializes correctly.
    Invoke-PhysicalClick -Element $Element
}

function Read-SharedTextFile {
    param([Parameter(Mandatory)][string]$Path)

    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        return ""
    }
    $stream = [System.IO.File]::Open(
        $Path,
        [System.IO.FileMode]::Open,
        [System.IO.FileAccess]::Read,
        [System.IO.FileShare]::ReadWrite -bor [System.IO.FileShare]::Delete)
    try {
        $reader = [System.IO.StreamReader]::new(
            $stream,
            [System.Text.Encoding]::UTF8,
            $true,
            4096,
            $true)
        try {
            return $reader.ReadToEnd()
        } finally {
            $reader.Dispose()
        }
    } finally {
        $stream.Dispose()
    }
}

function Set-CiciFilterOnMainThread {
    param(
        [Parameter(Mandatory)][string]$SourceName,
        [Parameter(Mandatory)][bool]$Applied,
        [Parameter(Mandatory)][System.Diagnostics.Process]$ObsProcess
    )

    $script:FilterControlSequence++
    $sequence = $script:FilterControlSequence
    $action = if ($Applied) { "APPLY" } else { "REMOVE" }
    $command = "$sequence`t$action`t$SourceName`tCiCi Beauty"
    [System.IO.File]::WriteAllText(
        $filterControlPath,
        $command,
        [System.Text.UTF8Encoding]::new($false))

    $deadline = (Get-Date).AddSeconds(30)
    do {
        if ($ObsProcess.HasExited) {
            throw "OBS exited while waiting for the UI-task filter command"
        }
        $status = (Read-SharedTextFile -Path $filterStatusPath).Trim()
        if ($status) {
            $fields = $status -split "`t", 3
            if ($fields.Count -ge 2 -and
                [uint64]$fields[0] -eq $sequence) {
                if ($fields[1] -ne "OK") {
                    $detail = if ($fields.Count -ge 3) {
                        $fields[2]
                    } else {
                        "unknown error"
                    }
                    throw "UI-task filter command failed: $detail"
                }
                return
            }
        }
        Start-Sleep -Milliseconds 100
    } while ((Get-Date) -lt $deadline)
    throw "Timed out waiting for UI-task filter command sequence $sequence"
}

function Wait-CiciFormatDisposition {
    param(
        [Parameter(Mandatory)][string]$Format,
        [Parameter(Mandatory)][System.Diagnostics.Process]$ObsProcess
    )

    $logRoot = Join-Path $env:LOCALAPPDATA "cicibeauty\logs"
    $pidMarker = "[PID $($ObsProcess.Id)]"
    $deadline = (Get-Date).AddSeconds(15)
    $processedSeenAt = $null
    do {
        if ($ObsProcess.HasExited) {
            throw "OBS exited while classifying $Format"
        }
        try {
            $lines = @(Get-ChildItem -LiteralPath $logRoot -Filter "ciciOBS*.log" `
                -File -ErrorAction SilentlyContinue |
                Sort-Object LastWriteTime -Descending |
                Select-Object -First 3 |
                ForEach-Object { Read-SharedTextFile -Path $_.FullName } |
                ForEach-Object { $_ -split "`r?`n" } |
                Where-Object { $_.Contains($pidMarker) })
            if ($lines | Where-Object {
                $_ -match "skip MRE render for unsupported OBS output format:\s*$Format\s*$"
            }) {
                return "BYPASS"
            }
            if ($lines | Where-Object {
                $_ -match "unsupported OBS frame format for MR_ImageInfo:\s*$Format\s*$"
            }) {
                return "RENDER_FAIL"
            }
            if ($lines | Where-Object {
                $_ -match "obsFormat_:\s*$Format\s+\("
            }) {
                if ($null -eq $processedSeenAt) {
                    $processedSeenAt = Get-Date
                } elseif (((Get-Date) - $processedSeenAt).TotalMilliseconds -ge 500) {
                    return "PASS"
                }
            }
        } catch [System.IO.IOException] {
        }
        Start-Sleep -Milliseconds 100
    } while ((Get-Date) -lt $deadline)
    throw "CiCi did not log whether $Format was processed or bypassed"
}

function Save-ObsWindowScreenshot {
    param(
        [Parameter(Mandatory)][System.Diagnostics.Process]$ObsProcess,
        [Parameter(Mandatory)][string]$Path
    )

    Add-Type -AssemblyName System.Drawing
    $handle = Get-ObsMainWindowHandle -ObsProcess $ObsProcess
    $rectangle = [CiciFormatTestInput+RECT]::new()
    if ($handle -eq [IntPtr]::Zero -or
        -not [CiciFormatTestInput]::GetWindowRect($handle, [ref]$rectangle)) {
        throw "Could not resolve the OBS window rectangle"
    }
    [void][CiciFormatTestInput]::SetForegroundWindow($handle)
    Start-Sleep -Milliseconds 150
    $width = $rectangle.Right - $rectangle.Left
    $height = $rectangle.Bottom - $rectangle.Top
    if ($width -le 0 -or $height -le 0) {
        throw "OBS returned an empty window rectangle"
    }

    $bitmap = [System.Drawing.Bitmap]::new($width, $height)
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    try {
        $graphics.CopyFromScreen($rectangle.Left, $rectangle.Top, 0, 0,
            [System.Drawing.Size]::new($width, $height))
        $bitmap.Save($Path, [System.Drawing.Imaging.ImageFormat]::Png)
    } finally {
        $graphics.Dispose()
        $bitmap.Dispose()
    }
}

function Dismiss-CiciUpdateWindow {
    param([Parameter(Mandatory)][System.Diagnostics.Process]$ObsProcess)

    [void][CiciFormatTestInput]::SetForegroundWindow(
        (Get-ObsMainWindowHandle -ObsProcess $ObsProcess))
    $deadline = (Get-Date).AddSeconds(5)
    do {
        if ($ObsProcess.HasExited) {
            throw "OBS exited while checking the CiCi update window"
        }
        $updateWindow = Get-ProcessWindows -ProcessId $ObsProcess.Id |
            Where-Object {
                $_.Current.Name -match
                    ([regex]::Unescape("Cici Beauty.*(\u66f4\u65b0|Update)|Update.*Cici Beauty")) -and
                -not $_.Current.IsOffscreen
            } |
            Select-Object -First 1
        if ($null -ne $updateWindow) {
            $rectangle = $updateWindow.Current.BoundingRectangle
            # The custom update dialog does not expose its buttons to UIA.
            # Cancel is the stable bottom-right action in this fixed layout.
            Invoke-PhysicalPointClick `
                -X ([int]($rectangle.Right - 42)) `
                -Y ([int]($rectangle.Bottom - 32))
            return
        }
        Start-Sleep -Milliseconds 200
    } while ((Get-Date) -lt $deadline)
}

function Select-CiciTestSource {
    param([Parameter(Mandatory)][System.Diagnostics.Process]$ObsProcess)

    $deadline = (Get-Date).AddSeconds(30)
    $nextSettingsRequest = [datetime]::MinValue
    do {
        if ($ObsProcess.HasExited) {
            throw "OBS exited while opening the CiCi settings"
        }
        $elements = Get-ProcessElements -ProcessId $ObsProcess.Id
        $combo = $elements |
            Where-Object {
                $_.Current.ControlType -eq
                    [Windows.Automation.ControlType]::ComboBox -and
                $_.Current.AutomationId -like "*SettingController.QComboBox" -and
                -not $_.Current.IsOffscreen
            } |
            Select-Object -First 1
        if ($null -eq $combo) {
            $settings = $elements |
                Where-Object {
                    ($_.Current.ControlType -eq
                        [Windows.Automation.ControlType]::Window -or
                     $_.Current.ControlType -eq
                        [Windows.Automation.ControlType]::Pane) -and
                    $_.Current.Name -match "^Cici Beauty V" -and
                    -not $_.Current.IsOffscreen
                } |
                Select-Object -First 1
            if ($null -ne $settings) {
                $windowRect = $settings.Current.BoundingRectangle
                $combo = $elements |
                    Where-Object {
                        $rect = $_.Current.BoundingRectangle
                        $_.Current.ControlType -eq
                            [Windows.Automation.ControlType]::ComboBox -and
                        $_.Current.ClassName -eq "QComboBox" -and
                        -not $_.Current.IsOffscreen -and
                        $rect.X -ge $windowRect.X -and
                        $rect.Right -le $windowRect.Right -and
                        $rect.Y -ge $windowRect.Y -and
                        $rect.Bottom -le ($windowRect.Y + 90)
                    } |
                    Select-Object -First 1
            }
        }
        if ($null -ne $combo) {
            break
        }

        Dismiss-CiciUpdateWindow -ObsProcess $ObsProcess
        if ((Get-Date) -ge $nextSettingsRequest) {
            $ciciButton = $elements |
                Where-Object {
                    $_.Current.ControlType -eq
                        [Windows.Automation.ControlType]::Button -and
                    $_.Current.Name -match "^CiCi(?:[\s,]|$)|^Cici(?:[\s,]|$)" -and
                    -not $_.Current.IsOffscreen
                } |
                Select-Object -First 1
            if ($null -ne $ciciButton) {
                Invoke-UiElement -Element $ciciButton
            }
            $nextSettingsRequest = (Get-Date).AddSeconds(2)
        }
        Start-Sleep -Milliseconds 300
    } while ((Get-Date) -lt $deadline)
    if ($null -eq $combo) {
        throw "CiCi source selector was not found"
    }

    $getCurrentSelection = {
        param([Windows.Automation.AutomationElement]$SourceCombo)
        $comboRect = $SourceCombo.Current.BoundingRectangle
        $text = Get-ProcessElements `
            -ProcessId $ObsProcess.Id `
            -ControlType ([Windows.Automation.ControlType]::Text) |
            Where-Object {
                $rect = $_.Current.BoundingRectangle
                -not $_.Current.IsOffscreen -and
                -not [string]::IsNullOrWhiteSpace($_.Current.Name) -and
                $rect.X -ge $comboRect.X -and
                $rect.Right -le $comboRect.Right -and
                $rect.Y -ge $comboRect.Y -and
                $rect.Bottom -le $comboRect.Bottom
            } |
            Select-Object -First 1
        if ($null -ne $text) {
            return $text.Current.Name
        }
        try {
            return $SourceCombo.GetCurrentPattern(
                [Windows.Automation.ValuePattern]::Pattern).Current.Value
        } catch {
            return ""
        }
    }

    $targetName = ([regex]::Unescape("\u6a2a\u5c4f-\u683c\u5f0f\u6d4b\u8bd5"))
    if ((& $getCurrentSelection $combo) -eq $targetName) {
        return
    }

    for ($attempt = 1; $attempt -le 4; ++$attempt) {
        try {
            $combo.GetCurrentPattern(
                [Windows.Automation.ExpandCollapsePattern]::Pattern).Expand()
        } catch {
            Invoke-PhysicalClick -Element $combo
        }
        Start-Sleep -Milliseconds 300
        $comboRect = $combo.Current.BoundingRectangle
        $item = Get-ProcessElements `
            -ProcessId $ObsProcess.Id `
            -ControlType ([Windows.Automation.ControlType]::ListItem) |
            Where-Object {
                $rect = $_.Current.BoundingRectangle
                $_.Current.Name -eq $targetName -and
                -not $_.Current.IsOffscreen -and
                $rect.X -ge ($comboRect.X - 5) -and
                $rect.Right -le ($comboRect.Right + 30) -and
                $rect.Y -ge ($comboRect.Bottom - 5) -and
                $rect.Y -le ($comboRect.Bottom + 250)
            } |
            Select-Object -First 1
        if ($null -eq $item) {
            Invoke-PhysicalClick -Element $combo
            Start-Sleep -Milliseconds 250
            continue
        }

        if ($attempt -eq 1) {
            try {
                $item.GetCurrentPattern(
                    [Windows.Automation.SelectionItemPattern]::Pattern).Select()
            } catch {
                Invoke-PhysicalClick -Element $item
            }
        } else {
            Invoke-PhysicalClick -Element $item
        }

        $selectionDeadline = (Get-Date).AddSeconds(2)
        do {
            Start-Sleep -Milliseconds 150
            if ((& $getCurrentSelection $combo) -eq $targetName) {
                return
            }
        } while ((Get-Date) -lt $selectionDeadline)
    }

    throw "CiCi source selector remained at '$(& $getCurrentSelection $combo)' instead of '$targetName' after 4 attempts"
}

function Wait-CiciFilterReady {
    param(
        [Parameter(Mandatory)][System.Diagnostics.Process]$ObsProcess,
        [int]$TimeoutSeconds = 120
    )

    $logRoot = Join-Path $env:LOCALAPPDATA "cicibeauty\logs"
    $crashRoot = Join-Path $env:APPDATA "obs-studio\crashes"
    $pidMarker = "[PID $($ObsProcess.Id)]"
    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    do {
        if ($ObsProcess.HasExited) {
            throw "OBS exited while initializing the CiCi filter"
        }
        $newCrash = Get-ChildItem -LiteralPath $crashRoot -Filter "*.txt" `
            -ErrorAction SilentlyContinue |
            Where-Object { $_.LastWriteTime -ge $runStartedAt } |
            Sort-Object LastWriteTime -Descending |
            Select-Object -First 1
        if ($null -ne $newCrash) {
            throw "OBS crashed while initializing the CiCi filter: $($newCrash.FullName)"
        }
        try {
            $contents = @(Get-ChildItem -LiteralPath $logRoot -Filter "ciciOBS*.log" `
                -File -ErrorAction SilentlyContinue |
                Sort-Object LastWriteTime -Descending |
                Select-Object -First 3 |
                ForEach-Object { Read-SharedTextFile -Path $_.FullName }) -join "`n"
            $pidLines = @($contents -split "`r?`n" |
                Where-Object { $_.Contains($pidMarker) })
            $created = $pidLines.Where({
                $_.Contains("CiciBeautyFilterInstance created")
            }).Count -gt 0
            $added = $pidLines.Where({
                $_.Contains("CiciBeautyFilterInstance::onFilterAdd")
            }).Count -gt 0
            $renderReady = $pidLines.Where({
                $_.Contains(
                    "callback=CiciBeautyApp2::onFilterVideo")
            }).Count -gt 0
            if ($created -and $added -and $renderReady) {
                return
            }
        } catch [System.IO.IOException] {
        }
        Start-Sleep -Milliseconds 250
    } while ((Get-Date) -lt $deadline)
    throw "Timed out waiting for the CiCi filter to process its first frame (created=$created, added=$added, renderReady=$renderReady)"
}

function Add-CiciFilterAfterStartup {
    param([Parameter(Mandatory)][System.Diagnostics.Process]$ObsProcess)

    $lastError = $null
    for ($attempt = 1; $attempt -le 4; ++$attempt) {
        Select-CiciTestSource -ObsProcess $ObsProcess
        $button = Get-ProcessElements `
            -ProcessId $ObsProcess.Id `
            -ControlType ([Windows.Automation.ControlType]::Button) |
            Where-Object {
                $_.Current.Name -in @(([regex]::Unescape("\u5e94\u7528Cici")), "Apply Cici") -and
                -not $_.Current.IsOffscreen
            } |
            Select-Object -First 1

        if ($null -ne $button) {
            if ($attempt -eq 1) {
                Invoke-UiElement -Element $button
            } elseif ($attempt -eq 2) {
                Invoke-AccessibilityClick -Element $button
            } else {
                Invoke-NativeMouseClick -Element $button
            }
        } elseif ($attempt -eq 1) {
            throw "CiCi Apply button was not found"
        }

        try {
            Wait-CiciFilterReady -ObsProcess $ObsProcess -TimeoutSeconds 20
            return
        } catch {
            $lastError = $_
        }
    }
    throw "CiCi Apply did not create a render-ready filter after 4 attempts: $($lastError.Exception.Message)"
}

if (-not (Test-Path -LiteralPath $obsExe -PathType Leaf)) {
    throw "OBS executable not found: $obsExe"
}
if ($UseMainThreadFilterControl -and -not $AttachCiciFilter) {
    throw "-UseMainThreadFilterControl requires -AttachCiciFilter"
}
if ($AttachCiciFilter -and
    ([string]::IsNullOrWhiteSpace($DefaultSettingPath) -or
     -not (Test-Path -LiteralPath $DefaultSettingPath -PathType Leaf))) {
    throw "-AttachCiciFilter requires -DefaultSettingPath pointing to the installed runtime's defaultsetting.json"
}
if (-not (Test-Path -LiteralPath $PluginDll -PathType Leaf)) {
    throw "Format test plugin not found: $PluginDll"
}
if (-not (Test-Path -LiteralPath $InputFrame -PathType Leaf)) {
    throw "Format test frame not found: $InputFrame"
}
$bgraFrameBytes = 1280 * 720 * 4
$inputBytes = (Get-Item -LiteralPath $InputFrame).Length
if ($inputBytes -lt $bgraFrameBytes -or
    $inputBytes % $bgraFrameBytes -ne 0) {
    throw "Format test input must contain one or more 1280x720 BGRA frames"
}

function Wait-ObsSourceScreenshot {
    param(
        [Parameter(Mandatory)][System.Diagnostics.Process]$ObsProcess,
        [Parameter(Mandatory)][string]$BasePath
    )

    $deadline = (Get-Date).AddSeconds(15)
    do {
        if ($ObsProcess.HasExited) {
            throw "OBS exited while waiting for source screenshot"
        }
        $screenshot = Get-ChildItem `
            -LiteralPath (Split-Path -Parent $BasePath) `
            -Filter "$(Split-Path -Leaf $BasePath).*" `
            -File `
            -ErrorAction SilentlyContinue |
            Select-Object -First 1
        if ($null -ne $screenshot -and $screenshot.Length -gt 0) {
            return $screenshot.FullName
        }
        Start-Sleep -Milliseconds 100
    } while ((Get-Date) -lt $deadline)
    throw "OBS did not produce source screenshot: $BasePath"
}
$inputFrameCount = [int64]($inputBytes / $bgraFrameBytes)
if (Get-Process -Name obs64 -ErrorAction SilentlyContinue) {
    throw "Close OBS before running the format-source preflight"
}

[void][System.IO.Directory]::CreateDirectory($WorkRoot)
[void][System.IO.Directory]::CreateDirectory((Join-Path $WorkRoot "screenshots"))
[void][System.IO.Directory]::CreateDirectory($pluginInstallDirectory)
Copy-Item -LiteralPath $PluginDll -Destination $pluginInstallPath -Force
[void](New-TestCollection)
if ($AttachCiciFilter) {
    $temporaryCiciConfigPaths = @(New-TemporaryCiciConfigs)
}
Set-Content -LiteralPath $controlPath -Value "0 NV12 64 0 0" -Encoding ascii
Remove-Item -LiteralPath $statusPath -Force -ErrorAction SilentlyContinue
Remove-Item -LiteralPath $filterControlPath -Force -ErrorAction SilentlyContinue
Remove-Item -LiteralPath $filterStatusPath -Force -ErrorAction SilentlyContinue
# OBS has used both names for its unclean-shutdown sentinel. The compatibility
# run already asserts that no OBS process exists, so removing these markers is
# safe and avoids a modal 60-second startup delay after an interrupted run.
foreach ($sentinelName in @(".sentinel", "safe_mode")) {
    $sentinelPath = Join-Path $env:APPDATA "obs-studio\$sentinelName"
    $sentinel = Get-Item -LiteralPath $sentinelPath -Force `
        -ErrorAction SilentlyContinue
    if ($null -ne $sentinel) {
        Remove-Item -LiteralPath $sentinel.FullName -Recurse -Force
    }
}

$oldControl = $env:CICI_OBS_FORMAT_TEST_CONTROL
$oldStatus = $env:CICI_OBS_FORMAT_TEST_STATUS
$oldFrame = $env:CICI_OBS_FORMAT_TEST_FRAME
$oldCaptureDirectory = $env:CICI_OBS_FORMAT_TEST_CAPTURE_DIR
$oldFilterControl = $env:CICI_OBS_FILTER_TEST_CONTROL
$oldFilterStatus = $env:CICI_OBS_FILTER_TEST_STATUS
$env:CICI_OBS_FORMAT_TEST_CONTROL = $controlPath
$env:CICI_OBS_FORMAT_TEST_STATUS = $statusPath
$env:CICI_OBS_FORMAT_TEST_FRAME = $InputFrame
$env:CICI_OBS_FORMAT_TEST_CAPTURE_DIR = Join-Path $WorkRoot "screenshots"
$env:CICI_OBS_FILTER_TEST_CONTROL = $filterControlPath
$env:CICI_OBS_FILTER_TEST_STATUS = $filterStatusPath
$obsProcess = $null

try {
    $obsProcess = Start-Process -FilePath $obsExe `
        -WorkingDirectory $obsWorkingDirectory `
        -ArgumentList @(
            "--collection", $testCollectionName,
            "--disable-updater",
            "--disable-shutdown-check") `
        -PassThru
    Write-Host "[setup] OBS started: pid=$($obsProcess.Id)"

    Wait-ObsSceneLoaded -ObsProcess $obsProcess
    Write-Host "[setup] OBS scene loaded"
    if (-not $UseMainThreadFilterControl -and -not $SkipUiAutomation) {
        Dismiss-CiciUpdateWindow -ObsProcess $obsProcess
        Write-Host "[setup] Update window check complete"
    }

    if ($AttachCiciFilter) {
        try {
            if ($UseMainThreadFilterControl) {
                Set-CiciFilterOnMainThread -SourceName ([regex]::Unescape("\u6a2a\u5c4f-\u683c\u5f0f\u6d4b\u8bd5")) `
                    -Applied $true -ObsProcess $obsProcess
                Wait-CiciFilterReady -ObsProcess $obsProcess
            } else {
                Add-CiciFilterAfterStartup -ObsProcess $obsProcess
            }
            $ciciAttachStatus = "READY"
        } catch {
            $ciciAttachStatus = "FAILED: $($_.Exception.Message)"
            throw
        }
    }

    for ($index = 0; $index -lt $formats.Count; ++$index) {
        $sequence = $index + 1
        $format = $formats[$index]
        $screenshotBasePath = Join-Path $WorkRoot (
            "screenshots\{0:D2}-{1}" -f $sequence, $format)
        Get-ChildItem `
            -LiteralPath (Split-Path -Parent $screenshotBasePath) `
            -Filter "$(Split-Path -Leaf $screenshotBasePath).*" `
            -File `
            -ErrorAction SilentlyContinue |
            Remove-Item -Force
        Set-Content -LiteralPath $controlPath `
            -Value "$sequence $format 64 0 0" -Encoding ascii
        $started = Get-Date
        try {
            $status = Wait-FormatReady `
                -Sequence $sequence `
                -Format $format `
                -ObsProcess $obsProcess
            Start-Sleep -Milliseconds 400
            if ($obsProcess.HasExited) {
                throw "OBS exited after accepting $format"
            }
            $disposition = if ($AttachCiciFilter) {
                Wait-CiciFormatDisposition -Format $format -ObsProcess $obsProcess
            } else {
                "SOURCE_READY"
            }
            $expectedDisposition = if (-not $AttachCiciFilter) {
                "SOURCE_READY"
            } elseif ($format -in $supportedFormats) {
                "PASS"
            } else {
                "BYPASS"
            }
            Start-Sleep -Milliseconds $FormatWarmupMilliseconds
            if ($obsProcess.HasExited) {
                throw "OBS exited while rendering $format"
            }
            $screenshotPath = Wait-ObsSourceScreenshot `
                -ObsProcess $obsProcess `
                -BasePath $screenshotBasePath
            $results.Add([pscustomobject]@{
                format = $format
                result = $disposition
                expected_result = $expectedDisposition
                matches_expected = ($disposition -eq $expectedDisposition)
                status = $status
                screenshot = $screenshotPath
                elapsed_ms = [int]((Get-Date) - $started).TotalMilliseconds
            })
            Write-Host "[$sequence/$($formats.Count)] $format $disposition"
        } catch {
            $results.Add([pscustomobject]@{
                format = $format
                result = "FAIL"
                status = $_.Exception.Message
                elapsed_ms = [int]((Get-Date) - $started).TotalMilliseconds
            })
            throw
        }
    }
} finally {
    $report = [pscustomobject]@{
        started_at = $runStartedAt.ToString("o")
        finished_at = (Get-Date).ToString("o")
        obs_version = (Get-Item -LiteralPath $obsExe).VersionInfo.ProductVersion
        attach_cici_filter = [bool]$AttachCiciFilter
        filter_attach_method = if ($UseMainThreadFilterControl) {
            "OBS_TASK_UI"
        } elseif ($AttachCiciFilter) {
            "SETTINGS_UI"
        } else {
            "NONE"
        }
        cici_attach_status = $ciciAttachStatus
        plugin_sha256 = (Get-FileHash -LiteralPath $PluginDll -Algorithm SHA256).Hash
        input_frame_sha256 = (Get-FileHash -LiteralPath $InputFrame -Algorithm SHA256).Hash
        input_frame_count = $inputFrameCount
        format_warmup_ms = $FormatWarmupMilliseconds
        pass_count = @($results | Where-Object result -eq "PASS").Count
        bypass_count = @($results | Where-Object result -eq "BYPASS").Count
        fail_count = @($results | Where-Object {
            $_.result -in @("FAIL", "RENDER_FAIL")
        }).Count
        results = @($results)
    }
    Write-JsonFile -Value $report -Path $reportPath

    if ($null -ne $obsProcess -and -not $obsProcess.HasExited -and -not $KeepObsOpen) {
        [void]$obsProcess.CloseMainWindow()
        if (-not $obsProcess.WaitForExit(15000)) {
            Stop-Process -Id $obsProcess.Id -Force
            [void]$obsProcess.WaitForExit(15000)
        }
    }
    foreach ($path in $temporaryCiciConfigPaths) {
        Remove-Item -LiteralPath $path -Force -ErrorAction SilentlyContinue
    }
    $env:CICI_OBS_FORMAT_TEST_CONTROL = $oldControl
    $env:CICI_OBS_FORMAT_TEST_STATUS = $oldStatus
    $env:CICI_OBS_FORMAT_TEST_FRAME = $oldFrame
    $env:CICI_OBS_FORMAT_TEST_CAPTURE_DIR = $oldCaptureDirectory
    $env:CICI_OBS_FILTER_TEST_CONTROL = $oldFilterControl
    $env:CICI_OBS_FILTER_TEST_STATUS = $oldFilterStatus
}

Write-Host "Preflight complete: $reportPath"
