# PocketCloud Terminal — Complete Deployment Guide
## M5Stack Cardputer v1.1 AWS EC2 Control System

**Target**: M5Stack Cardputer v1.1 (ESP32-S3) → AWS Lambda API → EC2 Instance Management  
**Platform**: Windows 11  
**Duration**: ~45 minutes (fresh machine, serial)

---

## SECTION 1 — PREREQUISITES

### 1.1 System Requirements

**Minimum:**
- Windows 11 (22H2 or later recommended)
- 4GB RAM
- 5GB free disk space
- Admin access to install software
- USB port for device flashing

---

### 1.2 Git

**What it is**: Version control system. Required to clone the project repository.

**Installation Command:**
```powershell
# Download installer from https://git-scm.com/download/win
# Or via Chocolatey (if installed):
choco install git -y
```

**Verification Command:**
```powershell
git --version
```

**Expected Output:**
```
git version 2.43.0.windows.1
```

---

### 1.3 Python 3.11+

**What it is**: Runtime for AWS Lambda handler and local testing.

**Installation Command:**
- Download from: https://www.python.org/downloads/
- Select Python 3.11+ (e.g., 3.11.8 or 3.12+)
- **Important**: Check **"Add Python to PATH"** during installation

**Verification Command:**
```powershell
python --version
```

**Expected Output:**
```
Python 3.11.8  (or higher)
```

---

### 1.4 pip

**What it is**: Python package manager. Usually installed with Python.

**Verification Command:**
```powershell
pip --version
```

**Expected Output:**
```
pip 23.3.1 from C:\Users\...\AppData\Local\Programs\Python\Python311\lib\site-packages\python
```

If missing:
```powershell
python -m pip install --upgrade pip
```

---

### 1.5 AWS CLI v2

**What it is**: Command-line interface for AWS. Required for IAM, EC2, and credential configuration.

**Installation Command:**
- Download from: https://aws.amazon.com/cli/
- Run the MSI installer and follow prompts

**Verification Command:**
```powershell
aws --version
```

**Expected Output:**
```
aws-cli/2.14.0 Python/3.11.0 Windows/11.0.22621 exe/AMD64.Intel64
```

---

### 1.6 AWS SAM CLI

**What it is**: AWS Serverless Application Model CLI. Required to build and deploy Lambda functions and supporting AWS resources.

**Installation Command (Windows):**

Option A — MSI Installer (Recommended):
```powershell
# Download from:
# https://github.com/aws/aws-sam-cli/releases
# Look for "AWS-SAM-CLI-v1.X.X-x86_64.msi"
# Download and run the .msi file
```

Option B — Chocolatey (if installed):
```powershell
choco install aws-sam-cli -y
```

Option C — pip (alternative):
```powershell
pip install aws-sam-cli
```

**Verification Command (in NEW PowerShell window after install):**
```powershell
sam --version
```

**Expected Output:**
```
SAM CLI, version 1.100.0
```

If you get "sam is not recognized," restart PowerShell or log out/log in to refresh PATH.

---

### 1.7 Visual Studio Code

**What it is**: Code editor for editing firmware, Lambda code, and configuration.

**Installation Command:**
- Download from: https://code.visualstudio.com/
- Run the installer

**Verification Command:**
```powershell
code --version
```

**Expected Output:**
```
1.85.0
(or higher)
```

---

### 1.8 PlatformIO CLI

**What it is**: Build system for ESP32 firmware. Compiles and uploads code to the Cardputer.

**Installation Command:**

Option A — Via VS Code Extension (Recommended):
1. Open VS Code
2. Go to Extensions (Ctrl+Shift+X)
3. Search "PlatformIO"
4. Click "Install" on "PlatformIO IDE" by PlatformIO
5. Restart VS Code
6. PlatformIO will auto-install the CLI

Option B — Via pip:
```powershell
pip install platformio
```

**Verification Command:**
```powershell
pio --version
```

**Expected Output:**
```
PlatformIO Core, version X.X.X
```

---

### 1.9 M5Stack Cardputer Drivers

**What it is**: USB serial drivers so Windows recognizes the device over USB.

**Installation:**

1. Connect the M5Stack Cardputer via USB-C cable
2. Open Device Manager (Win+X → Device Manager)
3. Look for "Unknown Device" or "USB Serial Device"
4. If present, right-click → Update driver
5. Download from: https://www.silabs.com/developers/usb-to-uart-bridge-vcp-drivers
6. Select "Windows" → download and install
7. Restart computer

**Verification:**
```powershell
pio device list
```

**Expected Output:**
```
/dev/ttyUSBx
    Hardware ID: USB VID:PID=10C4:EA60
```

(Port name varies; typical on Windows: `COM3`, `COM4`, etc.)

---

### 1.10 PowerShell Requirements

**What it is**: Windows shell for running deployment scripts.

**Version Check:**
```powershell
$PSVersionTable.PSVersion
```

**Minimum**: PowerShell 5.1 (usually built-in to Windows 11)

**If you need to enable script execution:**
```powershell
Set-ExecutionPolicy -ExecutionPolicy RemoteSigned -Scope CurrentUser
```

---

## SECTION 2 — AWS ACCOUNT SETUP

### 2.1 Create AWS Account

1. Go to https://aws.amazon.com/
2. Click "Create an AWS Account"
3. Provide email and password
4. Enter card information (charges will apply for EC2)
5. Verify email and phone
6. Choose Support Plan (Basic = free)

