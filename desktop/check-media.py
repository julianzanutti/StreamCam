"""Synthetic H.264 sender -> native receiver over real DTLS/SRTP, no camera/audio."""
import asyncio
import json
import os
import sys
from pathlib import Path
from aiortc import RTCPeerConnection, RTCConfiguration, RTCSessionDescription, RTCRtpSender, VideoStreamTrack
from aiortc.sdp import candidate_from_sdp
from av import VideoFrame

class SyntheticVideo(VideoStreamTrack):
    def __init__(self,width,height):
        super().__init__()
        self.width,self.height=width,height
    async def recv(self):
        pts, time_base = await self.next_timestamp()
        frame = VideoFrame(self.width, self.height, 'yuv420p')
        for index, plane in enumerate(frame.planes):
            plane.update(bytes([80 if index == 0 else 128]) * plane.buffer_size)
        frame.pts, frame.time_base = pts, time_base
        return frame

async def main():
    root = Path(__file__).resolve().parent.parent
    obs_process = None
    obs_log = None
    if '--obs' in sys.argv:
        environment = os.environ.copy()
        environment['PATH'] = str(Path(os.environ.get('OBS_STUDIO_DIR',r'C:\Program Files\obs-studio'))/'bin'/'64bit') + os.pathsep + environment.get('PATH', '')
        obs_log = open(root / '.tooling/obs-media-probe.log', 'wb')
        obs_process = await asyncio.create_subprocess_exec(str(root / 'obs-plugin/out/Release/StreamCamObsFrameProbe.exe'),
            str(root / 'obs-plugin/out/Release/streamcam-source.dll'), env=environment, stdout=obs_log, stderr=obs_log)
    process = await asyncio.create_subprocess_exec(str(root / 'desktop/out/Release/StreamCamMediaProbe.exe'),
        stdin=asyncio.subprocess.PIPE, stdout=asyncio.subprocess.PIPE)
    peer = RTCPeerConnection(RTCConfiguration(iceServers=[]))
    width,height=(1920,1080) if "--1080p" in sys.argv else (1280,720)
    track = SyntheticVideo(width,height)
    transceiver = peer.addTransceiver(track, direction='sendonly')
    transceiver.setCodecPreferences([codec for codec in RTCRtpSender.getCapabilities('video').codecs if codec.mimeType == 'video/H264'])
    try:
        await peer.setLocalDescription(await peer.createOffer())
        print('probe: offer ready', flush=True)
        process.stdin.write((json.dumps({'type': 'rtc.offer', 'sdp': peer.localDescription.sdp}) + '\n').encode())
        await process.stdin.drain()
        candidates = []
        answered = False
        async with asyncio.timeout(65):
            while line := await process.stdout.readline():
                if not line.startswith(b'{'):
                    continue
                message = json.loads(line)
                print('probe: received', message['type'], flush=True)
                if message['type'] == 'rtc.answer':
                    assert 'H264' in message['sdp'], 'No H.264 negotiated'
                    await peer.setRemoteDescription(RTCSessionDescription(message['sdp'], 'answer'))
                    answered = True
                    for candidate in candidates:
                        await peer.addIceCandidate(candidate)
                    candidates.clear()
                elif message['type'] == 'rtc.ice':
                    text = message['candidate'].removeprefix('candidate:')
                    candidate = candidate_from_sdp(text)
                    candidate.sdpMid = message['mid']
                    candidate.sdpMLineIndex = message['mline']
                    if answered:
                        await peer.addIceCandidate(candidate)
                    else:
                        candidates.append(candidate)
                elif message['type'] == 'probe.frame':
                    print('probe: decoded dimensions', message['width'], message['height'], 'bytes', message['bytes'], flush=True)
                    assert message['width'] == width and message['height'] == height
                    assert message['bytes'] == width * height * 4
                    assert await process.wait() == 0
                    if obs_process:
                        assert await asyncio.wait_for(obs_process.wait(), 20) == 0, 'OBS did not receive the decoded frame; inspect obs-media-probe.log'
                        print('Native H264 -> named pipe -> OBS source frame passed', flush=True)
                    print(f'H.264 DTLS/SRTP native decode passed: {width}x{height} BGRA frame', flush=True)
                    return
        raise RuntimeError(f'Native receiver exited without a frame: {await process.wait()}')
    finally:
        track.stop()
        await peer.close()
        if process.returncode is None:
            process.kill()
            await process.wait()
        if obs_process and obs_process.returncode is None:
            obs_process.kill()
            await obs_process.wait()
        if obs_log:
            obs_log.close()

asyncio.run(main())
