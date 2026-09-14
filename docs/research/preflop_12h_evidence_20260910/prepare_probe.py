from pathlib import Path
import hashlib
import json
import re

root = Path(__file__).resolve().parents[3]
out = Path(__file__).resolve().parent
source = root / 'libs/preflop/src/hu_preflop_solver.cpp'
assert hashlib.sha256(source.read_bytes()).hexdigest() == '9841dc63060412d6cb7be4097b983309667188bb58d87ce178e30c1e17c204e0', 'Source changed: re-audit before reproducing'
original = source.read_text(encoding='utf-8')
prefix = r'''
#include <iostream>
#include <chrono>
#include <array>
#include <cstdint>
namespace probe {
struct Stat { std::uint64_t calls=0, ns=0; };
inline std::array<Stat, 8> stats{};
inline std::uint64_t creates=0, touches=0, misses=0, queries=0;
struct Timer {
  int id; std::chrono::steady_clock::time_point start;
  explicit Timer(int i):id(i),start(std::chrono::steady_clock::now()) { ++stats[i].calls; }
  ~Timer() {stats[id].ns += static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-start).count());}
};
template<class F> auto time(int i, F f) { Timer t(i); return f(); }
void dump(const char* phase) {
  const char* names[]={"touch","key_inclusive","bucket_nested","terminal","legal_actions","apply_action","sample_deal","canonical_nested"};
  std::cerr << "PROBE " << phase << " creates=" << creates << " touches=" << touches << " policy_queries=" << queries << " policy_misses=" << misses << '\n';
  for(int i=0;i<8;++i) std::cerr << "TIMER " << phase << ' ' << names[i] << " calls=" << stats[i].calls << " seconds=" << (stats[i].ns/1e9) << '\n';
}
}
'''

def replace_once(text, old, new):
    assert text.count(old) == 1, (old[:80], text.count(old))
    return text.replace(old, new)

profile = original
profile = replace_once(profile, '  std::uint64_t last_iteration{0};', '  std::uint64_t last_iteration{0};\n  std::uint64_t probe_visits{0};')
profile = replace_once(profile, '    auto [found, inserted] = states_.try_emplace(key);', '    probe::Timer timer(0);\n    ++probe::touches;\n    auto [found, inserted] = states_.try_emplace(key);\n    ++found->second.probe_visits;\n    probe::creates += inserted;')
profile = replace_once(profile, '    const auto found = states_.find(key);', '    ++probe::queries;\n    const auto found = states_.find(key);\n    probe::misses += found == states_.end();')
profile = replace_once(profile, 'Deal sample_deal(std::mt19937_64 &random) {', 'Deal sample_deal(std::mt19937_64 &random) {\n  probe::Timer timer(6);')
profile = replace_once(profile, 'canonical_visible_observation(const Deal &deal, const std::uint8_t player, const Street street) {', 'canonical_visible_observation(const Deal &deal, const std::uint8_t player, const Street street) {\n  probe::Timer timer(7);')
profile = replace_once(profile, '                                                     const std::uint64_t bucket_seed) {', '                                                     const std::uint64_t bucket_seed) {\n  probe::Timer timer(2);')
profile = replace_once(profile, '                                                      const std::uint64_t history) {', '                                                      const std::uint64_t history) {\n    probe::Timer timer(1);')
profile = replace_once(profile, '  double terminal_payoff(const PublicState &state, const Deal &deal, const std::uint8_t player) {', '  double terminal_payoff(const PublicState &state, const Deal &deal, const std::uint8_t player) {\n    probe::Timer timer(3);')
profile = profile.replace('const auto actions = legal_actions(state, postflop_config_);', 'const auto actions = probe::time(4, [&] { return legal_actions(state, postflop_config_); });')
profile = re.sub(r'const auto next = apply_action\(state, actions.value\(\)\[(selected|action)\], postflop_config_\);', r'const auto next = probe::time(5, [&] { return apply_action(state, actions.value()[\1], postflop_config_); });', profile)
dump = r'''
    probe::dump("training");
    std::array<std::array<std::uint64_t,5>,4> histogram{};
    for(const auto& [key,state]:blueprint_.states()) {
      auto& h=histogram[static_cast<std::size_t>(key.street)];
      ++h[0]; h[1]+=state.probe_visits; h[2]+=state.probe_visits==1; h[3]+=state.probe_visits<=2; h[4]+=state.probe_visits>=10;
    }
    for(int i=0;i<4;++i) std::cerr << "VISITS street=" << i << " states=" << histogram[i][0] << " touches=" << histogram[i][1] << " singleton=" << histogram[i][2] << " le2=" << histogram[i][3] << " ge10=" << histogram[i][4] << '\n';
'''
profile = replace_once(profile, '    const auto baseline = evaluate(blueprint_, blueprint_, options_.evaluation_deals,', dump + '\n    const auto baseline = evaluate(blueprint_, blueprint_, options_.evaluation_deals,')
profile = replace_once(profile, '    DcfrTable response_co(options_.best_response_iterations);', '    probe::dump("after_profile_eval");\n    DcfrTable response_co(options_.best_response_iterations);')
profile = replace_once(profile, '    HuPreflopSolveResult result;', '    probe::dump("all");\n    HuPreflopSolveResult result;')
(out / 'profile_solver.cpp').write_text(prefix + profile, encoding='utf-8')
(out / 'baseline_solver.cpp').write_text(original, encoding='utf-8')

