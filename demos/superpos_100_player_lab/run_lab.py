"""101 native Superpos streams, remote Box3D server, seeded UDP fault injection."""
import argparse, base64, ctypes, hashlib, heapq, json, os, random, selectors
import secrets, socket, subprocess, sys, threading, time
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
OUT = ROOT / '.build/diagnostics/superpos-100'
HOST = 'Administrator@74.50.75.234'
KEY = Path.home() / '.ssh/build-helper-admin'
SSH = ['ssh', '-o', 'BatchMode=yes', '-o', 'ConnectTimeout=10', '-o', 'StrictHostKeyChecking=yes', '-i', str(KEY), HOST]
REMOTE_ROOT = r'C:\EGP Workspace\EGP-Engine'
SERVER_IP, CLIENT_IP, BASE = '10.77.64.1', '10.77.64.2', 48000
SINGLE_PORT = True
PROFILES = [
    dict(name='Fibre', up=5, down=7, jitter=1, loss=0.0005, duplicate=0, reorder=0, rate=1000000, burst=0),
    dict(name='Broadband', up=14, down=21, jitter=4, loss=0.003, duplicate=0.001, reorder=0.005, rate=300000, burst=0),
    dict(name='Busy WiFi', up=25, down=35, jitter=18, loss=0.02, duplicate=0.003, reorder=0.02, rate=100000, burst=0.015),
    dict(name='Mobile', up=75, down=95, jitter=35, loss=0.04, duplicate=0.003, reorder=0.03, rate=45000, burst=0.025),
    dict(name='Poor + blackout', up=120, down=150, jitter=65, loss=0.07, duplicate=0.005, reorder=0.04, rate=24000, burst=0.04),
]

def atomic_json(path, value):
    temp = path.with_suffix('.tmp')
    temp.write_text(json.dumps(value, indent=2))
    for _ in range(10):
        try:
            os.replace(temp, path)
            return
        except PermissionError:
            time.sleep(0.005)
    raise RuntimeError('Telemetry writer could not replace ' + str(path))

ABOVE_NORMAL, BELOW_NORMAL = 0x8000, 0x4000

def affinity(process=None, priority=None):
    # All lab processes share the same four logical CPUs. Priority decides who waits:
    # the packet proxy, server and playable window ahead of the 100 simulated bots.
    handle = ctypes.windll.kernel32.GetCurrentProcess() if process is None else int(process._handle)
    ctypes.windll.kernel32.SetProcessAffinityMask(ctypes.c_void_p(handle), ctypes.c_size_t(15))
    if priority:
        ctypes.windll.kernel32.SetPriorityClass(ctypes.c_void_p(handle), ctypes.c_uint32(priority))

def bootstrap(process, config):
    encoded=base64.b64encode(json.dumps(config).encode()).decode()
    process.stdin.write('\n'.join(encoded[i:i+512] for i in range(0,len(encoded),512))+'\nEND\n')
    process.stdin.flush()
    process.stdin.close()

