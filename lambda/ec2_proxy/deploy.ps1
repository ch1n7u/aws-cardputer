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

# Please save the AdminToken and PairCode and update the config in web

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
}

if (-not $PairCode) {
  $PairCode = New-RandomSecret -ByteCount 16
}

if (-not $TokenSigningKey) {
  $TokenSigningKey = New-RandomSecret -ByteCount 32
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

$buildOutLog = Join-Path $env:TEMP 'sam_build.out.log'
$buildErrLog = Join-Path $env:TEMP 'sam_build.err.log'
$deployOutLog = Join-Path $env:TEMP 'sam_deploy.out.log'
$deployErrLog = Join-Path $env:TEMP 'sam_deploy.err.log'

$buildProcess = Start-Process -FilePath $samCommand -ArgumentList @('build', '--template-file', 'template.yaml') -NoNewWindow -Wait -PassThru -RedirectStandardOutput $buildOutLog -RedirectStandardError $buildErrLog
if ($buildProcess.ExitCode -ne 0) {
  Write-Host "SAM build failed. Showing build log:"
  if (Test-Path $buildOutLog) { Get-Content $buildOutLog -Raw }
  if (Test-Path $buildErrLog) { Get-Content $buildErrLog -Raw }
  throw "sam build failed"
}

$deployArguments = @(
  'deploy',
  '--stack-name', $StackName,
  '--region', $Region,
  '--capabilities', 'CAPABILITY_IAM',
  '--resolve-s3',
  '--template-file', '.aws-sam/build/template.yaml',
  '--parameter-overrides'
) + $paramOverrides

$deployProcess = Start-Process -FilePath $samCommand -ArgumentList $deployArguments -NoNewWindow -Wait -PassThru -RedirectStandardOutput $deployOutLog -RedirectStandardError $deployErrLog
if ($deployProcess.ExitCode -ne 0) {
  Write-Host "SAM deploy failed. Showing deploy log:"
  if (Test-Path $deployOutLog) { Get-Content $deployOutLog -Raw }
  if (Test-Path $deployErrLog) { Get-Content $deployErrLog -Raw }
  throw "sam deploy failed"
}

# After successful deploy, lookup the API endpoint and print only the three values
try {
  $apiEndpoint = ''
  $awsCmd = Get-Command aws -ErrorAction SilentlyContinue
  if ($awsCmd) {
    # Try to get the RestApi physical id for the Ec2ProxyApi logical resource
    $restApiId = (& aws cloudformation describe-stack-resources --stack-name $StackName --region $Region --query "StackResources[?LogicalResourceId=='Ec2ProxyApi'].PhysicalResourceId" --output text) 2>$null
    if ($restApiId) {
      $restApiId = $restApiId.Trim()
      $apiEndpoint = "https://$restApiId.execute-api.$Region.amazonaws.com/Prod"
    } else {
      # Fallback to looking for an explicit output value
      $apiEndpoint = (& aws cloudformation describe-stacks --stack-name $StackName --region $Region --query "Stacks[0].Outputs[?contains(OutputKey, 'ApiEndpoint') || contains(OutputKey, 'Ec2ProxyApiEndpoint')].OutputValue" --output text) 2>$null
      if ($apiEndpoint) { $apiEndpoint = $apiEndpoint.Trim() }
    }
  } else {
    # Try AWS Tools for PowerShell if AWS CLI is not available
    try {
      $resources = Get-CFNStackResource -StackName $StackName -Region $Region -ErrorAction Stop
      $rest = $resources | Where-Object { $_.LogicalResourceId -eq 'Ec2ProxyApi' }
      if ($rest) {
        $restApiId = $rest.PhysicalResourceId
        $apiEndpoint = "https://$restApiId.execute-api.$Region.amazonaws.com/Prod"
      } else {
        $stack = Get-CFNStack -StackName $StackName -Region $Region -ErrorAction Stop
        $output = $stack.Outputs | Where-Object { $_.OutputKey -match 'ApiEndpoint|Ec2ProxyApiEndpoint' }
        if ($output) { $apiEndpoint = $output.OutputValue }
      }
    } catch {
      $apiEndpoint = ''
    }
  }

  # Print only the required values
  Write-Output "AdminToken=$AdminToken"
  Write-Output "PairCode=$PairCode"
  Write-Output "ApiGatewayUrl=$apiEndpoint"
} catch {
  # If the lookup fails, still print tokens (endpoint may be empty)
  Write-Output "AdminToken=$AdminToken"
  Write-Output "PairCode=$PairCode"
  Write-Output "ApiGatewayUrl="
}
}
finally {
  Pop-Location
}
