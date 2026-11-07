"""Public signature fixture generated with an ephemeral key; no private key saved."""
from pathlib import Path
import base64
import json
from cryptography.hazmat.primitives.asymmetric import rsa, padding
from cryptography.hazmat.primitives import hashes, serialization
key = rsa.generate_private_key(public_exponent=65537, key_size=2048)
message = b'StreamCam reconnect v1\npublic-test-PC\npublic-test-challenge-32-bytes-long'
fixture = {
    'key': base64.b64encode(key.public_key().public_bytes(serialization.Encoding.DER, serialization.PublicFormat.SubjectPublicKeyInfo)).decode(),
    'message': message.decode(),
    'signature': base64.b64encode(key.sign(message, padding.PKCS1v15(), hashes.SHA256())).decode(),
}
destination = Path(__file__).resolve().parent.parent / '.tooling/device-proof-fixture.json'
destination.write_text(json.dumps(fixture), encoding='utf-8')
print('Public device proof fixture generated; private key discarded')
