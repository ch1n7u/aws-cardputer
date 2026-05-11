param(
  [Parameter(Mandatory=$true)]
  [string]$StackName,

  [Parameter(Mandatory=$true)]
  [string]$Region,

  [Parameter(Mandatory=$true)]
  [string]$AdminToken,

  [Parameter(Mandatory=$true)]
  [string]$PairCode,

  [Parameter(Mandatory=$true)]
  [string]$TokenSigningKey,

  [string]$AllowedInstanceArns = ""
)

$ErrorActionPreference = 'Stop'

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
