import json

with open('benchmarks/fixtures/gto_plus_th7d6s_101.json') as f:
    d = json.load(f)
d['benchmark_id'] = 'GTP-TH7D6S-999'
d['gtosd_run']['maximum_iterations'] = 5
d['gtosd_run']['certification_interval'] = 1
d['gtosd_run']['averaging_delay'] = 0
with open('benchmarks/fixtures/gto_plus_th7d6s_smoke.json', 'w') as f:
    json.dump(d, f, indent=2)
print('smoke fixture written')
