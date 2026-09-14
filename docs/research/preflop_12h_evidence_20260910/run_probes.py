from pathlib import Path
import ctypes
from ctypes import wintypes
import json
import subprocess
import time
import statistics

out = Path(__file__).resolve().parent
root = out.parents[2]

class MemoryStatus(ctypes.Structure):
    _fields_ = [('length',wintypes.DWORD),('load',wintypes.DWORD)] + [(n,ctypes.c_ulonglong) for n in ['total_phys','avail_phys','total_page','avail_page','total_virtual','avail_virtual','avail_extended']]
class ProcessMemory(ctypes.Structure):
    _fields_=[('cb',wintypes.DWORD),('faults',wintypes.DWORD)]+[(n,ctypes.c_size_t) for n in ['peak_rss','rss','peak_paged','paged','peak_nonpaged','nonpaged','pagefile','peak_pagefile','private']]
kernel=ctypes.WinDLL('kernel32',use_last_error=True)
psapi=ctypes.WinDLL('psapi',use_last_error=True)
psapi.GetProcessMemoryInfo.argtypes=[wintypes.HANDLE,ctypes.POINTER(ProcessMemory),wintypes.DWORD]
memory=MemoryStatus(); memory.length=ctypes.sizeof(memory)
assert kernel.GlobalMemoryStatusEx(ctypes.byref(memory))
(out/'machine_memory.json').write_text(json.dumps({n:getattr(memory,n) for n,_ in memory._fields_},indent=2))

records=[]
plan=[]
for rep in range(3):
    # Reverse A/B order on alternate repetitions.
    for mode in ['perfect','exact']:
        for variant in (['baseline','cached'] if rep%2==0 else ['cached','baseline']):
            plan.append((variant,mode,20000,rep))
plan += [('profile','perfect',20000,0),('profile','exact',20000,0),('profile','exact',100000,0),('profile','memoryless',100000,0)]
for variant,mode,iterations,rep in plan:
    name=f'{variant}_{mode}_{iterations}_{rep}'
    command=[str(out/f'{variant}.exe'),'--config',str(root/'benchmarks/fixtures/hu_preflop_co40_game_v1.json'),'--output',str(out/f'{name}.json'),'--iterations',str(iterations),'--evaluation-deals','20000','--br-iterations','5000','--br-evaluation-deals','10000','--equity-samples','8','--seed','5207644666046803969']
    if mode=='exact': command+=['--postflop-exact']
    if mode=='memoryless': command+=['--postflop-memoryless-buckets']
    started=time.perf_counter()
    peak_rss=peak_private=0
    with (out/f'{name}.stdout.log').open('w') as stdout, (out/f'{name}.stderr.log').open('w') as stderr:
        process=subprocess.Popen(command,cwd=root,stdout=stdout,stderr=stderr)
        while process.poll() is None:
            mem=ProcessMemory(); mem.cb=ctypes.sizeof(mem)
            if psapi.GetProcessMemoryInfo(wintypes.HANDLE(int(process._handle)),ctypes.byref(mem),mem.cb):
                peak_rss=max(peak_rss,mem.peak_rss)
                peak_private=max(peak_private,mem.private)
            time.sleep(.05)
    assert process.returncode==0,(name,process.returncode)
    record={'name':name,'variant':variant,'mode':mode,'iterations':iterations,'repeat':rep,'wall_seconds':time.perf_counter()-started,'observed_peak_working_set_bytes':peak_rss,'observed_max_private_bytes':peak_private,'command':command}
    candidate=json.loads((out/f'{name}.json').read_text())
    record.update({k:candidate[k] for k in ['solve_seconds','information_sets','root_ev_ante','root_ev_standard_error_ante','normalized_nashconv','minimum_blueprint_payload_bytes']})
    records.append(record)
    (out/'runs.json').write_text(json.dumps(records,indent=2))
    print(json.dumps({k:v for k,v in record.items() if k!='command'}),flush=True)

for mode in ['perfect','exact']:
    reference=json.loads((out/f'baseline_{mode}_20000_0.json').read_text())
    for variant in ['baseline','cached','profile']:
        for item in out.glob(f'{variant}_{mode}_20000_*.json'):
            actual=json.loads(item.read_text())
            for key in ['strategy','root_ev_ante','root_ev_standard_error_ante','information_sets','normalized_nashconv']:
                assert reference[key]==actual[key],(item,key)
print('BASELINE_CACHED_PROFILE_DETERMINISTIC_OUTPUT_EQUAL=PASS',flush=True)
