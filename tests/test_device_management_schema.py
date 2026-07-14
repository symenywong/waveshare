import json
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SCHEMA_PATH = ROOT / "contracts/device-management.schema.json"


def test_device_status_contract_is_strict_and_contains_no_secret_values():
    schema = json.loads(SCHEMA_PATH.read_text(encoding="utf-8"))
    status = schema["$defs"]["deviceStatus"]

    assert status["additionalProperties"] is False
    assert status["properties"]["config"]["additionalProperties"] is False
    config_fields = set(status["properties"]["config"]["properties"])
    assert config_fields == {
        "available",
        "revision",
        "chatProvider",
        "chatModel",
        "hasChatApiKey",
        "hasAsrApiKey",
    }
    serialized = json.dumps(status).lower()
    assert "password" not in serialized


def test_root_contract_accepts_device_status_envelope():
    schema = json.loads(SCHEMA_PATH.read_text(encoding="utf-8"))

    assert {"$ref": "#/$defs/deviceStatus"} in schema["oneOf"]
    assert {"$ref": "#/$defs/wifiUpdateAccepted"} in schema["oneOf"]
    assert schema["$defs"]["deviceStatus"]["properties"]["latestOperation"] == {
        "$ref": "#/$defs/managementOperation"
    }
