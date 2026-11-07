"""Independent signed pairing/reconnect/revocation check; secrets remain in pipes/memory."""
import asyncio
import base64
import hashlib
import json
import ssl
import os
from pathlib import Path
from cryptography import x509
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import rsa, padding
from websockets.asyncio.client import connect

async def main():
    root=Path(__file__).resolve().parent.parent
    process=await asyncio.create_subprocess_exec(os.environ.get('STREAMCAM_AUTH_PROBE',str(root/'desktop/out/Release/StreamCamAuthProbe.exe')),stdout=asyncio.subprocess.PIPE)
    key=rsa.generate_private_key(public_exponent=65537,key_size=2048)
    public=key.public_key().public_bytes(serialization.Encoding.DER,serialization.PublicFormat.SubjectPublicKeyInfo)
    device_id=hashlib.sha256(public).hexdigest()
    context=ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT)
    context.check_hostname=False; context.verify_mode=ssl.CERT_NONE
    def sign(message):
        return base64.b64encode(key.sign(message.encode(),padding.PKCS1v15(),hashes.SHA256())).decode()
    async def accepted(socket):
        while True:
            response=json.loads(await socket.recv())
            if response['type']=='pair.pending':continue
            return response['type']=='pair.accepted'
    async def invitation():
        while line:=await process.stdout.readline():
            if line.startswith(b'{'):
                message=json.loads(line)
                if message.get('type')=='probe.invitation':return message['payload']
        raise RuntimeError('Probe did not create invitation')
    try:
        async with asyncio.timeout(60):
            for stage in range(3):
                invite=await invitation()
                async with connect(invite['endpoint'],ssl=context,proxy=None,ping_interval=3,ping_timeout=3) as socket:
                    certificate=x509.load_der_x509_certificate(socket.transport.get_extra_info('ssl_object').getpeercert(True))
                    public_pc=certificate.public_key().public_bytes(serialization.Encoding.DER,serialization.PublicFormat.SubjectPublicKeyInfo)
                    assert hashlib.sha256(public_pc).hexdigest()==invite['pc_key_sha256'],'PC pin mismatch'
                    if os.environ.get('STREAMCAM_AUTH_USB'):
                        await socket.send(json.dumps({'version':1,'type':'usb.hello','device_id':device_id,'device_key':base64.b64encode(public).decode()}))
                        response=json.loads(await socket.recv())
                        if stage==2:
                            assert response['type']=='pair.rejected','Revoked USB device was not rejected'
                            print('USB without QR: pinned TLS, signed fresh challenge, reconnect and revocation passed',flush=True)
                            return
                        assert response['type']=='usb.challenge'
                        proof=sign('StreamCam USB v1\n'+invite['pc_id']+'\n'+response['challenge'])
                        await socket.send(json.dumps({'version':1,'type':'usb.proof','device_id':device_id,'device_proof':proof}))
                    elif stage==0:
                        proof=sign('StreamCam pair v1\n'+invite['pc_id']+'\n'+invite['nonce'])
                        await socket.send(json.dumps({'version':1,'type':'pair.request','token':invite['token'],'nonce':invite['nonce'],
                            'device_id':device_id,'device_key':base64.b64encode(public).decode(),'device_proof':proof}))
                    else:
                        await socket.send(json.dumps({'version':1,'type':'reconnect.hello','device_id':device_id}))
                        response=json.loads(await socket.recv())
                        if stage==2:
                            assert response['type']=='pair.rejected','Revoked device was not rejected'
                            print('Signed TLS pairing, fresh challenge reconnect and revoked-device rejection passed',flush=True)
                            return
                        assert response['type']=='reconnect.challenge'
                        proof=sign('StreamCam reconnect v1\n'+invite['pc_id']+'\n'+response['challenge'])
                        await socket.send(json.dumps({'version':1,'type':'reconnect.proof','device_id':device_id,'device_proof':proof}))
                    assert await accepted(socket),'Signed pairing/reconnect rejected'
    finally:
        if process.returncode is None:
            process.terminate(); await process.wait()

asyncio.run(main())
