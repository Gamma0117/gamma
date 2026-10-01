param(
    [string]$SourceDirectory = (Join-Path $PSScriptRoot '..'),
    [string]$ReferenceDirectory = '',
    [string]$RoslynDirectory = ''
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2.0
$sourceFiles = @(Get-ChildItem -LiteralPath $SourceDirectory -Recurse -File |
    Where-Object { $_.Extension -in @('.ps1', '.psm1') })
foreach ($file in $sourceFiles) {
    $tokens = $null; $errors = $null
    $null = [Management.Automation.Language.Parser]::ParseFile($file.FullName, [ref]$tokens, [ref]$errors)
    if (@($errors).Count) { throw "$($file.Name): $($errors -join '; ')" }
}
Write-Output "PASS PowerShell parse: $($sourceFiles.Count) files"
$code = [IO.File]::ReadAllText((Join-Path $SourceDirectory 'Windows.Automation.cs'))
if ($ReferenceDirectory -and $RoslynDirectory) {
    $null = [Reflection.Assembly]::LoadFrom((Join-Path $RoslynDirectory 'Microsoft.CodeAnalysis.dll'))
    $null = [Reflection.Assembly]::LoadFrom((Join-Path $RoslynDirectory 'Microsoft.CodeAnalysis.CSharp.dll'))
    $options = [Microsoft.CodeAnalysis.CSharp.CSharpParseOptions]::Default.WithLanguageVersion(
        [Microsoft.CodeAnalysis.CSharp.LanguageVersion]::CSharp5)
    $tree = [Microsoft.CodeAnalysis.CSharp.CSharpSyntaxTree]::ParseText($code, $options)
    [Microsoft.CodeAnalysis.MetadataReference[]]$references = @(
        'mscorlib.dll', 'System.dll', 'System.Core.dll', 'System.Windows.Forms.dll',
        'System.Drawing.dll', 'UIAutomationClient.dll', 'UIAutomationTypes.dll', 'WindowsBase.dll'
    ) | ForEach-Object {
        [Microsoft.CodeAnalysis.MetadataReference]::CreateFromFile((Join-Path $ReferenceDirectory $_))
    }
    $compilationOptions = New-Object Microsoft.CodeAnalysis.CSharp.CSharpCompilationOptions(
        [Microsoft.CodeAnalysis.OutputKind]::DynamicallyLinkedLibrary)
    $compilation = [Microsoft.CodeAnalysis.CSharp.CSharpCompilation]::Create(
        'AuroraReviewRelayValidation', [Microsoft.CodeAnalysis.SyntaxTree[]]@($tree), $references,
        $compilationOptions)
    $stream = New-Object IO.MemoryStream
    try {
        $result = $compilation.Emit($stream)
        $diagnostics = @($result.Diagnostics | Where-Object { $_.Severity.ToString() -in @('Error', 'Warning') })
        if (-not $result.Success -or $diagnostics.Count) { throw ($diagnostics -join [Environment]::NewLine) }
    } finally { $stream.Dispose() }
    Write-Output 'PASS C# 5 compile against actual Microsoft .NET Framework references (no Windows execution)'
} elseif ([Environment]::OSVersion.Platform -eq [PlatformID]::Win32NT) {
    Add-Type -AssemblyName System.Windows.Forms, System.Drawing, UIAutomationClient, UIAutomationTypes, WindowsBase
    $references = @(
        [Diagnostics.Process].Assembly.Location, [Windows.Forms.Form].Assembly.Location,
        [Drawing.Point].Assembly.Location, [Windows.Automation.AutomationElement].Assembly.Location,
        [Windows.Automation.ControlType].Assembly.Location, [System.Windows.Point].Assembly.Location
    ) | Select-Object -Unique
    Add-Type -TypeDefinition $code -ReferencedAssemblies $references
    Write-Output 'PASS Windows C# compile (does not click or send keys)'
} else { throw 'Linux compile validation needs ReferenceDirectory and RoslynDirectory.' }
