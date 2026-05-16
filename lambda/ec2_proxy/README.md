# EC2 Proxy Lambda

This Lambda provides a small HTTPS proxy that performs AWS EC2 actions (list, start, stop) using server-side credentials. The Cardputer device calls this API rather than performing SigV4 itself.

Auth model:
- One-time pairing: `POST /pair` with `deviceId` + `pairCode`
- Session refresh: `POST /refresh` with `deviceId` + `refreshToken`
- Protected EC2 endpoints require short-lived `Authorization: Bearer <accessToken>` and `X-Device-Id`.
- Admin-only revoke: `POST /revoke` with admin bearer token.

Security: set `ADMIN_TOKEN` when deploying and configure the device to include `Authorization: Bearer <token>` header on requests.

Deployment (AWS SAM):

1. Package & deploy. `deploy.ps1` can generate missing secrets automatically, so the simplest run is:

```powershell
./deploy.ps1 -StackName ec2-proxy-stack -Region us-east-1
```

If you want to supply your own values, pass `-AdminToken`, `-PairCode`, and `-TokenSigningKey` explicitly.

Or use guided deploy:

```bash
sam deploy --guided --template-file template.yaml --stack-name ec2-proxy-stack
```

During `sam deploy --guided` provide a value for `AdminToken` (a strong random string).

API endpoints (after deployment):

- POST /pair
  - Body: `{ "deviceId": "cardputer-001", "pairCode": "..." }`
  - Returns `{ accessToken, refreshToken, expiresIn }`
- POST /refresh
  - Body: `{ "deviceId": "cardputer-001", "refreshToken": "..." }`
  - Returns `{ accessToken, expiresIn }`
- POST /revoke
  - Requires admin bearer token
  - Body: `{ "deviceId": "cardputer-001" }`
- GET /instances
  - Requires short-lived access token + `X-Device-Id` header
- POST /instances/{instanceId}/start
  - Requires short-lived access token + `X-Device-Id` header
- POST /instances/{instanceId}/stop
  - Requires short-lived access token + `X-Device-Id` header
- POST /instances/{instanceId}/reboot
  - Requires short-lived access token + `X-Device-Id` header

Example curl (replace API_URL and TOKEN):

```bash
PAIR=$(curl -s -X POST https://API_ID.execute-api.REGION.amazonaws.com/Prod/pair -H "Content-Type: application/json" -d '{"deviceId":"cardputer-001","pairCode":"YOUR_PAIR_CODE"}')
ACCESS_TOKEN=$(echo "$PAIR" | jq -r '.accessToken')
REFRESH_TOKEN=$(echo "$PAIR" | jq -r '.refreshToken')

curl -H "Authorization: Bearer $ACCESS_TOKEN" -H "X-Device-Id: cardputer-001" https://API_ID.execute-api.REGION.amazonaws.com/Prod/instances
curl -X POST -H "Authorization: Bearer $ACCESS_TOKEN" -H "X-Device-Id: cardputer-001" https://API_ID.execute-api.REGION.amazonaws.com/Prod/instances/i-0123456789abcdef/start
curl -X POST -H "Authorization: Bearer $ACCESS_TOKEN" -H "X-Device-Id: cardputer-001" https://API_ID.execute-api.REGION.amazonaws.com/Prod/instances/i-0123456789abcdef/reboot
```

Notes:
- IAM is least-privilege for `DescribeInstances` and optional restricted `Start/Stop` instance ARNs via `AllowedInstanceArns`.
- Use TLS and rotate `AdminToken` periodically.
- API Gateway throttling is enabled in the SAM template.
- Device-side token/PIN storage uses device-bound obfuscated fields (`token_enc`, `pin_enc`) when written to SD or Preferences.

Run tests:

```bash
python -m pytest tests/test_handler.py
```