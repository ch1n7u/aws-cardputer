import os
import json
import time
import hmac
import base64
import hashlib
import secrets
import boto3

ADMIN_TOKEN = os.environ.get("ADMIN_TOKEN", "")
PAIR_CODE = os.environ.get("PAIR_CODE", "")
TOKEN_SIGNING_KEY = os.environ.get("TOKEN_SIGNING_KEY", "")
TABLE_NAME = os.environ.get("DEVICE_TABLE_NAME", "")
ACCESS_TTL_SECONDS = int(os.environ.get("ACCESS_TTL_SECONDS", "900"))
REFRESH_TTL_SECONDS = int(os.environ.get("REFRESH_TTL_SECONDS", str(30 * 24 * 3600)))

ec2 = boto3.client("ec2")
ddb = boto3.resource("dynamodb")


def _response(status_code, body):
    return {
        "statusCode": status_code,
        "headers": {
            "Content-Type": "application/json",
            "Access-Control-Allow-Origin": "*",
            "Access-Control-Allow-Headers": "Content-Type,Authorization,X-Device-Id",
            "Access-Control-Allow-Methods": "GET,POST,OPTIONS",
        },
        "body": json.dumps(body),
    }


def _headers(event):
    return event.get("headers") or {}


def _auth_bearer(event):
    headers = _headers(event)
    auth = headers.get("Authorization") or headers.get("authorization") or ""
    if auth.startswith("Bearer "):
        return auth.split(" ", 1)[1]
    return ""


def _authorized_admin(event):
    token = _auth_bearer(event)
    return bool(ADMIN_TOKEN) and token == ADMIN_TOKEN and len(token) >= 16


def _device_id_from_headers(event):
    headers = _headers(event)
    return headers.get("X-Device-Id") or headers.get("x-device-id") or ""


def _json_body(event):
    body = event.get("body")
    if not body:
        return {}
    if isinstance(body, str):
        return json.loads(body)
    return body


def _sign(data):
    return hmac.new(TOKEN_SIGNING_KEY.encode("utf-8"), data.encode("utf-8"), hashlib.sha256).hexdigest()


def _issue_access_token(device_id):
    exp = int(time.time()) + ACCESS_TTL_SECONDS
    payload = json.dumps({"deviceId": device_id, "exp": exp}, separators=(",", ":"))
    payload_b64 = base64.urlsafe_b64encode(payload.encode("utf-8")).decode("utf-8").rstrip("=")
    sig = _sign(payload_b64)
    return f"{payload_b64}.{sig}", exp


def _validate_access_token(token, expected_device_id):
    try:
        payload_b64, sig = token.split(".", 1)
    except ValueError:
        return False
    if _sign(payload_b64) != sig:
        return False
    padded = payload_b64 + "=" * ((4 - len(payload_b64) % 4) % 4)
    payload = json.loads(base64.urlsafe_b64decode(padded.encode("utf-8")).decode("utf-8"))
    if payload.get("exp", 0) < int(time.time()):
        return False
    return payload.get("deviceId") == expected_device_id


def _refresh_hash(refresh_token):
    return hashlib.sha256((refresh_token + TOKEN_SIGNING_KEY).encode("utf-8")).hexdigest()


def _device_table():
    if not TABLE_NAME:
        raise RuntimeError("DEVICE_TABLE_NAME not configured")
    return ddb.Table(TABLE_NAME)


def list_instances():
    out = []
    paginator = ec2.get_paginator("describe_instances")
    for page in paginator.paginate():
        for r in page.get("Reservations", []):
            for inst in r.get("Instances", []):
                name = ""
                for t in inst.get("Tags", []) or []:
                    if t.get("Key") == "Name":
                        name = t.get("Value")
                        break
                out.append({
                    "InstanceId": inst.get("InstanceId"),
                    "State": inst.get("State", {}).get("Name"),
                    "InstanceType": inst.get("InstanceType"),
                    "PrivateIpAddress": inst.get("PrivateIpAddress"),
                    "PublicIpAddress": inst.get("PublicIpAddress"),
                    "Name": name,
                })
    return out


def start_instance(instance_id):
    if not instance_id.startswith("i-"):
        raise ValueError("invalid instance id")
    resp = ec2.start_instances(InstanceIds=[instance_id])
    return resp


def stop_instance(instance_id):
    if not instance_id.startswith("i-"):
        raise ValueError("invalid instance id")
    resp = ec2.stop_instances(InstanceIds=[instance_id])
    return resp


