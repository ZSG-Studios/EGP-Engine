"""Private stdin bootstrap; public telemetry only over SSH. No credential files."""
import base64, json, subprocess, sys, time, hashlib, threading, os, ctypes
from pathlib import Path

root = Path(__file__).resolve().parents[2]
out = root / '.build/diagnostics/superpos-100'
out.mkdir(parents=True, exist_ok=True)
config = json.loads(sys.stdin.readline())
if config.get('local_server'):
    ctypes.windll.kernel32.SetProcessAffinityMask(ctypes.c_void_p(ctypes.windll.kernel32.GetCurrentProcess()),15)
config['role'] = 'server'
config['telemetry'] = str(out / 'server.json')
config['control'] = str(out / 'epochs.json')
control_path=out / 'epochs.json'
control_path.write_text(json.dumps({str(p['id']):1 for p in config['peers']}))
def read_control():
    for line in sys.stdin:
        try:
            epochs=json.loads(line)
            temp=out / 'epochs.tmp'
            temp.write_text(json.dumps(epochs))
            for _ in range(10):
                try:
                    os.replace(temp,control_path)
                    break
                except PermissionError:
                    time.sleep(0.005)
        except (ValueError,OSError):
            pass
threading.Thread(target=read_control,daemon=True).start()
engine = root / 'bin/godot.windows.editor.dev.x86_64.mono.exe'
with (out / 'server.log').open('w') as stdout, (out / 'server-error.log').open('w') as stderr:
    process = subprocess.Popen([str(engine), '--headless', '--path', str(Path(__file__).parent), '--max-fps', '120'],
                               stdin=subprocess.PIPE, stdout=stdout, stderr=stderr, text=True)
    if config.get('local_server'):
        ctypes.windll.kernel32.SetProcessAffinityMask(ctypes.c_void_p(int(process._handle)),15)
        ctypes.windll.kernel32.SetPriorityClass(ctypes.c_void_p(int(process._handle)),ctypes.c_uint32(0x8000))
    encoded=base64.b64encode(json.dumps(config).encode()).decode()
    process.stdin.write('\n'.join(encoded[i:i+512] for i in range(0,len(encoded),512))+'\nEND\n')
    process.stdin.flush()
    process.stdin.close()
    print('SERVER_HOST ' + json.dumps({'pid': process.pid, 'engine_sha256': hashlib.sha256(engine.read_bytes()).hexdigest()}), flush=True)
    last = None
    deadline = config['start_unix'] + config['duration'] + 20
    try:
        while process.poll() is None and time.time() < deadline:
            try:
                text = (out / 'server.json').read_text()
                report = json.loads(text)
                if text != last:
                    print('SERVER_TELEMETRY ' + json.dumps(report, separators=(',', ':')), flush=True)
                    last = text
            except (OSError, ValueError):
                pass
            time.sleep(0.25)
        if process.poll() is None:
            process.kill()
            raise RuntimeError('Remote server watchdog expired')
        # The final full telemetry is written just before exit: relay it too.
        try:
            text = (out / 'server.json').read_text()
            if text != last:
                print('SERVER_TELEMETRY ' + json.dumps(json.loads(text), separators=(',', ':')), flush=True)
        except (OSError, ValueError):
            pass
        print('SERVER_EXIT ' + str(process.returncode), flush=True)
        if process.returncode:
            # Error text can contain script locations, never the bootstrap.
            print('SERVER_ERROR ' + (out / 'server-error.log').read_text()[-6000:], flush=True)
        sys.exit(process.returncode)
    finally:
        if process.poll() is None:
            process.kill()
