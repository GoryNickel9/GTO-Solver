import json

for f in ['out/th7d6s_v6.json', 'out/th7d6s_r2.json']:
    d = json.load(open(f))
    print('===', f)
    unc = d.get('gto_plus_unconditional_ev_checks', {}).get('flop_co_root', {})
    if unc:
        print('  measured_ev:', unc.get('measured_ev_antes'))
        for c in unc.get('components', []):
            print('   ', c.get('id'), 'freq:', c.get('measured_frequency'),
                  'ev:', c.get('measured_ev_antes'))
    # root action frequencies
    afc = d.get('gto_plus_action_frequency_checks', {})
    for node_id, actions in afc.items():
        if 'root' in node_id or node_id == 'flop_co_root':
            print('  node', node_id, {k: round(v.get('measured_fraction', -1), 4) for k, v in actions.items()})