def lambda_handler(event, context):
    try:
        method = event.get("httpMethod")
        resource = event.get("resource") or event.get("path")

        if method == "OPTIONS":
            return _response(200, {"ok": True})

        # POST /pair (one-time pairing)
        if method == "POST" and resource and "/pair" in resource:
            if not PAIR_CODE or not TOKEN_SIGNING_KEY:
                return _response(500, {"error": "server misconfigured"})
            body = _json_body(event)
            device_id = body.get("deviceId", "")
            pair_code = body.get("pairCode", "")
            if not device_id or not pair_code:
                return _response(400, {"error": "missing deviceId or pairCode"})
            
            pair_code_str = str(pair_code)
            expected_code_str = str(PAIR_CODE)
            
            if pair_code_str == expected_code_str:
                pass # Exact match
            elif pair_code_str.strip() == expected_code_str.strip():
                if pair_code_str.lower() == expected_code_str.lower():
                    return _response(401, {"error": "pair failed: whitespace mismatch"})
                else:
                    return _response(401, {"error": "pair failed: whitespace and case mismatch"})
            elif pair_code_str.strip().lower() == expected_code_str.strip().lower():
                return _response(401, {"error": "pair failed: case mismatch"})
            else:
                return _response(401, {"error": f"pair failed: invalid code (got length {len(pair_code_str)}, expected {len(expected_code_str)})"})

            refresh_token = secrets.token_urlsafe(32)
            refresh_hash = _refresh_hash(refresh_token)
            now = int(time.time())
            _device_table().put_item(
                Item={
                    "deviceId": device_id,
                    "refreshHash": refresh_hash,
                    "expiresAt": now + REFRESH_TTL_SECONDS,
                    "updatedAt": now,
                }
            )
            access_token, _ = _issue_access_token(device_id)
            return _response(
                200,
                {
                    "accessToken": access_token,
                    "refreshToken": refresh_token,
                    "expiresIn": ACCESS_TTL_SECONDS,
                },
            )

        # POST /refresh
        if method == "POST" and resource and "/refresh" in resource:
            if not TOKEN_SIGNING_KEY:
                return _response(500, {"error": "server misconfigured"})
            body = _json_body(event)
            device_id = body.get("deviceId", "")
            refresh_token = body.get("refreshToken", "")
            if not device_id or not refresh_token:
                return _response(400, {"error": "missing deviceId or refreshToken"})

            item = _device_table().get_item(Key={"deviceId": device_id}).get("Item")
            if not item:
                return _response(401, {"error": "unknown device"})
            if int(item.get("expiresAt", 0)) < int(time.time()):
                return _response(401, {"error": "refresh expired"})
            if item.get("refreshHash") != _refresh_hash(refresh_token):
                return _response(401, {"error": "invalid refresh token"})

            access_token, _ = _issue_access_token(device_id)
            return _response(200, {"accessToken": access_token, "expiresIn": ACCESS_TTL_SECONDS})

        # POST /revoke (admin only)
        if method == "POST" and resource and "/revoke" in resource:
            if not _authorized_admin(event):
                return _response(401, {"error": "unauthorized"})
            body = _json_body(event)
            device_id = body.get("deviceId", "")
            if not device_id:
                return _response(400, {"error": "missing deviceId"})
            _device_table().delete_item(Key={"deviceId": device_id})
            return _response(200, {"status": "revoked", "deviceId": device_id})

        # Protected EC2 actions require short-lived access token
        device_id = _device_id_from_headers(event)
        token = _auth_bearer(event)
        if not device_id or not token or not _validate_access_token(token, device_id):
            return _response(401, {"error": "unauthorized"})

        # GET /instances
        if method == "GET" and resource and (resource == "/instances" or resource.endswith("/instances")):
            instances = list_instances()
            return _response(200, {"instances": instances})

        # POST /instances/{instanceId}/start
        if method == "POST" and resource and "/instances/{instanceId}/start" in resource:
            pid = event.get("pathParameters", {}).get("instanceId")
            if not pid:
                return _response(400, {"error": "missing instanceId"})
            try:
                start_instance(pid)
            except ValueError as ve:
                return _response(400, {"error": str(ve)})
            return _response(200, {"status": "starting", "instanceId": pid})

        # POST /instances/{instanceId}/stop
        if method == "POST" and resource and "/instances/{instanceId}/stop" in resource:
            pid = event.get("pathParameters", {}).get("instanceId")
            if not pid:
                return _response(400, {"error": "missing instanceId"})
            try:
                stop_instance(pid)
            except ValueError as ve:
                return _response(400, {"error": str(ve)})
            return _response(200, {"status": "stopping", "instanceId": pid})

        return _response(404, {"error": "not found"})
    except Exception:
        return _response(500, {"error": "internal server error"})
