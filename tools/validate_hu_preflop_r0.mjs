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
assert(reference.game.root_pot_ante === 3, "root pot mismatch");
assert(reference.game.root_amount_to_call_ante === 1, "root call mismatch");
assert(reference.game.forced_contributions_ante.CO === 1, "CO contribution mismatch");
assert(reference.game.forced_contributions_ante.BTN === 2, "BTN contribution mismatch");
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
  rake: "disabled",
  root_ev_ante: reference.reference.root_ev_ante,
  postflop_sizes_basis_points: game.postflop_sizes_basis_points,
  comparison_normalization: reference.reference.comparison_normalization,
}, null, 2));
