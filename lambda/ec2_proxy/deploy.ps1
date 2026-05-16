param(
  [Parameter(Mandatory=$true)]
  [string]$StackName,

  [Parameter(Mandatory=$true)]
  [string]$Region,

  [string]$AdminToken = "",

  [string]$PairCode = "",

  [string]$TokenSigningKey = "",

  [string]$AllowedInstanceArns = ""
)

$ErrorActionPreference = 'Stop'

Push-Location $PSScriptRoot

try {

function New-RandomSecret {
  param(
    [Parameter(Mandatory=$true)]
    [int]$ByteCount
  )

  $bytes = New-Object byte[] $ByteCount
  [System.Security.Cryptography.RandomNumberGenerator]::Create().GetBytes($bytes)
  $secret = [Convert]::ToBase64String($bytes)
  return ($secret -replace '[^a-zA-Z0-9]', '')
}

if (-not $AdminToken) {
  $AdminToken = New-RandomSecret -ByteCount 32
  Write-Host "Generated AdminToken: $AdminToken"
}

if (-not $PairCode) {
  $PairCode = New-RandomSecret -ByteCount 16
  Write-Host "Generated PairCode: $PairCode"
}

if (-not $TokenSigningKey) {
  $TokenSigningKey = New-RandomSecret -ByteCount 32
  Write-Host "Generated TokenSigningKey: $TokenSigningKey"
}

$samCommand = $null
foreach ($candidate in @('sam', 'sam.cmd', 'sam.exe')) {
  $resolved = Get-Command $candidate -ErrorAction SilentlyContinue
  if ($resolved) {
    $samCommand = $resolved.Source
    break
  }
}

if (-not $samCommand) {
  throw "SAM CLI is not installed or not on PATH. Install AWS SAM CLI, then rerun this script. On Windows, ensure 'sam' is available in a new PowerShell session."
}

$paramOverrides = @(
  "AdminToken=$AdminToken"
  "PairCode=$PairCode"
  "TokenSigningKey=$TokenSigningKey"
)

if ($AllowedInstanceArns -ne "") {
  $paramOverrides += "AllowedInstanceArns=$AllowedInstanceArns"
}

& $samCommand build --template-file template.yaml
& $samCommand deploy `
  --stack-name $StackName `
  --region $Region `
  --capabilities CAPABILITY_IAM `
  --resolve-s3 `
  --template-file .aws-sam/build/template.yaml `
  --parameter-overrides $paramOverrides

}
finally {
  Pop-Location
}
