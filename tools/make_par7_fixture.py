import json

with open('benchmarks/fixtures/gto_plus_th7d6s_101.json') as f:
    d = json.load(f)
for depth in [7]:
    d['benchmark_id'] = f'GTP-TH7D6S-{940 + depth:03d}'
    d['gtosd_run']['maximum_iterations'] = 5
    d['gtosd_run']['certification_interval'] = 1
    d['gtosd_run']['averaging_delay'] = 0
    d['gtosd_run']['parallel_action_depth'] = depth
    with open(f'benchmarks/fixtures/gto_plus_th7d6s_par{depth}.json', 'w') as f:
        json.dump(d, f, indent=2)
    print(f'par{depth} written')