def sync_remote_project():
    # Stream only this lab's source into the one canonical remote project.
    destination=REMOTE_ROOT+r'\demos\superpos_100_player_lab'
    create="New-Item -ItemType Directory -Force -Path '"+destination+"' | Out-Null"
    encoded=base64.b64encode(create.encode('utf-16le')).decode()
    subprocess.run(SSH+['powershell -NoProfile -NonInteractive -EncodedCommand '+encoded],capture_output=True,check=True,timeout=20)
    uploader=subprocess.Popen(SSH+[f'tar -C "{destination}" -xzf -'],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
    packer=subprocess.Popen(['tar','-C',str(HERE),'-czf','-','project.godot','main.tscn','lab.gd','gameplay.gd','character_view.gd','simulation_world.gd','exhibit_view.gd','playground.gd','remote_server.py'],stdout=uploader.stdin,stderr=subprocess.PIPE)
    uploader.stdin.close()
    _,packing_error=packer.communicate()
    uploader.communicate()
    if packer.returncode or uploader.returncode:
        raise RuntimeError('Remote lab source transfer failed')

class Proxy:
    def __init__(self, count, start, duration):
        self.selector = selectors.DefaultSelector()
        self.heap = []
        self.ordinal = 0
        self.start, self.duration = start, duration
        self.human_profile, self.human_offline_until = 0, 0.0
        self.bots = count - 1
        self.rows = []
        for peer in range(count):
            stats = dict(id=peer, packets=0, forwarded=0, dropped=0, duplicated=0, expedited=0, blackout_drops=0, overflow=0, bytes=0, max_queue_bytes=0)
            self.rows.append(stats)
            front, back = socket.socket(socket.AF_INET, socket.SOCK_DGRAM), socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            front.bind(('127.0.0.1', BASE+2000+peer))
            back.bind((CLIENT_IP, BASE+3000+peer))
            front.setblocking(False)
            back.setblocking(False)
            # The server listens on one port (single-port UDP); its connection ID prefix routes.
            server_port = BASE if SINGLE_PORT else BASE+peer
            for source, target, dest, direction in [(front, back, (SERVER_IP, server_port), 'up'), (back, front, ('127.0.0.1', BASE+1000+peer), 'down')]:
                lane = dict(peer=peer, source=source, target=target, dest=dest, direction=direction,
                            rng=random.Random(83117+peer*13+(1 if direction=='up' else 2)), jitter=0.0, bad=False, tail=0.0, queued=0, stats=stats)
                self.selector.register(source, selectors.EVENT_READ, lane)

    def admit(self, lane, payload, current):
        peer, stats, rng = lane['peer'], lane['stats'], lane['rng']
        stats['packets'] += 1
        age = current-self.start
        p = PROFILES[cohort(peer,self.bots)] if peer<self.bots else PROFILES[self.human_profile]
        if (30 <= age < 46 and peer<self.bots and cohort(peer,self.bots)==4) or (peer==self.bots and current<self.human_offline_until):
            stats['dropped'] += 1
            stats['blackout_drops'] += 1
            return
        recovery = age >= 55 and peer<self.bots and (self.duration<=300 or age<90)
        if recovery:
            p = dict(up=5,down=5,jitter=0,loss=0,duplicate=0,reorder=0,rate=1000000,burst=0)
        # Two-state correlated loss, asymmetric AR(1) jitter and bounded serialization.
        if lane['bad']:
            lane['bad'] = rng.random()>0.35
        elif rng.random()<p['burst']:
            lane['bad'] = True
        if rng.random() < (0.45 if lane['bad'] else p['loss']):
            stats['dropped'] += 1
            return
        lane['jitter'] = lane['jitter']*0.65+rng.gauss(0,p['jitter'])*0.76
        delay = max(0.001,(p[lane['direction']]+lane['jitter'])/1000)
        if rng.random()<p['reorder']:
            delay *= 0.05
            stats['expedited'] += 1
        rate = p['rate'] * (0.25 if 20<=age<27 and peer<self.bots and cohort(peer,self.bots) in (2,3) else 1)
        if lane['direction']=='up':
            rate *= 0.6
        lane['tail'] = max(current,lane['tail'])+len(payload)/rate
        due = lane['tail']+delay
        copies = 2 if rng.random()<p['duplicate'] else 1
        for copy in range(copies):
            if lane['queued']+len(payload)>65536 or len(self.heap)>=32768:
                stats['overflow'] += 1
                stats['dropped'] += 1
                continue
            self.ordinal += 1
            lane['queued'] += len(payload)
            stats['max_queue_bytes'] = max(stats['max_queue_bytes'],lane['queued'])
            heapq.heappush(self.heap,(due+copy*0.002,self.ordinal,lane,payload))
            if copy:
                stats['duplicated'] += 1

    def pump(self):
        current = time.time()
        for key, _ in self.selector.select(0.002):
            lane = key.data
            for _ in range(128):
                try:
                    payload, address = lane['source'].recvfrom(65536)
                except (BlockingIOError, ConnectionResetError):
                    break
                expected = ('127.0.0.1',BASE+1000+lane['peer']) if lane['direction']=='up' else (SERVER_IP,BASE if SINGLE_PORT else BASE+lane['peer'])
                if address==expected:
                    self.admit(lane,payload,current)
        current = time.time()
        while self.heap and self.heap[0][0]<=current:
            _, _, lane, payload = heapq.heappop(self.heap)
            lane['queued'] -= len(payload)
            try:
                lane['target'].sendto(payload,lane['dest'])
                lane['stats']['forwarded'] += 1
                lane['stats']['bytes'] += len(payload)
            except (OSError, BlockingIOError):
                lane['stats']['dropped'] += 1

    def close(self):
        for key in list(self.selector.get_map().values()):
            key.fileobj.close()
        self.selector.close()

def live_view(server_report, rows, bots, previous):
    """Server timing and per-cohort lockstep state from the native streams summary."""
    live = server_report.get('live', {})
    streams = live.get('streams', {})
    totals = live.get('command_totals', {})
    tick = streams.get('tick', 0)
    # Packed byte arrays reach JSON as encoded strings ("[0, 1, ...]").
    def array(key):
        value = streams.get(key, [])
        return json.loads(value) if isinstance(value, str) else value
    acknowledged, sent_bytes, enabled = array('acknowledged'), array('bytes'), array('enabled')
    repairs, redundancy = array('repairs'), array('redundancy')
    now = live.get('now', 0.0)
    dt = max(now - previous.get('now', now - 1.0), 0.001)
    old = previous.get('bytes', [])
    ready = {r['id']: r['ready'] for r in rows}
    cohorts = []
    for index, profile in enumerate(PROFILES):
        ids = [peer for peer in range(bots) if cohort(peer, bots) == index]
        lags = sorted((tick - acknowledged[peer]) * 1000.0 / 60.0 for peer in ids
                      if peer < len(acknowledged) and peer < len(enabled) and enabled[peer])
        rate = sum(sent_bytes[peer] - (old[peer] if peer < len(old) else 0) for peer in ids if peer < len(sent_bytes))
        cohorts.append(dict(name=profile['name'].split()[0].upper(), count=len(ids), ready=sum(1 for peer in ids if ready.get(peer)),
                            lag_ms_p50=round(lags[len(lags) // 2], 1) if lags else 0.0, lag_ms_max=round(lags[-1], 1) if lags else 0.0,
                            kBps=round(rate / max(len(ids), 1) / dt / 1000.0, 2) if old else 0.0,
                            repairs=sum(repairs[peer] for peer in ids if peer < len(repairs)),
                            redundant=sum(1 for peer in ids if peer < len(redundancy) and redundancy[peer] > 0)))
    ticks = max(totals.get('ticks', 0), 1)
    server = dict(tick_rate=live.get('tick_rate', 0), app_ms=live.get('application_ms', [0, 0])[0], app_max_ms=live.get('application_ms', [0, 0])[1],
                  network_ms=live.get('network_ms', [0, 0])[0], physics_ms=live.get('physics_ms', [0, 0])[0],
                  replication_ms=live.get('replication_ms', [0, 0])[0], interval_max_ms=live.get('interval_ms', [0, 0])[1],
                  bytes_per_tick=round(totals.get('encoded_bytes', 0) / ticks, 1), joining=live.get('joining', 0), tick=tick)
    return server, cohorts, dict(now=now, bytes=list(sent_bytes))


def cohort(peer, bots):
    """Network-profile cohort (0-4) of a bot: equal fifths of the bot ids."""
    return min(4, peer*5//max(bots,1))


def main():
    global SERVER_IP, CLIENT_IP, SINGLE_PORT
    parser = argparse.ArgumentParser()
    parser.add_argument('--duration', type=int, default=180)
    parser.add_argument('--bots', type=int, default=100)
    parser.add_argument('--bots-per-process', type=int, default=0, help='Bot clients per native process (default: about four processes, at least 25 each)')
    parser.add_argument('--no-human', action='store_true')
    parser.add_argument('--local-server', action='store_true', help='Run the native dedicated server on this PC over loopback')
    parser.add_argument('--tuning', default='{}', help='JSON transport overrides, e.g. {"human_lanes":2}')
    parser.add_argument('--engine-build', choices=('opt', 'dev'), default='opt', help='Optimized editor (default) or the dev build, on every peer')
    args = parser.parse_args()
    # 255 bots + the human = 256 clients, the planned per-server limit.
    if not 1<=args.bots<=255 or args.duration<15:
        parser.error('Use 1..255 bots and at least 15 seconds')
    OUT.mkdir(parents=True,exist_ok=True)
    # The parse-error watch reads these before each process rewrites its own log:
    # a previous run's error must not fail this one.
    for stale in [*OUT.glob('clients-*-error.log'),OUT/'human-error.log',OUT/'server-error.log']:
        stale.unlink(missing_ok=True)
    affinity(priority=ABOVE_NORMAL)
    SINGLE_PORT = bool(json.loads(args.tuning).get('single_port', 1))
    if args.local_server:
        SERVER_IP=CLIENT_IP="127.0.0.1"
    else:
        sync_remote_project()
    engine = ROOT / ('bin/godot.windows.editor.x86_64.mono.exe' if args.engine_build == 'opt' else 'bin/godot.windows.editor.dev.x86_64.mono.exe')
    if not engine.exists():
        sys.exit('LAB_ERROR missing engine build: ' + str(engine))
    count = args.bots+(not args.no_human)
    # The human takes the slot after the bots.
    human_id = args.bots
    start = time.time()+10
    peers = [dict(id=i,key=base64.b64encode(secrets.token_bytes(32)).decode()) for i in range(count)]
    epochs={str(i):1 for i in range(count)}
    retried_at={i:-100.0 for i in range(count)}
    control_path=OUT/'epochs.json'
    atomic_json(control_path,epochs)
    atomic_json(OUT/'human-network.json',dict(profile=0,offline_until=0))
    common = dict(total=count,bots=args.bots,human_id=human_id,server_ip=SERVER_IP,client_ip=CLIENT_IP,port_base=BASE,start_unix=start,duration=args.duration,control=str(control_path),local_server=args.local_server,tuning=json.loads(args.tuning),engine_build=args.engine_build,server_location="LOCAL PC" if args.local_server else "REMOTE BUILD PC")
    proxy = Proxy(count,start,args.duration)
    proxy.bots = args.bots
    processes, streams = [], []
    server_report, server_meta, server_exit = {}, {}, []
    remote_command = r'C:\EGPTools\python\python.exe "C:\EGP Workspace\EGP-Engine\demos\superpos_100_player_lab\remote_server.py"'
    server_command=[sys.executable,str(HERE/'remote_server.py')] if args.local_server else SSH+[remote_command]
    remote = subprocess.Popen(server_command,stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True,bufsize=1)
    processes.append(remote)
    remote.stdin.write(json.dumps(dict(common,peers=peers))+'\n')
    remote.stdin.flush()
    def read_server():
        for line in remote.stdout:
            if line.startswith('SERVER_TELEMETRY '):
                try:
                    server_report.clear()
                    received=json.loads(line[len('SERVER_TELEMETRY '):])
                    if abs(received.get('start_unix',0)-start)>0.001:
                        continue
                    server_report.update(received)
                    # Locally the server writes this file itself; copying an older relayed
                    # snapshot over it could replace its final full write.
                    if not args.local_server:
                        atomic_json(OUT/'server.json',dict(server_report))
                except (ValueError,OSError):
                    pass
            elif line.startswith('SERVER_HOST '):
                server_meta.update(json.loads(line[len('SERVER_HOST '):]))
                print(('LOCAL_SERVER_STARTED pid=' if args.local_server else 'REMOTE_SERVER_STARTED pid=')+str(server_meta['pid']),flush=True)
            elif line.startswith('SERVER_EXIT '):
                server_exit.append(int(line.split()[1]))
            elif line.startswith('SERVER_ERROR '):
                print(line.strip(),flush=True)
    threading.Thread(target=read_server,daemon=True).start()
    worker_paths = []
    env = os.environ.copy()
    env['DISABLE_VK_LAYER_reshade_1']='1'
    per_process = args.bots_per_process or max(25,-(-args.bots//4))
    for worker, offset in enumerate(range(0,args.bots,per_process)):
        role, selection = 'bots', peers[offset:min(offset+per_process,args.bots)]
        path = OUT/f'clients-{worker}.json'
        worker_paths.append(path)
        launch = dict(common,role=role,peers=selection,telemetry=str(path))
        stdout, stderr = (OUT/f'clients-{worker}.log').open('w'), (OUT/f'clients-{worker}-error.log').open('w')
        streams.extend([stdout,stderr])
        process = subprocess.Popen([str(engine),'--headless','--path',str(HERE),'--max-fps','60'],stdin=subprocess.PIPE,stdout=stdout,stderr=stderr,text=True,env=env)
        affinity(process,BELOW_NORMAL)
        processes.append(process)
        bootstrap(process,launch)
    if not args.no_human:
        path = OUT/'human.json'
        worker_paths.append(path)
        stdout, stderr = (OUT/'human.log').open('w'), (OUT/'human-error.log').open('w')
        streams.extend([stdout,stderr])
        launch = dict(common,role='human',peers=[peers[human_id]],telemetry=str(path),monitor=str(OUT/'monitor.json'))
        process = subprocess.Popen([str(engine),'--path',str(HERE),'--max-fps','60'],stdin=subprocess.PIPE,stdout=stdout,stderr=stderr,text=True,env=env)
        affinity(process,ABOVE_NORMAL)
        processes.append(process)
        bootstrap(process,launch)
        print('HUMAN_WINDOW_STARTED pid='+str(process.pid),flush=True)
    last_output, last_monitor = -100, -100
    live_previous = {}
    proxy_gaps, last_pump = [], time.time()
    try:
        while time.time()<start+args.duration+12:
            proxy.pump()
            pumped_at=time.time()
            if pumped_at-last_pump>0.03 and len(proxy_gaps)<200:
                proxy_gaps.append([round(pumped_at-start,2),round((pumped_at-last_pump)*1000,1)])
            last_pump=pumped_at
            age = time.time()-start
            if age-last_monitor>=0.5:
                last_monitor=age
                reports = []
                for path in worker_paths:
                    try:
                        report=json.loads(path.read_text())
                        if abs(report.get('start_unix',0)-start)<0.001:
                            reports.append(report)
                    except (OSError,ValueError):
                        pass
                rows = [row for report in reports for row in report['rows']]
                client_map={r['id']:r for r in rows}
                server_map={r['id']:r for r in server_report.get('rows',[])}
                try:
                    human=json.loads((OUT/'human-network.json').read_text())
                    proxy.human_profile=max(0,min(4,int(human['profile'])))
                    proxy.human_offline_until=float(human['offline_until'])
                except (OSError,ValueError,KeyError):
                    pass
                changed=False
                for peer in range(count):
                    sides=[mapping[peer] for mapping in (client_map,server_map) if peer in mapping]
                    # Coordinated fresh-epoch retry only for a terminal native error.
                    # During a known outage, wait for the path to return first.
                    if len(sides)!=2 or (30<=age<46 and peer<args.bots and cohort(peer,args.bots)==4) or (peer==human_id and time.time()<proxy.human_offline_until):
                        continue
                    if any(r['terminal'] for r in sides) and all(r['generation']==epochs[str(peer)] for r in sides) and age-retried_at[peer]>=10:
                        epochs[str(peer)]+=1
                        retried_at[peer]=age
                        changed=True
                        print(f"STREAM_RETRY id={peer} fresh_epoch={epochs[str(peer)]}",flush=True)
                if changed:
                    atomic_json(control_path,epochs)
                    remote.stdin.write(json.dumps(epochs)+'\n')
                    remote.stdin.flush()
                phase = 'WARMUP' if age<0 else 'BLACKOUT' if 30<=age<46 else 'RECONNECT' if 46<=age<55 else 'RECOVERY / CLEAN PATH' if age>=55 else 'MIXED INTERNET'
                cohort_summary=' / '.join(str(sum(r['ready'] for r in rows if r['id']<args.bots and cohort(r['id'],args.bots)==i)) for i in range(5))
                server_view, cohort_view, live_previous = live_view(server_report, rows, args.bots, live_previous)
                atomic_json(OUT/'monitor.json',dict(phase=phase,elapsed=age,bots_ready=sum(r['ready'] for r in rows if r['id']<args.bots),server_ready=sum(r['ready'] for r in server_report.get('rows',[])),cohort_summary=cohort_summary,server=server_view,cohorts=cohort_view))  # HUD-sized: the client parses it on its main thread
                if age-last_output>=10:
                    last_output=age
                    human_live = next((report.get('live', {}).get('det', {}) for report in reports if report.get('role') == 'human'), {})
                    lag_text = ' '.join(f"{k['name']}={k['ready']}/{k['count']},{k['lag_ms_p50']:.0f}/{k['lag_ms_max']:.0f}ms,{k['kBps']:.1f}kB/s,r{k['repairs']},x{k['redundant']}" for k in cohort_view)
                    print(f"LAB_PROGRESS t={age:.0f}s phase={phase} clients={sum(r['ready'] for r in rows)}/{count} server={server_view['tick_rate']}Hz app={server_view['app_ms']}/{server_view['app_max_ms']}ms {server_view['bytes_per_tick']}B/tick | {lag_text} | human lag={human_live.get('lag_ticks','-')}t buffer={human_live.get('buffered','-')} rtt={human_live.get('srtt_ms','-')}ms",flush=True)
                for process in processes[1:]:
                    if process.poll() not in (None,0):
                        raise RuntimeError('A native client failed; inspect fixed diagnostics logs')
                # A script that fails to parse leaves the engine idling: fail fast.
                for log in [*OUT.glob('clients-*-error.log'),OUT/'human-error.log',OUT/'server-error.log']:
                    try:
                        if 'Parse Error' in log.read_text(errors='replace'):
                            raise RuntimeError('Script parse error in '+log.name)
                    except OSError:
                        pass
                if remote.poll() not in (None,0):
                    raise RuntimeError('Remote server failed; inspect server error log')
            if all(p.poll() is not None for p in processes):
                break
        for process in processes:
            if process.poll() is None:
                raise RuntimeError('Lab watchdog: process did not finish')
        rows = [row for path in worker_paths for row in json.loads(path.read_text())['rows']]
        server_rows = server_report.get('rows',[])
        errors = []
        if len(rows)!=count or len(server_rows)!=count:
            errors.append('Missing per-client telemetry')
        for row in rows:
            deterministic_row=row.get('bot_loaded') or row.get('deterministic')
            if not row['ever_ready'] or not row['ready'] or row['received']<10 or row['applied']<10 or row['max_pending']>2 or (not deterministic_row and row.get('exhibit_received',0)<2) or (row['id']<args.bots and row.get('keyframes',1)<1):
                errors.append('Client qualification failed id='+str(row['id']))
        for row in server_rows:
            if not row['ever_ready'] or not row['ready'] or row['received']<10 or (row['id']<args.bots and row['distance']<3):
                errors.append('Server qualification failed id='+str(row['id']))
        if args.duration>=65 and args.bots>=20:
            blackout=[r for r in rows if r['id']<args.bots and cohort(r['id'],args.bots)==4]
            # A session may ride out the 9 s blackout inside its progress timeout or fail
            # and re-admit on a fresh epoch; either way every blackout client must return.
            if not all(r['ready'] for r in blackout):
                errors.append('Blackout cohort did not recover')
            if not any(r['generation']>=2 and r['recoveries']>=1 for r in blackout):
                errors.append('Fresh-epoch re-admission was not exercised')
            if not sum(r['blackout_drops'] for r in proxy.rows):
                errors.append('Blackout was not exercised on actual packets')
        human_row=next((r for r in rows if r['id']==human_id),None)
        if human_row and human_row.get('deterministic') is not None and any(r.get('deterministic') for r in server_rows if r['id']==human_id):
            # Deterministic mode: the playable client joined by keyframe and simulated the world locally.
            if not human_row.get('deterministic') or human_row.get('keyframes',0)<1 or human_row.get('det_advanced',0)<120:
                errors.append('Deterministic client did not join and simulate the relayed world')
        # Deterministic command stream per network profile, measured at the server.
        stream={}
        for index,profile in enumerate(PROFILES):
            members=[r for r in server_rows if r['id']<args.bots and cohort(r['id'],args.bots)==index]
            if members:
                cohort_rows=members
                rates=sorted(r.get('command_bytes',0)/max(args.duration,1)/1000 for r in cohort_rows)
                stream[profile['name']]=dict(command_kBps_p50=round(rates[len(rates)//2],2),command_kBps_max=round(rates[-1],2),
                    rtt_ms_p50=round(sorted(r.get('srtt_ms',0) for r in cohort_rows)[len(cohort_rows)//2],1),
                    resends=sum(r.get('command_resends',0) for r in cohort_rows),keyframes=sum(r.get('keyframes',0) for r in cohort_rows),
                    behind_s_p50=round(sorted((r.get('server_tick',0)-r.get('server_ack',0))/60 for r in cohort_rows)[len(cohort_rows)//2],2),
                    behind_s_max=round(max((r.get('server_tick',0)-r.get('server_ack',0))/60 for r in cohort_rows),2),
                    link_kBps=profile['rate']/1000)
        print('LAB_STREAM '+json.dumps(stream),flush=True)
        if any(r.get('bot_loaded') is not None for r in rows):
            storms=[r['id'] for r in rows if r['id']<args.bots and r.get('keyframes',0)>3]
            if storms:
                errors.append('Keyframe storm: bots rejoined more than 3 times: '+str(storms[:10]))
        simulation=server_report.get('simulation',{})
        if simulation.get('backend')!='EGPBox3DWorld' or simulation.get('joints',0)<100 or not simulation.get('cloth_finite') or simulation.get('pin_error',1)>0.001 or simulation.get('cloth_deformation',0)<0.1 or simulation.get('joint_travel',0)<1:
            errors.append('Native cloth/joint qualification failed')
        heights=simulation.get('float_heights',[])
        # Finite and bounded; a float may legitimately be knocked off the arena edge.
        if len(heights)!=6 or not all(isinstance(h,(int,float)) and -60<h<100 for h in heights):
            errors.append('Bounded interactive buoyancy state failed')
        if args.duration>=65:
            activities={key for row in server_rows for key in row.get('activities',{})}
            if not {'8','16','32','64','256'}.issubset(activities):
                errors.append('Replicated bot activities not exercised')
        summary = dict(passed=not errors,errors=errors,scope=('Local' if args.local_server else 'Remote')+' native dedicated Box3D server; local independent native Superpos UDP/DTLS streams through seeded datagram impairment',bots=args.bots,human=not args.no_human,duration=args.duration,server=server_meta,local_engine_sha256=hashlib.sha256(engine.read_bytes()).hexdigest(),clients=rows,server_clients=server_rows,proxy=proxy.rows,profiles=PROFILES,physics_p95_ms=server_report.get('physics_p95_ms'),server_exit=server_exit,simulation=simulation,performance={report['role']:report.get('performance',{}) for report in reports if report['role']=='human'},server_performance=server_report.get('performance',{}),local_server=args.local_server,proxy_gaps=proxy_gaps,diagnostics={report['role']:report.get('diagnostics',[]) for report in reports if report['role']=='human'}|{'server':server_report.get('diagnostics',[])},command_stream=stream,command_totals=server_report.get('command_totals',{}),live={report['role']:report.get('live',{}) for report in reports if report['role']=='human'}|{'server':{k:v for k,v in server_report.get('live',{}).items() if k!='streams'}})
        atomic_json(OUT/'receipt.json',summary)
        print('LAB_'+('PASS' if not errors else 'FAIL')+' clients='+str(count)+' physics_p95_ms='+str(summary['physics_p95_ms']),flush=True)
        if errors:
            print('\n'.join(errors),flush=True)
            return 1
        return 0
    finally:
        proxy.close()
        if remote.stdin and not remote.stdin.closed:
            remote.stdin.close()
        for process in processes:
            if process.poll() is None:
                process.kill()
        # Killing the local supervisor would orphan its native server child.
        if args.local_server and server_meta.get('pid'):
            subprocess.run(['taskkill','/F','/PID',str(server_meta['pid'])],capture_output=True)
        for stream in streams:
            stream.close()

if __name__=='__main__':
    try:
        sys.exit(main())
    except Exception as error:
        import traceback
        traceback.print_exc()
        print('LAB_ERROR '+str(error),file=sys.stderr)
        sys.exit(1)
