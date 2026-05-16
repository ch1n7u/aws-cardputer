import json
import os
import sys
from unittest.mock import patch

sys.path.append(os.path.abspath(os.path.join(os.path.dirname(__file__), "..")))
os.environ.setdefault("AWS_DEFAULT_REGION", "us-east-1")

from handler import lambda_handler


def _event(method, path, resource, token="access-token", instance_id="i-1234567890abcdef0", device_id="cardputer-01", body=None):
    return {
        "httpMethod": method,
        "path": path,
        "resource": resource,
        "headers": {"Authorization": f"Bearer {token}", "X-Device-Id": device_id},
        "pathParameters": {"instanceId": instance_id},
        "body": json.dumps(body) if body is not None else None,
    }


@patch("handler._validate_access_token", return_value=True)
@patch("handler.list_instances")
def test_list_instances(mock_list, _mock_validate):
    mock_list.return_value = [{"InstanceId": "i-1", "State": "running", "Name": "web"}]
    resp = lambda_handler(_event("GET", "/instances", "/instances"), None)
    assert resp["statusCode"] == 200
    body = json.loads(resp["body"])
    assert len(body["instances"]) == 1


@patch("handler._validate_access_token", return_value=True)
@patch("handler.start_instance")
def test_start_instance(mock_start, _mock_validate):
    resp = lambda_handler(
        _event("POST", "/instances/i-1234567890abcdef0/start", "/instances/{instanceId}/start"),
        None,
    )
    assert resp["statusCode"] == 200
    mock_start.assert_called_once()


@patch("handler._validate_access_token", return_value=True)
@patch("handler.reboot_instance")
def test_reboot_instance(mock_reboot, _mock_validate):
    resp = lambda_handler(
        _event("POST", "/instances/i-1234567890abcdef0/reboot", "/instances/{instanceId}/reboot"),
        None,
    )
    assert resp["statusCode"] == 200
    mock_reboot.assert_called_once()


@patch("handler._validate_access_token", return_value=False)
def test_unauthorized(_mock_validate):
    evt = _event("GET", "/instances", "/instances", token="bad-token")
    resp = lambda_handler(evt, None)
    assert resp["statusCode"] == 401


@patch("handler._validate_access_token", return_value=True)
def test_invalid_instance_id(_mock_validate):
    evt = _event(
        "POST",
        "/instances/not-an-instance/start",
        "/instances/{instanceId}/start",
        instance_id="not-an-instance",
    )
    resp = lambda_handler(evt, None)
    assert resp["statusCode"] == 400


@patch("handler._validate_access_token", return_value=True)
def test_invalid_instance_id_stop(_mock_validate):
    evt = _event(
        "POST",
        "/instances/not-an-instance/stop",
        "/instances/{instanceId}/stop",
        instance_id="not-an-instance",
    )
    resp = lambda_handler(evt, None)
    assert resp["statusCode"] == 400


class _FakeTable:
    def __init__(self):
        self.item = None

    def put_item(self, Item):
        self.item = Item

    def get_item(self, Key):
        if self.item and self.item.get("deviceId") == Key.get("deviceId"):
            return {"Item": self.item}
        return {}

    def delete_item(self, Key):
        if self.item and self.item.get("deviceId") == Key.get("deviceId"):
            self.item = None


@patch("handler.PAIR_CODE", "pair-1234")
@patch("handler.TOKEN_SIGNING_KEY", "signing-secret-123")
def test_pair_and_refresh():
    table = _FakeTable()
    with patch("handler._device_table", return_value=table):
        pair_evt = _event(
            "POST",
            "/pair",
            "/pair",
            body={"deviceId": "cardputer-01", "pairCode": "pair-1234"},
        )
        pair_resp = lambda_handler(pair_evt, None)
        assert pair_resp["statusCode"] == 200
        pair_body = json.loads(pair_resp["body"])
        assert pair_body["refreshToken"]
        assert pair_body["accessToken"]
        assert table.item["deviceId"] == "cardputer-01"

        refresh_evt = _event(
            "POST",
            "/refresh",
            "/refresh",
            body={"deviceId": "cardputer-01", "refreshToken": pair_body["refreshToken"]},
        )
        refresh_resp = lambda_handler(refresh_evt, None)
        assert refresh_resp["statusCode"] == 200
        refresh_body = json.loads(refresh_resp["body"])
        assert refresh_body["accessToken"]