---

### 2.2 Create IAM User for Deployment

**Why**: Do not use AWS account root credentials. Create a limited IAM user with just enough permissions to deploy the Lambda stack.

**Steps:**

1. Log into AWS Console: https://console.aws.amazon.com
2. Search for "IAM" (Identity and Access Management)
3. Click "Users" in left sidebar
4. Click "Create user"
5. Enter name: `cardputer-deployer`
6. Click "Next"
7. **Attach policies**:
   - Check: `AdministratorAccess` (for this demo; in production, restrict further)
8. Click "Create user"

---

### 2.3 Generate Access Key for CLI

**Steps:**

1. In IAM Users list, click `cardputer-deployer`
2. Go to "Security credentials" tab
3. Under "Access keys", click "Create access key"
4. Choose "Command Line Interface (CLI)"
5. Acknowledge warning, click "Create access key"
6. **SAVE THE KEY** (you can only see it once):
   - Access Key ID
   - Secret Access Key
7. Download CSV or copy both values to a secure location

---

### 2.4 Configure AWS CLI

**Steps:**

1. Open PowerShell
2. Run:
   ```powershell
   aws configure
   ```
3. Enter values (from section 2.3):
   ```
   AWS Access Key ID [None]: YOUR_ACCESS_KEY_ID
   AWS Secret Access Key [None]: YOUR_SECRET_ACCESS_KEY
   Default region name [None]: ap-south-1
   Default output format [None]: json
   ```
4. Verify configuration:
   ```powershell
   aws sts get-caller-identity
   ```

**Expected Output:**
```json
{
    "UserId": "AIDAI...",
    "Account": "123456789012",
    "Arn": "arn:aws:iam::123456789012:user/cardputer-deployer"
}
```

---

## SECTION 3 — EC2 INSTANCE SETUP

### 3.1 Launch an EC2 Instance

**Steps:**

1. Log into AWS Console
2. Search for "EC2" (Elastic Compute Cloud)
3. Click "Instances" in left sidebar
4. Click "Launch instances"
5. **AMI**: Select "Ubuntu Server 22.04 LTS" (free tier eligible)
6. **Instance Type**: `t2.micro` (free tier; smallest, adequate for testing)
7. **Key Pair**: Click "Create new key pair"
   - Name: `cardputer-ec2-key`
   - Type: `RSA`
   - Format: `.pem` (for Linux/Mac; use `.ppk` if you need PuTTY on Windows)
   - Click "Create key pair"
   - **Save the .pem file** to a secure folder (e.g., `C:\Users\YourName\.ssh\cardputer-ec2-key.pem`)
8. **Security Group**:
   - Create new security group
   - Name: `cardputer-ec2-sg`
   - **Inbound Rules**:
     - SSH (22): Source = `0.0.0.0/0` (or your IP for security)
   - Click "Launch instances"

---

### 3.2 Retrieve Instance Details

**After launch (5-10 minutes):**

1. Go to EC2 → Instances
2. Click the instance ID
3. Note:
   - **Instance ID** (e.g., `i-0a1b2c3d4e5f6g7h8`)
   - **Public IPv4 Address** (e.g., `54.123.45.67`)
   - **Private IPv4 Address** (e.g., `172.31.0.100`)

**Store these for later use.**

---

### 3.3 Cost Considerations

**Free Tier (Year 1):**
- `t2.micro` instance: 750 hours/month = free
- Data transfer: varies; usually ~15GB/month free

**Beyond Free Tier:**
- `t2.micro`: ~$0.01/hour (~$7-8/month if always running)
- **Recommendation**: Stop the instance when not in use

**To stop instance (free; preserves data):**
```powershell
aws ec2 stop-instances --instance-ids i-0a1b2c3d4e5f6g7h8 --region ap-south-1
```

**To start again:**
```powershell
aws ec2 start-instances --instance-ids i-0a1b2c3d4e5f6g7h8 --region ap-south-1
```

---

### 3.4 Elastic IP (Optional)

To keep the same IP address across stop/start cycles:

1. Go to EC2 → Elastic IPs
2. Click "Allocate Elastic IP address"
3. In Actions, click "Associate Elastic IP address"
4. Select your instance
5. Click "Associate"

**Cost**: ~$0.005/hour if not attached; free when attached to running instance.

---

## SECTION 4 — PROJECT BUILD

### 4.1 Clone Project Repository

```powershell
cd C:\Users\YourName\Documents
git clone https://github.com/YOUR_REPO/aws-cardputer.git
cd aws-cardputer
```

(Replace with your actual repository URL.)

---

### 4.2 Verify PlatformIO Configuration

Open `platformio.ini` in the project root:

```ini
[env:m5stack-stamps3]
platform = espressif32
board = m5stack-stamps3
framework = arduino

build_flags =
    -O2
    -Wall
    -DARDUINO_LOOP_STACK_SIZE=8192

monitor_speed = 115200
monitor_filters = esp32_exception_decoder

lib_deps =
    m5stack/M5Cardputer @ ^1.1.1
    m5stack/M5Unified @ ^0.2.14
    m5stack/M5GFX @ ^0.2.20
    ArduinoJson @ ^6.21.6

board_upload_speed = 921600
board_build.f_cpu = 240000000L
board_build.partitions = default.csv
```