# This isolated variant only memoizes the winner of the already sampled complete deal.
# The game, policies, RNG draws, regrets, averaging, and action tree are unchanged.
cached = original
cached = replace_once(cached, 'struct Deal {', 'struct Deal {\n  mutable std::uint8_t cached_winner{0U};')
cached = replace_once(cached, '    if (state.status != HandStatus::Folded) {', '    if (state.status != HandStatus::Folded && deal.cached_winner == 0U) {')
cached = replace_once(cached, '      winner_mask = showdown.value().winner_mask;', '      deal.cached_winner = showdown.value().winner_mask;')
cached = replace_once(cached, '    const auto settlement = settle_terminal(state, tree_.config.rake, winner_mask);', '    winner_mask = state.status == HandStatus::Folded ? 0U : deal.cached_winner;\n    const auto settlement = settle_terminal(state, tree_.config.rake, winner_mask);')
(out / 'cached_solver.cpp').write_text(cached, encoding='utf-8')

build = root / 'out/build/codex-release-20260907'
libs = ' '.join(f'"{build}/libs/{n}/gtosd_{n}.lib"' for n in ['preflop','tree','equity','core'])
common = f'/nologo /std:c++20 /EHsc /O2 /Ob2 /DNDEBUG /MD /W4 /permissive- /I"{root}/include" /I"{build}/generated" /external:I"{build}/vcpkg_installed/x64-windows/include" /external:W0'
commands = ['@echo off', 'call "C:\\Program Files\\Microsoft Visual Studio\\18\\Community\\Common7\\Tools\\VsDevCmd.bat" -arch=x64 >nul', f'cd /d "{out}"']
for variant in ['baseline','profile','cached']:
    commands += [f'cl {common} {variant}_solver.cpp "{root}/benchmarks/hu_preflop_solve.cpp" /Fe:{variant}.exe /link {libs}', 'if errorlevel 1 exit /b 1']
(out / 'build.cmd').write_text('\n'.join(commands)+'\n', encoding='utf-8')
(out / 'source_manifest.json').write_text(json.dumps({'source':str(source),'sha256':hashlib.sha256(source.read_bytes()).hexdigest(),'build_libraries':str(build),'profile_extra_state_bytes':8,'variants':['baseline','profile','cached']},indent=2),encoding='utf-8')
print(out)
