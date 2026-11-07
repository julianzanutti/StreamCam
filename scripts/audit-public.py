"""Audit Git-visible source without printing secret values. Does not publish anything."""
from pathlib import Path
import re
import subprocess
import sys

root=Path(__file__).resolve().parent.parent
files=subprocess.check_output(['git','ls-files','--cached','--others','--exclude-standard','-z'],cwd=root).decode().split('\0')
rules={
    'private key':re.compile(rb'-----BEGIN (?:RSA |EC |OPENSSH |ENCRYPTED )?PRIVATE KEY-----[\r\n]+[A-Za-z0-9+/]{64}'),
    'credential token':re.compile(rb'(?:gh[pousr]_[A-Za-z0-9]{30,}|github_pat_[A-Za-z0-9_]{40,}|sk-[A-Za-z0-9]{40,}|AKIA[0-9A-Z]{16})'),
    'personal Windows path':re.compile(rb'[A-Za-z]:[\\/]Users[\\/][A-Za-z0-9_.-]+[\\/]'),
}
forbidden={'.apk','.aab','.exe','.dll','.pdb','.jks','.keystore','.pfx','.p12','.key','.pem','.log'}
issues=[]
for name in sorted(set(files)):
    if not name:continue
    path=root/name
    if path.suffix.lower() in forbidden:issues.append((name,'generated binary or private file'));continue
    if not path.is_file():continue
    data=path.read_bytes()
    for label,pattern in rules.items():
        if pattern.search(data):issues.append((name,label))
if issues:
    for name,label in issues:print(f'{name}: {label}')
    print(f'FAILED: {len(issues)} findings; no secret values printed.')
    sys.exit(1)
print(f'PASS: {len([name for name in files if name])} Git-visible files; no matching private keys, tokens, personal paths or binary artifacts.')