**Verify**:
- `board = m5stack-stamps3` (correct for Cardputer)
- `lib_deps` includes M5Cardputer, M5Unified, M5GFX, ArduinoJson

---

### 4.3 Build the Firmware

```powershell
cd C:\Users\YourName\Documents\aws-cardputer
pio run
```

**Expected Output:**
```
============= pio run =============
...
PLATFORM: Espressif 32 (v6.7.0)
BOARD: M5Stack Stamps3
FRAMEWORK: Arduino
...
Environment m5stack-stamps3 PASSED [00:00:45]
============= END COMPILE ===============
```

**Troubleshooting:**

If you see **library not found**:
```powershell
pio lib install
```

If you see **platform error**:
```powershell
pio platform install espressif32
```

If build still fails, check [Section 9 — TROUBLESHOOTING](#section-9--troubleshooting).

---

## SECTION 5 — FLASHING TO CARDPUTER

### 5.1 Connect Device Over USB

1. Plug M5Stack Cardputer into Windows PC via USB-C cable
2. Wait 2-3 seconds for driver to load

---

### 5.2 Detect COM Port

```powershell
pio device list
```

**Expected Output:**
```
COM3
  Hardware ID: USB VID:PID=10C4:EA60 SNR=0001
```

Note the COM port (e.g., `COM3`). If no device shows up, see [Section 9](#section-9--troubleshooting).

---

### 5.3 Upload Firmware

```powershell
pio run --target upload
```

**Expected Output:**
```
Device: COM3
Connecting.... ___
Flashing... [============] 100%
Hash of data verified.

Hard resetting via RTS pin...
============= [SUCCESS] Upload complete ========
```

**If upload hangs or fails**, try:
1. Unplug and replug USB cable
2. Press the reset button on Cardputer (if visible)
3. Re-run: `pio run --target upload`

---

### 5.4 Monitor Serial Output

After successful upload, view real-time firmware logs:

```powershell
pio device monitor --baud 115200
```

**Expected Output** (shows during boot):
```
M5Cardputer hardware initialized
SD: not present (or SD: mounted if SD card inserted)
Scanning SSIDs...
...
```

Press Ctrl+C to exit monitor.

---

## SECTION 6 — AWS BACKEND DEPLOYMENT

### 6.1 Generate Secure Deployment Parameters

You need three random secrets. Generate them:

**In PowerShell:**

```powershell
# Generate AdminToken (32+ bytes, base64-like)
$adminToken = [Convert]::ToBase64String(
  [System.Security.Cryptography.RNGCryptoServiceProvider]::new().GetBytes(32)
) -replace '[^a-zA-Z0-9]', ''
Write-Host "AdminToken: $adminToken"

# Generate PairCode (16+ bytes)
$pairCode = [Convert]::ToBase64String(
  [System.Security.Cryptography.RNGCryptoServiceProvider]::new().GetBytes(16)
) -replace '[^a-zA-Z0-9]', ''
Write-Host "PairCode: $pairCode"

# Generate TokenSigningKey (32+ bytes)
$tokenSigningKey = [Convert]::ToBase64String(
  [System.Security.Cryptography.RNGCryptoServiceProvider]::new().GetBytes(32)
) -replace '[^a-zA-Z0-9]', ''
Write-Host "TokenSigningKey: $tokenSigningKey"
```

**Example Output** (NEVER use these; generate your own):
```
AdminToken: a1b2c3d4e5f6g7h8i9j0k1l2m3n4o5p6
PairCode: x9y8z7w6v5u4t3s2
TokenSigningKey: q1w2e3r4t5y6u7i8o9p0a1s2d3f4g5h
```

**Store these somewhere safe** (not in code or email; use a password manager).

If you prefer, `deploy.ps1` can generate these values automatically when you omit them.

---

### 6.2 Inspect Deployment Script

Open `lambda/ec2_proxy/deploy.ps1`:

```powershell
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
```

**Parameters explained:**
- `StackName`: CloudFormation stack name (e.g., `ec2-proxy-stack`). Unique per deployment.
- `Region`: AWS region (e.g., `ap-south-1`). Must match EC2 instance region.
- `AdminToken`: Secret for revoke API. Store securely.
- `PairCode`: Device uses this to pair. Device must send same code.
- `TokenSigningKey`: HMAC secret to sign access tokens. Must match device's understanding.
- `AllowedInstanceArns`: (optional) Restrict which EC2 instances can be controlled.

---

### 6.3 Inspect Lambda Template

Open `lambda/ec2_proxy/template.yaml`:

**Key resources:**
- `Ec2ProxyApi`: API Gateway REST endpoint
- `Ec2ProxyFunction`: Lambda function (Python handler)
- `DeviceSessionTable`: DynamoDB table for storing device refresh tokens
- `Parameters`: AdminToken, PairCode, TokenSigningKey, etc.

**IAM Policies in template:**
```yaml
Ec2Describe:
  - ec2:DescribeInstances
Ec2PowerControl:
  - ec2:StartInstances
  - ec2:StopInstances
```

This grants Lambda permission to list and control EC2 instances.

---

### 6.4 Deploy Lambda Stack

**Preconditions:**
- AWS CLI configured (Section 2.4)
- SAM CLI installed and in PATH (Section 1.6)
- Either generated secrets from Section 6.1 or let `deploy.ps1` generate them automatically

**Deploy Command:**

```powershell
cd C:\Users\YourName\Documents\aws-cardputer\lambda\ec2_proxy

.\deploy.ps1 `
   -StackName "ec2-proxy-stack" `
   -Region "ap-south-1"
```

To use manually generated values instead, add `-AdminToken`, `-PairCode`, and `-TokenSigningKey`.

**Example:**
```powershell
.\deploy.ps1 `
   -StackName "ec2-proxy-stack" `
   -Region "ap-south-1"
```

If you prefer explicit secrets, add the three secret parameters shown above.

---

### 6.5 Monitor Deployment

**Expected output:**
```
Building
...

Packaging
...

Deploying
...

Successfully created/updated stack in CloudFormation
Stack outputs:
  Ec2ProxyApiEndpoint: https://API_ID.execute-api.ap-south-1.amazonaws.com/Prod
```

**SAVE THIS ENDPOINT URL.** You'll need it on the device.

---

### 6.6 Verify Deployment

**Check CloudFormation stack:**
```powershell
aws cloudformation describe-stacks `
  --stack-name "ec2-proxy-stack" `
  --region "ap-south-1" `
  --query "Stacks[0].Outputs"
```

**Expected Output:**
```json
[
  {
    "OutputKey": "Ec2ProxyApiEndpoint",
    "OutputValue": "https://a1b2c3d4e5.execute-api.ap-south-1.amazonaws.com/Prod"
  }
]
```

---

## SECTION 7 — DEVICE CONFIGURATION

### 7.1 Boot Device and Connect Wi-Fi

1. Power on the M5Stack Cardputer
2. Display shows:
   ```
   SD: not present
   Scanning SSIDs...
   5 networks found
   1: MyHomeWiFi (-45)
   2: NeighborWiFi (-67)
   ...
   Select network: press 1-9, R=rescan
   ```
3. Press `1` to select first network (or your Wi-Fi)
4. Enter password when prompted
5. Device saves credentials to Preferences (NVS) and/or SD card

**Expected outcome:**
```
Connected: 192.168.1.100
```

---

### 7.2 Open Settings Menu

Press the `s` key (Settings):

```
Device Settings
Edit URL/token/pair/device
u=url, k=legacy token, c=pair code
d=device id
Press 'p' to edit PIN
Press 'i' to import /ec2.conf from SD
Press 'w' to write SD, 's' save prefs, 'b' back

URL: 
Device: cardputer-XXXXXXXX
Pair code: ****
Token: ****
PIN set: no
```

---

### 7.3 Set Backend API Endpoint

1. Press `u` to edit URL
2. Type the API endpoint from Section 6.5:
   ```
   https://a1b2c3d4e5.execute-api.ap-south-1.amazonaws.com/Prod
   ```
3. Press Enter
4. Verify displayed URL matches

---

### 7.4 Set Pairing Code

1. Press `c` to edit pair code
2. Type the **same** PairCode from Section 6.1
   ```
   x9y8z7w6v5u4t3s2
   ```
3. Press Enter

---

### 7.5 Set 4-Digit PIN (Optional but Recommended)

1. Press `p` to edit PIN
2. Enter exactly 4 digits (e.g., `1234`)
3. Press Enter
4. Verify "PIN set: yes" appears

---

### 7.6 Save Settings

1. Press `s` to save to Preferences (NVS)
2. Display confirms: "Saved to Preferences"
3. (Optional) Press `w` to write to SD card for backup

---

### 7.7 Verify Device ID

Device automatically generates unique ID from MAC address:
```
Device: cardputer-3C71BF5A2E10
```

This ID is shown in settings. Note it for debugging.

---

## SECTION 8 — END-TO-END TESTING

### 8.1 Boot Device and Connect

1. Power on device
2. Wait for Wi-Fi connection
3. Verify "WiFi OK: 192.168.X.X" appears

---

### 8.2 Start EC2 Instance

**Via AWS Console:**
1. Go to EC2 → Instances
2. Select your instance
3. Click "Instance State" → "Start instance"
4. Wait 30-60 seconds for it to reach "Running" state
5. Note its **Public IPv4 Address** (e.g., `54.123.45.67`)

**Or via CLI:**
```powershell
aws ec2 start-instances `
  --instance-ids i-0a1b2c3d4e5f6g7h8 `
  --region ap-south-1
```

---

### 8.3 List EC2 Instances on Device

1. Press `e` key (EC2 UI)
2. If device has never paired:
   - Displays: "Request sent" (pairing in progress)
   - Device exchanges pair code for access token with Lambda
   - Device stores refresh token for future use
3. On success, device shows:
   ```
   EC2 Instances:
   1: my-test-instance (running)
   2: another-instance (stopped)
   ...
   Select instance 1-9
   ```

---

### 8.4 Control Instance from Device

1. Press `1` to select first instance
2. Display shows:
   ```
   Instance: my-test-instance
   State: running
   Press 't' to stop
   Press 'b' to go back
   ```
3. Press `t` to stop the instance
4. Display shows: "Sending stop... Request sent"
5. Instance status in AWS console changes to "Stopping" then "Stopped" (30-60 seconds)

---

### 8.5 Verify Lambda Logs

**Via CloudWatch Logs:**
1. Go to AWS Console → CloudWatch → Log Groups
2. Find `/aws/lambda/Ec2ProxyFunction` (exact name varies)
3. Click and view latest log streams
4. Should show successful pairing and instance queries

**Or via CLI:**
```powershell
aws logs tail /aws/lambda/Ec2ProxyFunction --follow --region ap-south-1
```

**Expected entries:**
```
2024-01-15 14:23:45 - Pairing device: cardputer-3C71BF5A2E10
2024-01-15 14:23:46 - Listing instances for device: cardputer-3C71BF5A2E10
2024-01-15 14:23:47 - Stopping instance: i-0a1b2c3d4e5f6g7h8
```

---

### 8.6 SSH to EC2 (Optional Verification)

**On Windows 11 (with WSL or OpenSSH):**

1. Open PowerShell or Command Prompt
2. SSH to instance:
   ```powershell
   ssh -i C:\Users\YourName\.ssh\cardputer-ec2-key.pem ubuntu@54.123.45.67
   ```
3. Accept fingerprint prompt (type `yes`)
4. Connected to Ubuntu instance:
   ```
   ubuntu@ip-172-31-0-100:~$
   ```

**Exit SSH:**
```bash
exit
```

---

### 8.7 Test Device Token Refresh

1. Wait ~15 minutes (default access token TTL is 15 minutes)
2. Press `e` again to use EC2 UI
3. Device automatically calls `/refresh` endpoint with stored refresh token
4. Lambda returns new access token
5. Instance list loads without re-pairing

---

## SECTION 9 — TROUBLESHOOTING

### 9.1 PlatformIO Issues

#### Build fails with "platform not found"
```
Error: Could not find platform 'espressif32' in platform registry
```

**Fix:**
```powershell
pio platform install espressif32
pio run --target clean
pio run
```

---

#### Library install fails
```
Error: Library 'm5stack/M5Cardputer' not found
```

**Fix:**
```powershell
pio lib install
pio run
```

---

#### Memory exhaustion during build
```
Fatal error: cc1plus: out of memory allocating 123456 bytes
```

**Fix:**
- Build on a machine with >4GB RAM
- Or reduce debug symbols:
  ```ini
  build_flags = -O2 -DNDEBUG
  ```

---

### 9.2 Driver Issues

#### Device not detected after plugging in
```
No COM port appears in "pio device list"
```

**Fix:**
1. Download USB driver: https://www.silabs.com/developers/usb-to-uart-bridge-vcp-drivers
2. Install for Windows (CP210x driver)
3. Restart computer
4. Re-plug USB cable

---

#### COM Port in Device Manager shows yellow triangle
```
Unknown USB Device (Device Descriptor Request Failed)
```

**Fix:**
1. Right-click → "Update driver"
2. Select "Browse my computer for drivers"
3. Navigate to extracted CP210x driver folder
4. Select and install

---

### 9.3 COM Port Issues

#### Upload hangs at "Connecting...."
```
Device: COM3
Connecting.... __
```

**Fix:**
1. Unplug USB, wait 5 seconds, replug
2. Press the physical reset button on Cardputer (if visible)
3. Try again:
   ```powershell
   pio run --target upload
   ```

---

#### Wrong COM port selected
```
Error: Failed to open COM999
```

**Fix:**
1. Find correct port:
   ```powershell
   pio device list
   ```
2. Specify manually:
   ```powershell
   pio run --target upload -p COM3
   ```

---

### 9.4 SAM Deployment Failures

#### "sam is not recognized"
```
sam is not recognized as the name of a cmdlet, function, script file, or operable program.
```

**Fix:**
1. Ensure SAM CLI is installed (Section 1.6)
2. Open **NEW** PowerShell window (to refresh PATH)
3. Verify:
   ```powershell
   sam --version
   ```
4. If still missing, reinstall:
   ```powershell
   pip uninstall aws-sam-cli
   pip install aws-sam-cli
   ```

---

#### "CAPABILITY_IAM not set"
```
Error: An error occurred (InsufficientCapabilitiesException) when calling the CreateStack operation
```

**Fix:**
The deploy.ps1 script includes `--capabilities CAPABILITY_IAM`. Ensure you're running the latest script from the repo. If error persists:
```powershell
cd lambda/ec2_proxy
.\deploy.ps1 -StackName "ec2-proxy-stack" -Region "ap-south-1" -AdminToken "..." -PairCode "..." -TokenSigningKey "..."
```

---

#### "S3 bucket name already exists"
```
Error: 1 validation error detected
S3 bucket 'aws-sam-cli-managed-default-samclisourcebucket-abc123' already exists
```

**Fix:**
This is a conflict with existing SAM resources. Either:
1. Use a different StackName
2. Or delete old stack:
   ```powershell
   aws cloudformation delete-stack --stack-name ec2-proxy-stack --region ap-south-1
   ```

---

### 9.5 AWS Auth Failures

#### AWS CLI returns "InvalidClientTokenId"
```
An error occurred (InvalidClientTokenId) when calling the GetCallerIdentity operation: The security token included in the request is invalid.
```

**Fix:**
1. Verify credentials:
   ```powershell
   aws configure
   ```
2. Re-enter Access Key ID and Secret Access Key (Section 2.3)
3. Test:
   ```powershell
   aws sts get-caller-identity
   ```

---

#### SAM deploy fails with "User is not authorized to perform"
```
User: arn:aws:iam::123456789012:user/cardputer-deployer is not authorized to perform: cloudformation:CreateStack
```

**Fix:**
1. Go to IAM → Users → cardputer-deployer
2. Add policy: `CloudFormationFullAccess`
3. Also ensure policies include:
   - `LambdaFullAccess`
   - `APIGatewayFullAccess`
   - `DynamoDBFullAccess`
   - `EC2FullAccess` (or restricted for production)

---

### 9.6 Lambda Failures

#### Lambda returns "500 Internal Server Error"
```
{
  "error": "internal server error"
}
```

**Fix:**
1. Check CloudWatch logs (Section 8.5)
2. Common causes:
   - `PAIR_CODE` environment variable not set → verify deploy.ps1 parameters
   - `TOKEN_SIGNING_KEY` incorrect → regenerate and redeploy
   - DynamoDB table not created → check CloudFormation stack status

---

#### Device pairing returns "401 Unauthorized"
```
{
  "error": "pair failed"
}
```

**Fix:**
1. Verify device sent correct pair code:
   - Device settings: press `s` → press `c` → view pair code
   - Should match: `PairCode` parameter used in deploy.ps1
2. If mismatch, update device settings and retry

---

### 9.7 API Gateway Issues

#### API endpoint times out
```
Request timeout
```

**Fix:**
1. Verify Lambda is running (no errors)
2. Check API Gateway throttling: default is 5 req/sec
3. If making many requests quickly, wait or reduce rate
4. Monitor in AWS Console → API Gateway → Stage logs

---

#### CORS errors in device
```
{
  "error": "unauthorized"
}
```

**Fix:**
1. Device must send headers:
   - `Authorization: Bearer ACCESS_TOKEN`
   - `X-Device-Id: DEVICE_ID`
2. Verify in firmware (main.cpp):
   ```cpp
   http.addHeader("Authorization", String("Bearer ") + token);
   http.addHeader("X-Device-Id", deviceId);
   ```

---

### 9.8 Wi-Fi Failures

#### Device fails to connect to Wi-Fi
```
WiFi connect failed
```

**Fix:**
1. Verify SSID and password are correct
   - Try re-entering in device settings
   - Common issue: Wi-Fi password typo
2. Check Wi-Fi is 2.4GHz (Cardputer doesn't support 5GHz)
3. Ensure Wi-Fi is broadcasting SSID (not hidden)
4. Try moving closer to router

---

#### Device connects but loses connection
```
WiFi: OK 192.168.1.100
[after 30 seconds]
WiFi: disconnected
```

**Fix:**
1. Check router's Wi-Fi stability (channel interference, etc.)
2. Device tries to reconnect automatically
3. See firmware logs via serial monitor (Section 5.4)

---

### 9.9 Cardputer Boot Loops

#### Device restarts repeatedly
```
Boot counter: 1
Boot counter: 2
...
```

**Fix:**
1. Device may have OTA (over-the-air) update in progress. Wait 30+ seconds.
2. If persists, firmware corruption:
   ```powershell
   pio run --target erase
   pio run --target upload
   ```
3. If still fails, contact M5Stack support with device logs

---

#### Device shows "no" for major functions
```
Enable battery monitoring: no
Enable keyboard input: no
Enable display output: no
```

**Fix:**
Check `include/hardware_config.h`:
```cpp
#define ENABLE_BATTERY_MONITORING 1
#define ENABLE_KEYBOARD_INPUT 1
#define ENABLE_DISPLAY_OUTPUT 1
```
Should all be `1`. If not, edit and rebuild:
```powershell
pio run --target upload
```

---

### 9.10 Memory Exhaustion

#### Device reboots when opening EC2 UI
```
MemError: heap exhausted
```

**Fix:**
1. Firmware uses ~15% RAM (see build output). Likely cause: API response too large
2. Reduce MAX_API_RESPONSE in `include/hardware_config.h`:
   ```cpp
   #define MAX_API_RESPONSE 2048  // reduce to 1024 if needed
   ```
3. Rebuild:
   ```powershell
   pio run --target upload
   ```

---

### 9.11 SSH Failures

#### SSH connection refused
```
Connection refused on port 22
```

**Fix:**
1. Verify EC2 instance is running (not "Stopped")
2. Check Security Group allows inbound SSH (port 22)
   - Go to EC2 → Security Groups → cardputer-ec2-sg
   - Verify inbound rule: "SSH, TCP, Port 22, Source 0.0.0.0/0"
3. Wait 30 seconds after launching instance for SSH to start

---

#### "Host key verification failed"
```
Host key verification failed.
```

**Fix:**
On first SSH connection, type `yes` to accept the host key fingerprint. On future connections, no prompt.

---

### 9.12 Host Fingerprint Mismatch

#### ECDSA host key changed
```
@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@
WARNING: POSSIBLE DNS SPOOFING DETECTED!
@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@
```

**Fix:**
This occurs if:
1. EC2 instance was stopped/started (new host key)
2. Or Security group changed

**Resolution:**
1. Remove old key:
   ```powershell
   ssh-keygen -R 54.123.45.67
   ```
2. SSH again (accepts new key)

---

### 9.13 EC2 Unreachable

#### Device lists instances but start/stop fails
```
Request failed
```

**Fix:**
1. Verify EC2 instance is running
2. Check IAM policy on Lambda has EC2 permissions:
   ```yaml
   Ec2PowerControl:
     - ec2:StartInstances
     - ec2:StopInstances
   ```
3. Check instance ARN matches (if using `AllowedInstanceArns`)
4. View Lambda logs in CloudWatch (Section 8.5)

---

### 9.14 Security Group Mistakes

#### Device cannot reach Lambda API
```
SSL: CERTIFICATE_VERIFY_FAILED
```

**Fix:**
1. Verify device's AWS Root CA certificate is correct (pinned in firmware)
2. Device uses API Gateway custom domain (usually `execute-api.region.amazonaws.com`)
3. Ensure device is not blocking https (check firewall/proxy)

---

## SECTION 10 — SECURITY HARDENING

### 10.1 IAM Least Privilege

**Current Policy**: Deployment user has `AdministratorAccess` (too broad for production).

**Production Approach:**

Create a custom policy for `cardputer-deployer` that only grants:
```json
{
  "Version": "2012-10-17",
  "Statement": [
    {
      "Sid": "CloudFormationStack",
      "Effect": "Allow",
      "Action": [
        "cloudformation:CreateStack",
        "cloudformation:UpdateStack",
        "cloudformation:DescribeStacks"
      ],
      "Resource": "arn:aws:cloudformation:*:*:stack/ec2-proxy-stack/*"
    },
    {
      "Sid": "IAMPassRole",
      "Effect": "Allow",
      "Action": "iam:PassRole",
      "Resource": "arn:aws:iam::*:role/ec2-proxy-*"
    },
    {
      "Sid": "S3SAMBucket",
      "Effect": "Allow",
      "Action": "s3:*",
      "Resource": "arn:aws:s3:::aws-sam-cli-*"
    },
    {
      "Sid": "LambdaFullAccess",
      "Effect": "Allow",
      "Action": "lambda:*",
      "Resource": "*"
    },
    {
      "Sid": "DynamoDBFullAccess",
      "Effect": "Allow",
      "Action": "dynamodb:*",
      "Resource": "*"
    },
    {
      "Sid": "APIGatewayFullAccess",
      "Effect": "Allow",
      "Action": "apigateway:*",
      "Resource": "*"
    }
  ]
}
```

**Steps:**
1. Go to IAM → Users → cardputer-deployer
2. Click "Add permissions" → "Create inline policy"
3. Paste custom policy
4. Save

---

### 10.2 SSH Hardening

**Current**: SSH key allows password-less login to EC2.

**Hardening Steps** (on Ubuntu EC2 instance after SSH):

1. **Disable password authentication:**
   ```bash
   sudo nano /etc/ssh/sshd_config
   # Find: PasswordAuthentication yes
   # Change to: PasswordAuthentication no
   # Save (Ctrl+O, Enter, Ctrl+X)
   
   sudo systemctl restart ssh
   ```

2. **Restrict SSH to specific IPs** (if your home IP is static):
   Edit EC2 Security Group:
   - Inbound Rule: SSH (22), Source = `YOUR_HOME_IP/32` (not `0.0.0.0/0`)

3. **Change default SSH port** (optional, increases obscurity):
   ```bash
   sudo nano /etc/ssh/sshd_config
   # Find: #Port 22
   # Change to: Port 2222
   # Save
   
   sudo systemctl restart ssh
   ```
   Then update Security Group inbound rule to allow port 2222.

---

### 10.3 API Token Rotation

**Current**: Access tokens expire in 15 minutes; refresh tokens in 30 days.

**For production:**

1. **Rotate PairCode monthly:**
   - Generate new PairCode (Section 6.1)
   - Update Lambda: `./deploy.ps1 -PairCode "NEW_CODE" ...`
   - Update device settings with new pair code
   - Old pair codes automatically expire

2. **Rotate TokenSigningKey quarterly:**
   - Generate new TokenSigningKey (Section 6.1)
   - Existing refresh tokens become invalid (force re-pairing)
   - Update device pair code and redeploy

3. **Monitor token usage:**
   ```powershell
   aws logs filter-log-events `
     --log-group-name /aws/lambda/Ec2ProxyFunction `
     --filter-pattern "PAIR_CODE" `
     --region ap-south-1
   ```

---

### 10.4 EC2 Exposure Minimization

**Current**: Lambda can start/stop ANY EC2 instance in region.

**Hardening:**

Use `AllowedInstanceArns` parameter in deploy.ps1:

```powershell
.\deploy.ps1 `
  -StackName "ec2-proxy-stack" `
  -Region "ap-south-1" `
  -AdminToken "..." `
  -PairCode "..." `
  -TokenSigningKey "..." `
  -AllowedInstanceArns "arn:aws:ec2:ap-south-1:123456789012:instance/i-0a1b2c3d4e5f6g7h8"
```

Lambda will then **only** allow control of that specific instance.

---

### 10.5 Secret Management

**Current**: Secrets stored in:
- Device NVS (encrypted with device-bound XOR)
- Device SD card (encrypted)
- AWS Lambda environment variables (encrypted by AWS)

**For production:**

1. **Use AWS Secrets Manager for Lambda secrets:**
   ```python
   import boto3
   
   secretsClient = boto3.client('secretsmanager')
   response = secretsClient.get_secret_value(SecretId='cardputer/pair-code')
   pairCode = response['SecretString']
   ```

2. **Use Hardware Security Module (HSM) for device encryption:**
   - Current: XOR encryption (device-local)
   - Upgrade: Use ESP32-S3 Secure Enclave (eFuses, HMAC peripheral)

3. **Audit secret access:**
   ```powershell
   aws secretsmanager list-secret-version-ids --secret-id cardputer/pair-code --region ap-south-1
   ```

---

### 10.6 API Rate Limiting

**Current**: API Gateway throttles at 5 req/sec globally.

**For production:**

Tighten per-device rate limiting:
- Create Usage Plan in API Gateway
- Set per-API-key rate: 1 req/sec per device
- Assign API key to each device during provisioning

---

### 10.7 Logging and Monitoring

**Enable CloudWatch alarms:**

```powershell
aws cloudwatch put-metric-alarm `
  --alarm-name cardputer-lambda-errors `
  --alarm-description "Alert on Lambda errors" `
  --metric-name Errors `
  --namespace AWS/Lambda `
  --statistic Sum `
  --period 300 `
  --threshold 5 `
  --comparison-operator GreaterThanThreshold `
  --dimensions Name=FunctionName,Value=Ec2ProxyFunction
```

This alerts if Lambda function has >5 errors in 5 minutes.

---

### 10.8 Disable Insecure Protocols

**Device uses:**
- HTTPS only (TLS 1.2+)
- AWS Root CA 1 certificate pinned (no MITM attacks)
- No fallback to HTTP

**Verify in firmware:**
```cpp
client->setCACert(AWS_ROOT_CA1);  // TLS validation
http.begin(*client, url);  // HTTPS required
```

---

### 10.9 Disable Password Auth on EC2

See Section 10.2 — SSH Hardening.

---

### 10.10 VPC and Networking

**For production, isolate Lambda:**

1. Place Lambda in private VPC subnet (no internet access)
2. Use VPC endpoint for API Gateway
3. Restrict EC2 security group to allow traffic only from Lambda security group

```bash
# On EC2 security group, instead of allowing SSH from 0.0.0.0:
# Inbound SSH: Source = sg-LAMBDA_SECURITY_GROUP (restrict to Lambda)
```

---

## APPENDIX A — QUICK REFERENCE

### Useful Commands

**Build Firmware:**
```powershell
pio run
```

**Upload to Device:**
```powershell
pio run --target upload
```

**Monitor Serial:**
```powershell
pio device monitor
```

**Deploy Lambda:**
```powershell
cd lambda/ec2_proxy
.\deploy.ps1 -StackName "ec2-proxy-stack" -Region "ap-south-1" -AdminToken "..." -PairCode "..." -TokenSigningKey "..."
```

**Start EC2:**
```powershell
aws ec2 start-instances --instance-ids i-XXXXX --region ap-south-1
```

**Stop EC2:**
```powershell
aws ec2 stop-instances --instance-ids i-XXXXX --region ap-south-1
```

**SSH to EC2:**
```powershell
ssh -i C:\path\to\cardputer-ec2-key.pem ubuntu@EC2_PUBLIC_IP
```

**View Lambda Logs:**
```powershell
aws logs tail /aws/lambda/Ec2ProxyFunction --follow --region ap-south-1
```

---

### Default Values

| Setting | Default | Notes |
|---------|---------|-------|
| Access Token TTL | 900s (15 min) | Changeable via deploy param |
| Refresh Token TTL | 2592000s (30 days) | Changeable via deploy param |
| API Throttle | 5 req/sec | Set in template.yaml |
| Device PIN | None | User-defined, 4 digits, numeric only |
| EC2 Instance Type | t2.micro | Free tier eligible |
| Lambda Memory | 128 MB | Sufficient for EC2 API calls |
| DynamoDB Billing | Pay-per-request | No provisioned capacity |

---

### Directory Structure

```
aws-cardputer/
├── src/
│   └── main.cpp                 # Firmware (900+ lines)
├── include/
│   └── hardware_config.h        # Constants
├── platformio.ini               # Build config
├── README.md                    # Project overview
├── lambda/
│   ├── ec2_proxy/
│   │   ├── handler.py           # Lambda function
│   │   ├── template.yaml        # IaC (SAM)
│   │   ├── deploy.ps1           # Deploy script
│   │   ├── requirements.txt     # Python dependencies
│   │   └── tests/
│   │       └── test_handler.py  # Unit tests
```

---

### Estimated Costs (First Year)

| Service | Usage | Cost |
|---------|-------|------|
| EC2 (t2.micro) | 720 hrs/month | Free (free tier) |
| Data transfer | ~15 GB/month | Free (free tier) |
| Lambda | ~100 invocations/day | ~$0.20/month |
| DynamoDB | ~1 GB stored | ~$0.25/month |
| API Gateway | ~3000 calls/month | ~$1/month |
| **Total** | | **~$1.50/month** (after free tier) |

---

### Support and Issues

**If problems persist:**

1. Check [Section 9 — TROUBLESHOOTING](#section-9--troubleshooting)
2. Review project README: `aws-cardputer/README.md`
3. Check Lambda logs (Section 8.5)
4. Open GitHub issue with:
   - Error message
   - Firmware logs (`pio device monitor`)
   - Lambda logs
   - Device configuration (sanitized)

---

## END OF DEPLOYMENT GUIDE

**Congratulations!** Your M5Stack Cardputer is now connected to AWS and can control EC2 instances.

**Next Steps:**
- Experiment with multiple EC2 instances
- Implement SSH integration for terminal access
- Deploy to production with hardened security (Section 10)
- Share feedback with the project maintainers

---
