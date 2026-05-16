# PocketCloud Terminal — Handheld AWS EC2 Controller for M5Stack Cardputer

A lightweight handheld cloud control firmware for the M5Stack Cardputer v1.1. It enables secure monitoring and management of AWS EC2 instances directly from a portable device. 

## Project Overview

PocketCloud Terminal turns your M5Stack Cardputer into a portable EC2 remote controller. It connects to Wi-Fi, fetches instance states, and can start or stop instances using a secure API Gateway/Lambda proxy.

**Architecture Flow:**

Cardputer -> Wi-Fi -> Authentication -> API Gateway + Lambda -> AWS EC2

### Prerequisites

Before deploying the backend proxy, ensure you have set up your AWS environment:
1. **AWS Account**: You need an active AWS Account.
2. **IAM User**: Create an IAM User with AdministratorAccess, or equivalent permissions for CloudFormation, Lambda, API Gateway, IAM, and DynamoDB.
3. **AWS CLI Setup**: Install the [AWS CLI](https://aws.amazon.com/cli/) and run `aws configure` to set your `AWS Access Key ID`, `AWS Secret Access Key`, and default region name, for example `ap-south-1`.
4. **AWS SAM CLI**: Install the [AWS SAM CLI](https://docs.aws.amazon.com/serverless-application-model/latest/developerguide/install-sam-cli.html).

## Project Structure

```text
aws-cardputer/
├── DEPLOYMENT_GUIDE.md        # Comprehensive deployment and setup instructions
├── README.md                  # This document
├── include/
│   └── hardware_config.h      # Hardware constants and configuration
├── lambda/
│   └── ec2_proxy/
│       ├── deploy.ps1         # Windows deployment script for AWS SAM
│       ├── handler.py         # Lambda backend Python logic
│       ├── requirements.txt   # Lambda dependencies
│       └── template.yaml      # AWS SAM template for the API proxy
├── platformio.ini             # PlatformIO build configuration
└── src/
    └── main.cpp               # Main firmware source code
```

## How It Works

1. **Boot**: The device boots and plays an animated initialization sequence.
2. **Network**: It connects to a saved Wi-Fi network or prompts the user to scan and connect.
3. **Setup**: The device loads AWS credentials from non-volatile storage (NVS) or SD card. An embedded local web server starts, allowing easy configuration via a browser.
4. **Fetching**: The user presses `[E]` to fetch EC2 instances. The firmware authenticates with AWS via the API proxy.
5. **Control**: The display lists instances with color-coded status indicators. The user selects an instance and presses `[T]` or `[E]` to toggle power (start/stop).

## Installation

```bash
# Clone the repository
git clone https://github.com/ch1n7u/aws-cardputer.git
cd aws-cardputer

# Install PlatformIO dependencies
pio lib install

# Build the firmware
pio run
```

## Firmware Build

Upload the firmware to the Cardputer over USB using PlatformIO:

```bash
# Upload to device
pio run --target upload

# Monitor serial output
pio device monitor --baud 115200
```

## AWS Deployment

To securely route requests from your Cardputer to your AWS EC2 instances, deploy the backend proxy to your AWS account. The backend uses API Gateway, AWS Lambda, and DynamoDB.

### Prerequisites

Before deploying the backend proxy, ensure you have set up your AWS environment:
1. **AWS Account**: You need an active AWS Account.
2. **IAM User**: Create an IAM User with AdministratorAccess, or equivalent permissions for CloudFormation, Lambda, API Gateway, IAM, and DynamoDB.
3. **AWS CLI Setup**: Install the [AWS CLI](https://aws.amazon.com/cli/) and run `aws configure` to set your `AWS Access Key ID`, `AWS Secret Access Key`, and default region name, for example `ap-south-1`.
4. **AWS SAM CLI**: Install the [AWS SAM CLI](https://docs.aws.amazon.com/serverless-application-model/latest/developerguide/install-sam-cli.html).

### Deployment Steps

1. Open PowerShell and, if script execution is restricted, allow local scripts for this session:

```powershell
Set-ExecutionPolicy -ExecutionPolicy RemoteSigned -Scope CurrentUser
```

2. Change into the backend deployment folder:

```powershell
cd .\lambda\ec2_proxy
```

3. Deploy the stack. The script runs `sam build` and `sam deploy` for you, and it generates missing secrets if you do not pass them:

```powershell
.\deploy.ps1 -StackName "ec2-proxy-stack" -Region "ap-south-1"
```

If you want to provide your own values instead of auto-generated secrets:

```powershell
.\deploy.ps1 -StackName "ec2-proxy-stack" -Region "ap-south-1" -AdminToken "YOUR_ADMIN_TOKEN" -PairCode "YOUR_PAIR_CODE" -TokenSigningKey "YOUR_TOKEN_SIGNING_KEY"
```

After the script completes it will print only the `AdminToken`, `PairCode`, and the API Gateway URL. Please save the `AdminToken` and `PairCode` and update the device configuration in the web interface.

4. If you need to run the SAM build manually, use:

```powershell
sam build --template-file template.yaml
```

5. Copy the `Ec2ProxyApiEndpoint` value from the deployment output and enter it into the device configuration.

Example output values are shown in [DEPLOYMENT_GUIDE.md](DEPLOYMENT_GUIDE.md) if you want a full Windows setup walkthrough.

## Device Configuration

Configuration can be performed directly on the device by pressing `[S]` or via the local web interface by navigating to the device's IP address.

**Configuration Options:**
- **AWS Proxy**: API Gateway URL, Pair Code, Legacy Admin Token.
- **Security**: Device 4-digit PIN lock.
- **Network**: Wi-Fi SSID and Password.

Settings can be exported/imported using an SD card with an `/ec2.conf` file.

## Security Model

- **Proxy Auth**: Uses a pairing code exchange to retrieve short-lived access and refresh tokens, verified via HMAC SHA-256.
- **Storage Security**: All credentials stored in Preferences NVS are XOR encoded using a hardware-specific MAC address key to prevent casual dumping.
- **Access Control**: The on-device settings UI is protected by a 4-digit PIN.

## Troubleshooting

- **Upload Failures**: Unplug/replug the USB cable. Ensure M5Stack Cardputer drivers (USB to UART Bridge) are properly installed.
- **Wi-Fi Issues**: Ensure you are connecting to a 2.4GHz network. The ESP32-S3 does not support 5GHz Wi-Fi.
- **AWS Auth Failure**: Check device clock synchronization. TLS and AWS requests require accurate time via NTP. Verify your pairing code via the local web interface `/debug` endpoint.
- **SAM CLI Missing**: Ensure `aws-sam-cli` is installed and in your system PATH. Restart PowerShell after installation.

## Limitations

- **No SSH**: SSH terminal access is NOT currently implemented.
- **Memory Limits**: The EC2 instance list is constrained to a predefined maximum limit to save RAM.
- **Display Constraints**: The UI displays abbreviated instance names to fit the Cardputer screen.
- **Wi-Fi Bands**: Only supports 2.4GHz Wi-Fi.

**Planned Features:**
- SSH terminal access integration
- Command relay mode
- Full ANSI color terminal output

## Contributing

Contributions are welcome! Please adhere to standard pull request workflows and ensure the firmware builds successfully before submitting changes.

## License

This project is open-source and licensed under the MIT License.
