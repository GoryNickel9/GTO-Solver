import fs from "node:fs";

const args = new Map();
for (let index = 2; index < process.argv.length; index += 2) {
  const key = process.argv[index];
  const value = process.argv[index + 1];
  if (!key?.startsWith("--") || value === undefined) {
    throw new Error("usage: node validate_hu_preflop_r0.mjs --game FILE --reference FILE");
  }
  args.set(key, value);
}

const gamePath = args.get("--game");
const referencePath = args.get("--reference");
if (!gamePath || !referencePath) {
  throw new Error("usage: node validate_hu_preflop_r0.mjs --game FILE --reference FILE");
}

const readJson = (path) => JSON.parse(fs.readFileSync(path, "utf8"));
const assert = (condition, message) => {
  if (!condition) throw new Error(message);
};
const equalArray = (actual, expected, name) => {
  assert(JSON.stringify(actual) === JSON.stringify(expected), `${name} mismatch`);
};

const game = readJson(gamePath);
assert(game.schema === "gtosd.hu_preflop_game.v1", "game schema mismatch");
assert(game.id === "HU-PREFLOP-CO40-GAME-001", "game id mismatch");
assert(game.monetary_contract_revision === 2, "monetary contract revision mismatch");
assert(game.ante_accounting === "dead_initial_pot_contribution", "ante accounting mismatch");
assert(game.preflop_target_basis === "live_commitment_excluding_dead_ante", "preflop target basis mismatch");
assert(game.effective_stack_units === 400000, "effective stack must be 40 ante");
assert(game.ante_units === 10000, "ante unit mismatch");
equalArray(game.open_target_units, [60000, 100000], "open targets");
equalArray(game.response_target_units, [105000, 145000], "response targets");
assert(game.allow_configured_incomplete_raise === true, "incomplete raise contract mismatch");
equalArray(game.postflop_sizes_basis_points, [3300, 6600, 12000], "postflop sizes");
assert(game.postflop_minimum_bet_units === 10000, "postflop minimum bet mismatch");
assert(game.include_all_in === true, "all-in action must be enabled");
assert(game.raise_termination === "natural_stack", "raise termination mismatch");
assert(game.rake_mode === "disabled", "rake mode must be disabled");

const reference = readJson(referencePath);
assert(reference.schema === "gtosd.hu_preflop_reference.v1", "reference schema mismatch");
assert(reference.id === "GTP-HU-PREFLOP-CO40-001", "reference id mismatch");
assert(reference.game.variant === "short_deck_hu_36", "variant mismatch");
assert(reference.game.effective_stack_ante === 40, "reference stack mismatch");
assert(reference.game.root_player === "CO", "root player mismatch");
assert(reference.game.monetary_contract_revision === 2, "reference monetary revision mismatch");
assert(reference.game.ante_accounting === "dead_initial_pot_contribution", "reference ante accounting mismatch");
assert(reference.game.preflop_target_basis === "live_commitment_excluding_dead_ante", "reference target basis mismatch");
assert(reference.game.root_pot_ante === 3, "root pot mismatch");
assert(reference.game.root_amount_to_call_ante === 1, "root call mismatch");
assert(reference.game.forced_contributions_ante.CO === 1, "CO contribution mismatch");
assert(reference.game.forced_contributions_ante.BTN === 2, "BTN contribution mismatch");
assert(reference.game.forced_contribution_components_ante.CO.ante === 1, "CO ante mismatch");
assert(reference.game.forced_contribution_components_ante.CO.button_blind === 0, "CO button blind mismatch");
assert(reference.game.forced_contribution_components_ante.CO.total === 1, "CO total mismatch");
assert(reference.game.forced_contribution_components_ante.BTN.ante === 1, "BTN ante mismatch");
assert(reference.game.forced_contribution_components_ante.BTN.button_blind === 1, "BTN button blind mismatch");
assert(reference.game.forced_contribution_components_ante.BTN.total === 2, "BTN total mismatch");
const rootCall = reference.game.root_actions.find((action) => action.id === "call");
assert(rootCall?.incremental_amount_ante === 1, "CO incremental call mismatch");
assert(rootCall?.target_live_commitment_ante === 1, "CO post-call live commitment mismatch");
assert(rootCall?.target_total_contribution_ante === 2, "CO post-call total contribution mismatch");
const expectedRootActions = {
  all_in: [39, 40],
  raise_6: [6, 7],
  raise_10: [10, 11],
  call: [1, 2],
  fold: [0, 1],
};
for (const [id, [live, total]] of Object.entries(expectedRootActions)) {
  const action = reference.game.root_actions.find((entry) => entry.id === id);
  assert(action?.target_live_commitment_ante === live, `${id} live commitment mismatch`);
  assert(action?.target_total_contribution_ante === total, `${id} total contribution mismatch`);
  assert(total === 1 + live, `${id} must count the dead CO ante exactly once`);
}
assert(reference.game.external_preflop_tree_status === "user_confirmed_match_2026-09-10", "external preflop tree status mismatch");
assert(reference.game.external_postflop_tree_status === "awaiting_information_2026-09-10", "external postflop tree status mismatch");
assert(
  reference.game.external_postflop_all_in_status === "user_confirmed_always_available_2026-09-10",
  "external postflop all-in status mismatch",
);
assert(reference.game.current_local_tree_fingerprint === "fnv1a64:a68337fa567aa2d9", "current local tree fingerprint mismatch");
assert(reference.game.rake.enabled === false, "reference rake must be disabled");
assert(reference.reference.root_ev_ante === -0.3, "reference EV mismatch");
assert(reference.reference.frequency_unit === "percent", "frequency unit mismatch");
assert(
  reference.reference.rounding === "integer_percent_source; class totals of 100 or 101 are preserved",
  "reference rounding contract mismatch",
);
assert(reference.reference.comparison_normalization === "divide each class row by its reported total; never rewrite the source values", "comparison normalization mismatch");

console.log(JSON.stringify({
  status: "PASS",
  game_id: game.id,
  reference_id: reference.id,
  effective_stack_ante: reference.game.effective_stack_ante,
  root_player: reference.game.root_player,
  forced_contributions: reference.game.forced_contribution_components_ante,
  root_incremental_call_ante: rootCall.incremental_amount_ante,
  external_preflop_tree_status: reference.game.external_preflop_tree_status,
  external_postflop_tree_status: reference.game.external_postflop_tree_status,
  external_postflop_all_in_status: reference.game.external_postflop_all_in_status,
  rake: "disabled",
  root_ev_ante: reference.reference.root_ev_ante,
  postflop_sizes_basis_points: game.postflop_sizes_basis_points,
  comparison_normalization: reference.reference.comparison_normalization,
}, null, 2));
