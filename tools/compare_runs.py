import json

for f in ['out/th7d6s_v6.json', 'out/th7d6s_r2.json']:
    d = json.load(open(f))
    ev = d['gto_plus_ev_checks']['flop_co_root']
    print(f, 'root_ev_measured:', ev['measured_antes'], 'passed:', ev['passed'])
    unc = d.get('gto_plus_unconditional_ev_checks', {}).get('flop_co_root', {})
    if unc:
        print('  unconditional measured:', unc.get('measured_ev_antes'),
              'frequency:', unc.get('measured_frequency'))
    print('  dev_percent:', d['final_gto_plus_dev_percent'])
