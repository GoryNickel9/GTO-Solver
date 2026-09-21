import fs from "node:fs";

if (process.argv.length !== 4) {
  throw new Error("usage: node analyze_preflop_bucket_components.mjs INPUT OUTPUT");
}

const input = JSON.parse(fs.readFileSync(process.argv[2], "utf8"));

function groupedValue(observations, actions, keyOf, forcePrefix) {
  const groups = new Map();
  let mass = 0;
  let physical = 0;
  for (const observation of observations) {
    const reach = forcePrefix ? 1 : observation.hero_reach;
    const weight = observation.board_multiplicity *
      observation.opponent_probability * reach;
    if (!(weight > 0)) continue;
    mass += weight;
    physical += weight * Math.max(...observation.action_ev);
    const key = keyOf(observation);
    let group = groups.get(key);
    if (group === undefined) {
      group = { sums: Array(actions).fill(0), bestActions: new Set() };
      groups.set(key, group);
    }
    for (let action = 0; action < actions; ++action) {
      group.sums[action] += weight * observation.action_ev[action];
    }
    group.bestActions.add(observation.action_ev.indexOf(Math.max(...observation.action_ev)));
  }
  let shared = 0;
  let conflictingGroups = 0;
  for (const group of groups.values()) {
    shared += Math.max(...group.sums);
    if (group.bestActions.size > 1) ++conflictingGroups;
  }
  return { mass, physical, shared, groups: groups.size, conflicting_groups: conflictingGroups };
}

function analyze(node, forcePrefix) {
  const actions = node.actions.length;
  const observations = node.observations;
  const physical = groupedValue(observations, actions,
    observation => `${observation.flop_index}:${observation.combo}`, forcePrefix);
  const feature = groupedValue(observations, actions,
    observation => `${observation.hand_class}:${observation.feature.join(",")}`, forcePrefix);
  const current = groupedValue(observations, actions,
    observation => `${observation.row}`, forcePrefix);
  const finer = groupedValue(observations, actions,
    observation => `${observation.hand_class}:${observation.comparison_bucket}`, forcePrefix);
  const conditional = value => value / physical.mass;
  const featureLoss = conditional(physical.physical - feature.shared);
  const currentLoss = conditional(physical.physical - current.shared);
  const finerLoss = conditional(physical.physical - finer.shared);
  return {
    mass: physical.mass,
    observations: observations.length,
    physical_best_value: conditional(physical.physical),
    feature_loss: featureLoss,
    current_200_loss: currentLoss,
    quantization_after_features: conditional(feature.shared - current.shared),
    finer_500_loss: finerLoss,
    loss_recovered_by_500: currentLoss - finerLoss,
    partitions: {
      exact_feature_groups: feature.groups,
      current_200_groups: current.groups,
      finer_500_groups: finer.groups,
      feature_conflicting_groups: feature.conflicting_groups,
      current_200_conflicting_groups: current.conflicting_groups,
      finer_500_conflicting_groups: finer.conflicting_groups
    }
  };
}

const output = {
  schema: "gtosd.preflop_bucket_component_audit.v1",
  source: process.argv[2],
  tree_fingerprint: input.tree_fingerprint,
  policy_fingerprint: input.policy_fingerprint,
  flop_feature_fingerprint: input.flop_feature_fingerprint,
  comparison_flop_table_fingerprint: input.comparison_flop_table_fingerprint,
  rows: input.rows,
  flop_indices: input.flop_indices,
  seed: input.seed,
  exact_future_runouts: input.exact_future_runouts,
  full_flop_coverage: input.full_flop_coverage,
  diagnostic_seconds: input.seconds,
  interpretation: "Frozen-continuation local decision losses. These are not NashConv and are not additive across nodes.",
  nodes: input.nodes.map(node => ({
    node: node.node,
    path: node.path,
    hero: node.hero,
    actions: node.actions,
    self_reach: analyze(node, false),
    forced_hero_prefix: analyze(node, true)
  }))
};

fs.writeFileSync(process.argv[3], `${JSON.stringify(output, null, 2)}\n`);
