"""Independent decoder check for the public, expired fixture, never a real QR."""
from pathlib import Path
import json
import zxingcpp
from PIL import Image

root = Path(__file__).resolve().parent.parent
result = zxingcpp.read_barcode(Image.open(root / '.tooling/public-qr-fixture.png'))
assert result is not None, 'QR was not decoded'
payload = json.loads(result.text)
assert payload == {
    'version': 1,
    'endpoint': 'wss://10.77.0.1:12345/pair',
    'pc_id': 'fixture-not-real-pc',
    'pc_key_sha256': '0' * 64,
    'token': 'A' * 43,
    'expires_at': '2000-01-01T00:00:00Z',
    'nonce': 'A' * 22,
}, 'Decoded invitation differs from the fixture'
print('QR independent decode passed: all invitation fields preserved')
