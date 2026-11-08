"""End-to-end regression for consent responses that arrive after their socket disconnects."""
import base64
import hashlib
import json
import os
import re
import queue
import ssl
import subprocess
import threading
import time
from pathlib import Path

from cryptography import x509
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import padding, rsa
from websockets.exceptions import ConnectionClosed
from websockets.sync.client import connect


def new_device():
    private = rsa.generate_private_key(public_exponent=65537, key_size=2048)
    public = private.public_key().public_bytes(
        serialization.Encoding.DER, serialization.PublicFormat.SubjectPublicKeyInfo
    )
    identity = hashlib.sha256(public).hexdigest()

    def sign(message):
        return base64.b64encode(private.sign(
            message.encode(), padding.PKCS1v15(), hashes.SHA256()
        )).decode()

    return public, identity, sign


def main():
    print("Stale consent check started", flush=True)
    root = Path(__file__).resolve().parent.parent
    executable = os.environ.get(
        "STREAMCAM_AUTH_PROBE", str(root / "desktop/out-v2/Release/StreamCamAuthProbe.exe")
    )
    print(f"Starting AuthProbe: {executable}", flush=True)
    environment = os.environ.copy()
    environment["STREAMCAM_AUTH_STALE_ATTEMPT"] = "1"
    environment["STREAMCAM_AUTH_USB"] = "1"  # Keep the integration test bound to loopback only.
    process = subprocess.Popen(
        [executable], stdout=subprocess.PIPE, stderr=subprocess.PIPE, env=environment,
    )
    output_lines = queue.Queue()

    def capture_output():
        for line in process.stdout:
            output_lines.put(line)
        output_lines.put(None)

    threading.Thread(target=capture_output, daemon=True).start()
    tls = ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT)
    tls.check_hostname = False
    tls.verify_mode = ssl.CERT_NONE

    def invitation():
        deadline = time.monotonic() + 20
        while True:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise TimeoutError("AuthProbe no publicó una invitación en 20 s")
            try:
                line = output_lines.get(timeout=remaining)
            except queue.Empty as error:
                raise TimeoutError("AuthProbe no publicó una invitación en 20 s") from error
            if line is None:
                raise RuntimeError("AuthProbe terminó antes de crear la invitación: "+process.stderr.read().decode(errors="replace"))
            if line.startswith(b"{"):
                message = json.loads(line)
                if message.get("type") == "probe.invitation":
                    print("Invite received", flush=True)
                    return message["payload"]

    def pair(invite, device, wait_for_acceptance=False):
        public, identity, sign = device
        with connect(invite["endpoint"], ssl=tls, proxy=None,
                     open_timeout=10, close_timeout=2, ping_interval=None) as socket:
            certificate = x509.load_der_x509_certificate(
                socket.socket.getpeercert(binary_form=True)
            )
            public_pc = certificate.public_key().public_bytes(
                serialization.Encoding.DER, serialization.PublicFormat.SubjectPublicKeyInfo
            )
            assert hashlib.sha256(public_pc).hexdigest() == invite["pc_key_sha256"], "PC pin mismatch"
            socket.send(json.dumps({
                "version": 1,
                "type": "usb.hello",
                "device_id": identity,
                "device_key": base64.b64encode(public).decode(),
            }))
            challenge = json.loads(socket.recv(timeout=12))
            assert challenge["type"] == "usb.challenge", "USB proof challenge was not issued"
            assert re.fullmatch(r"[0-9]{6}", challenge.get("verification_code", "")), "USB verification code is missing or malformed"
            proof = sign(f"StreamCam USB v1\n{invite['pc_id']}\n{challenge['challenge']}")
            socket.send(json.dumps({
                "version": 1,
                "type": "usb.proof",
                "device_id": identity,
                "device_proof": proof,
            }))
            while True:
                response = json.loads(socket.recv(timeout=12))
                if response["type"] == "pair.pending":
                    print("Pair pending", flush=True)
                    if not wait_for_acceptance:
                        return
                    with connect(invite["endpoint"], ssl=tls, proxy=None,
                                 open_timeout=10, close_timeout=2, ping_interval=None) as extra_socket:
                        while True:
                            response = json.loads(socket.recv(timeout=12))
                            if response["type"] == "pair.accepted":
                                print("Pair accepted", flush=True)
                                break
                            if response["type"] != "pair.pending":
                                raise AssertionError(f"Emparejamiento inesperado: {response['type']}")
                        try:
                            extra_socket.recv(timeout=3)
                        except ConnectionClosed:
                            return
                        raise AssertionError("Un socket sobrante siguió abierto después de aceptar al teléfono B")
                if response["type"] == "pair.accepted":
                    print("Pair accepted", flush=True)
                    if not wait_for_acceptance:
                        raise AssertionError("El teléfono A recibió aprobación después de desconectarse")
                    return
                raise AssertionError(f"Emparejamiento inesperado: {response['type']}")

    try:
        pair(invitation(), new_device())  # A se desconecta mientras espera confirmación.
        second_invite = invitation()
        pair(second_invite, new_device(), wait_for_acceptance=True)  # B debe esperar su propia aprobación.
        process.wait(timeout=10)
        saw_probe_pass = False
        while True:
            line = output_lines.get(timeout=3)
            if line is None:
                break
            if b"PASS: stale consent rejected" in line:
                saw_probe_pass = True
        if process.returncode != 0:
            raise RuntimeError(f"Probe falló ({process.returncode}): {process.stderr.read().decode(errors='replace')}")
        if not saw_probe_pass:
            raise RuntimeError("AuthProbe terminó sin verificar la respuesta antigua")
        print("PASS: consentimiento viejo rechazado y socket sobrante cerrado al aceptar B")
    finally:
        if process.poll() is None:
            process.terminate()
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait(timeout=5)


if __name__ == "__main__":
    main()
