"""Load the built source using the user's installed libobs; no camera or recording."""
import ctypes as c
import os
from pathlib import Path

root = Path(__file__).resolve().parent.parent
obs_bin = Path(os.environ.get("OBS_STUDIO_DIR", r"C:\Program Files\obs-studio")) / "bin" / "64bit"
dll_dir = os.add_dll_directory(str(obs_bin))
obs = c.CDLL(str(obs_bin / "obs.dll"))
obs.obs_startup.argtypes = [c.c_char_p, c.c_char_p, c.c_void_p]
obs.obs_startup.restype = c.c_bool
obs.obs_open_module.argtypes = [c.POINTER(c.c_void_p), c.c_char_p, c.c_char_p]
obs.obs_open_module.restype = c.c_int
obs.obs_init_module.argtypes = [c.c_void_p]
obs.obs_init_module.restype = c.c_bool
obs.obs_source_create.argtypes = [c.c_char_p, c.c_char_p, c.c_void_p, c.c_void_p]
obs.obs_source_create.restype = c.c_void_p
obs.obs_source_release.argtypes = [c.c_void_p]
assert obs.obs_startup(b"en-US", None, None), "libobs startup failed"
try:
    module = c.c_void_p()
    plugin = root / "obs-plugin/out/Release/streamcam-source.dll"
    result = obs.obs_open_module(c.byref(module), os.fsencode(plugin), os.fsencode(plugin.parent))
    assert result == 0, f"obs_open_module failed: {result}"
    assert obs.obs_init_module(module), "obs_init_module failed"
    source = obs.obs_source_create(b"streamcam_source_v1", b"StreamCam load probe", None, None)
    assert source, "OBS could not create StreamCam source"
    obs.obs_source_release(source)
    print("OBS plugin load, source registration, creation and destruction passed", flush=True)
finally:
    obs.obs_shutdown()
