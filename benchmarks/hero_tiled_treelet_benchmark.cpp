#include <benchmark/benchmark.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <limits>
#include <string_view>
#include <vector>

namespace {
constexpr std::size_t ranks = 36, cards = 36, leaves = 13, states = 4;
constexpr std::size_t max_actions = 5, nodes = 6;
constexpr std::array<std::size_t, states> arities{5, 2, 3, 4};
constexpr std::uint16_t invalid = std::numeric_limits<std::uint16_t>::max();
struct Shape { std::string_view name; std::size_t hero, opponent; };
constexpr std::array<Shape, 2> shapes{{{"tst_p0", 358, 301},
                                       {"tst_p1", 301, 358}}};
struct Meta {
  std::vector<std::uint16_t> rank, first, second, first_all, second_all, own;
};
struct Frame {
  std::vector<float> reach, total, base, by_card, prefix, card_prefix;
};
struct StateIn {
  std::size_t actions{}; std::vector<std::uint16_t> regret, average;
  float regret_scale{}, average_scale{};
};
struct StateOut {
  std::vector<float> regret, average; std::vector<std::uint16_t> regret_code, average_code;
  double max_regret{}, max_average{}; float regret_scale{}, average_scale{};
};

bool bits(const std::vector<float> &a, const std::vector<float> &b) {
  return a.size() == b.size() && std::memcmp(a.data(), b.data(), a.size() * 4) == 0;
}
bool bits(const std::vector<std::uint16_t> &a,
          const std::vector<std::uint16_t> &b) {
  return a.size() == b.size() && std::memcmp(a.data(), b.data(), a.size() * 2) == 0;
}

class Fixture {
public:
  explicit Fixture(Shape shape) : s_(shape), opp_(columns(shape.opponent)),
      hero_(columns(shape.hero)), parent_(shape.opponent), own_(shape.hero),
      baseline_root_(shape.hero), tiled_root_(shape.hero) {
    init_meta();
    for (std::size_t l = 0; l < leaves; ++l) {
      strategy_[l].resize(s_.opponent); baseline_[l] = frame(); tiled_[l] = frame();
      leaf_value_[l].resize(s_.hero);
      for (std::size_t i = 0; i < s_.opponent; ++i)
        strategy_[l][i] = float(((i * (43 + l * 6) + 19 + l * 71) % 977) + 1) / 977.0F;
    }
    for (auto &v : node_value_) v.resize(s_.hero);
    for (std::size_t st = 0; st < states; ++st) {
      input_[st] = state_in(arities[st], st);
      baseline_state_[st] = state_out(arities[st]);
      tiled_state_[st] = state_out(arities[st]);
    }
    validate<32>(); validate<64>();
  }

  void full() {
    prepare(baseline_); reset(baseline_state_);
    for (std::size_t l = 0; l < leaves; ++l) terminal(baseline_[l], 0, s_.hero, leaf_value_[l].data());
    sum({leaf_value_[1].data(), leaf_value_[2].data()}, 2, s_.hero, node_value_[1].data());
    update(1, {leaf_value_[3].data(), leaf_value_[4].data()}, 0, s_.hero, node_value_[2].data(), baseline_state_); finish(1, baseline_state_);
    update(2, {leaf_value_[5].data(), leaf_value_[6].data(), leaf_value_[7].data()}, 0, s_.hero, node_value_[3].data(), baseline_state_); finish(2, baseline_state_);
    update(3, {leaf_value_[9].data(), leaf_value_[10].data(), leaf_value_[11].data(), leaf_value_[12].data()}, 0, s_.hero, node_value_[5].data(), baseline_state_); finish(3, baseline_state_);
    sum({leaf_value_[8].data(), node_value_[5].data()}, 2, s_.hero, node_value_[4].data());
    update(0, {leaf_value_[0].data(), node_value_[1].data(), node_value_[2].data(), node_value_[3].data(), node_value_[4].data()}, 0, s_.hero, baseline_root_.data(), baseline_state_); finish(0, baseline_state_);
  }

  template <std::size_t Tile> void tiled() {
    prepare(tiled_); reset(tiled_state_);
    for (std::size_t begin = 0; begin < s_.hero; begin += Tile) {
      const auto count = std::min(Tile, s_.hero - begin);
      std::array<std::array<float, Tile>, leaves> leaf{};
      std::array<std::array<float, Tile>, nodes> node{};
      for (std::size_t l = 0; l < leaves; ++l) terminal(tiled_[l], begin, count, leaf[l].data());
      sum({leaf[1].data(), leaf[2].data()}, 2, count, node[1].data());
      update(1, {leaf[3].data(), leaf[4].data()}, begin, count, node[2].data(), tiled_state_);
      update(2, {leaf[5].data(), leaf[6].data(), leaf[7].data()}, begin, count, node[3].data(), tiled_state_);
      update(3, {leaf[9].data(), leaf[10].data(), leaf[11].data(), leaf[12].data()}, begin, count, node[5].data(), tiled_state_);
      sum({leaf[8].data(), node[5].data()}, 2, count, node[4].data());
      update(0, {leaf[0].data(), node[1].data(), node[2].data(), node[3].data(), node[4].data()}, begin, count, tiled_root_.data() + begin, tiled_state_);
    }
    for (std::size_t st = 0; st < states; ++st) finish(st, tiled_state_);
  }

  std::size_t arena(std::size_t tile) const {
    const auto frame_bytes = leaves * (s_.opponent + 75 * ranks + 37) * 4;
    const auto scratch_bytes = 2 * 14 * s_.hero * 4;
    return frame_bytes + scratch_bytes + (leaves + nodes) * tile * 4;
  }
  const Shape &shape() const { return s_; }
  const std::vector<float> &full_output() const { return baseline_root_; }
  const std::vector<float> &tile_output() const { return tiled_root_; }

private:
  static Meta columns(std::size_t n) { return {std::vector<std::uint16_t>(n), std::vector<std::uint16_t>(n), std::vector<std::uint16_t>(n), std::vector<std::uint16_t>(n), std::vector<std::uint16_t>(n), std::vector<std::uint16_t>(n)}; }
  Frame frame() const { return {std::vector<float>(s_.opponent), std::vector<float>(ranks), std::vector<float>(ranks), std::vector<float>(ranks * cards), std::vector<float>(ranks + 1), std::vector<float>((ranks + 1) * cards)}; }
  void init_meta() {
    for (std::size_t i = 0; i < s_.opponent; ++i) {
      auto r = (i * 37 + i / 7) % ranks, c0 = (i * 11 + r * 3) % cards;
      auto c1 = (i * 17 + r * 5 + 1) % cards; if (c0 == c1) c1 = (c1 + 1) % cards;
      opp_.rank[i] = std::uint16_t(r); opp_.first[i] = std::uint16_t(r * cards + c0); opp_.second[i] = std::uint16_t(r * cards + c1);
      parent_[i] = float(((i * 41 + 23) % 991) + 1) / 991.0F;
    }
    for (std::size_t i = 0; i < s_.hero; ++i) {
      auto r = (i * 43 + i / 5) % ranks, c0 = (i * 7 + r * 11) % cards;
      auto c1 = (i * 13 + r * 17 + 1) % cards; if (c0 == c1) c1 = (c1 + 1) % cards;
      hero_.rank[i] = std::uint16_t(r); hero_.first[i] = std::uint16_t(r * cards + c0); hero_.second[i] = std::uint16_t(r * cards + c1);
      hero_.first_all[i] = std::uint16_t(ranks * cards + c0); hero_.second_all[i] = std::uint16_t(ranks * cards + c1);
      hero_.own[i] = i % 13 == 12 ? invalid : std::uint16_t(i % s_.opponent);
      own_[i] = float(((i * 59 + 37) % 971) + 1) / 971.0F;
    }
  }
  StateIn state_in(std::size_t actions, std::size_t seed) const {
    StateIn in{actions, std::vector<std::uint16_t>(actions * s_.hero), std::vector<std::uint16_t>(actions * s_.hero), float(seed + 1) * .00075F, float(seed + 1) * .00003125F};
    for (std::size_t a = 0; a < actions; ++a) for (std::size_t i = 0; i < s_.hero; ++i) {
      auto x = a * s_.hero + i; auto code = std::int16_t(int((i * 97 + a * 331 + seed * 173) % 2001) - 1000);
      in.regret[x] = std::uint16_t(code); in.average[x] = std::uint16_t(((i * 83 + a * 271 + seed * 109) % 65534) + 1);
    } return in;
  }
  StateOut state_out(std::size_t actions) const { auto n = actions * s_.hero; return {std::vector<float>(n), std::vector<float>(n), std::vector<std::uint16_t>(n), std::vector<std::uint16_t>(n)}; }
  void prepare(std::array<Frame, leaves> &frames) {
    for (std::size_t l = 0; l < leaves; ++l) {
      auto &f = frames[l]; std::fill(f.total.begin(), f.total.end(), 0.0F); std::fill(f.by_card.begin(), f.by_card.end(), 0.0F); f.prefix[0] = 0.0F; std::fill_n(f.card_prefix.begin(), cards, 0.0F);
      for (std::size_t i = 0; i < s_.opponent; ++i) { auto w = parent_[i] * strategy_[l][i]; f.reach[i] = w; f.total[opp_.rank[i]] += w; f.by_card[opp_.first[i]] += w; f.by_card[opp_.second[i]] += w; }
      for (std::size_t r = 0; r < ranks; ++r) { f.prefix[r + 1] = f.prefix[r] + f.total[r]; for (std::size_t c = 0; c < cards; ++c) f.card_prefix[(r + 1) * cards + c] = f.card_prefix[r * cards + c] + f.by_card[r * cards + c]; }
      float win = 4.0F + float(l % 4) * 3.25F, tie = -.5F + float(l % 3) * .25F, loss = -5.0F - float(l % 4) * 3.25F, total = f.prefix[ranks];
      for (std::size_t r = 0; r < ranks; ++r) f.base[r] = f.prefix[r] * win + f.total[r] * tie + (total - f.prefix[r + 1]) * loss;
    }
  }
  void terminal(const Frame &f, std::size_t begin, std::size_t count, float *out) const {
    for (std::size_t j = 0; j < count; ++j) { const auto i = begin + j; const auto slot = hero_.own[i]; float own = slot == invalid ? 0 : f.reach[slot];
      float lower = f.card_prefix[hero_.first[i]] + f.card_prefix[hero_.second[i]], tie = f.by_card[hero_.first[i]] + f.by_card[hero_.second[i]] - own;
      float all = f.card_prefix[hero_.first_all[i]] + f.card_prefix[hero_.second_all[i]] - own;
      out[j] = (f.base[hero_.rank[i]] - lower * 7.0F - tie * 2.0F + all * 5.0F) * .03125F; }
  }
  static void sum(std::array<const float *, max_actions> action, std::size_t n, std::size_t count, float *out) {
    std::fill_n(out, count, 0.0F); for (std::size_t a = 0; a < n; ++a) for (std::size_t i = 0; i < count; ++i) out[i] = float(out[i] + action[a][i]);
  }
  static void reset(std::array<StateOut, states> &out) { for (auto &x : out) { x.max_regret = x.max_average = 0; x.regret_scale = x.average_scale = 0; } }
  void update(std::size_t st, std::array<const float *, max_actions> action,
              std::size_t begin, std::size_t count, float *value,
              std::array<StateOut, states> &output) const {
    const auto &in = input_[st]; auto &out = output[st]; std::array<float, max_actions> strategy{};
    for (std::size_t j = 0; j < count; ++j) { auto i = begin + j; std::uint32_t positive = 0;
      for (std::size_t a = 0; a < in.actions; ++a) { auto p = std::max(0, int(std::int16_t(in.regret[a * s_.hero + i]))); strategy[a] = float(p); positive += std::uint32_t(p); }
      if (positive == 0) std::fill_n(strategy.begin(), in.actions, 1.0F / float(in.actions)); else for (std::size_t a = 0; a < in.actions; ++a) strategy[a] *= 1.0F / float(positive);
      float current = 0; for (std::size_t a = 0; a < in.actions; ++a) current = float(current + strategy[a] * action[a][j]); value[j] = current;
      for (std::size_t a = 0; a < in.actions; ++a) { auto x = a * s_.hero + i; double old = double(std::int16_t(in.regret[x])) * in.regret_scale;
        double r = old * (old > 0 ? .97 : .91) + .875 * (double(action[a][j]) - current); double av = double(in.average[x]) * in.average_scale + .625 * own_[i] * strategy[a];
        out.regret[x] = float(r); out.average[x] = float(av); out.max_regret = std::max(out.max_regret, std::abs(r)); out.max_average = std::max(out.max_average, av); }
    }
  }
  void finish(std::size_t st, std::array<StateOut, states> &output) const {
    auto &out = output[st]; out.regret_scale = out.max_regret > 0 ? float(out.max_regret / 32767) : 0; out.average_scale = out.max_average > 0 ? float(out.max_average / 65535) : 0;
    double ri = out.regret_scale > 0 ? 1.0 / out.regret_scale : 0, ai = out.average_scale > 0 ? 1.0 / out.average_scale : 0;
    for (std::size_t i = 0; i < out.regret.size(); ++i) { auto r = std::int16_t(std::clamp(std::nearbyint(double(out.regret[i]) * ri), -32767.0, 32767.0)); out.regret_code[i] = std::uint16_t(r); out.average_code[i] = std::uint16_t(std::clamp(std::nearbyint(double(out.average[i]) * ai), 0.0, 65535.0)); }
  }
  static bool same(const Frame &a, const Frame &b) { return bits(a.reach,b.reach)&&bits(a.total,b.total)&&bits(a.base,b.base)&&bits(a.by_card,b.by_card)&&bits(a.prefix,b.prefix)&&bits(a.card_prefix,b.card_prefix); }
  static bool same(const StateOut &a, const StateOut &b) { return bits(a.regret,b.regret)&&bits(a.average,b.average)&&bits(a.regret_code,b.regret_code)&&bits(a.average_code,b.average_code)&&std::bit_cast<std::uint32_t>(a.regret_scale)==std::bit_cast<std::uint32_t>(b.regret_scale)&&std::bit_cast<std::uint32_t>(a.average_scale)==std::bit_cast<std::uint32_t>(b.average_scale); }
  template <std::size_t Tile> void validate() { full(); tiled<Tile>(); for (std::size_t l=0;l<leaves;++l) if(!same(baseline_[l],tiled_[l])) std::abort(); if(!bits(baseline_root_,tiled_root_)) std::abort(); for(std::size_t st=0;st<states;++st) if(!same(baseline_state_[st],tiled_state_[st])) std::abort(); }

  Shape s_; Meta opp_, hero_; std::vector<float> parent_, own_;
  std::array<std::vector<float>, leaves> strategy_, leaf_value_;
  std::array<Frame, leaves> baseline_, tiled_;
  std::array<StateIn, states> input_; std::array<StateOut, states> baseline_state_, tiled_state_;
  std::array<std::vector<float>, nodes> node_value_; std::vector<float> baseline_root_, tiled_root_;
};

Fixture &fixture(std::size_t i) { static Fixture p0(shapes[0]), p1(shapes[1]); return i == 0 ? p0 : p1; }
void metrics(benchmark::State &state, const Fixture &f, std::size_t tile) {
  state.SetItemsProcessed(state.iterations()); state.counters["hero_hands"] = double(f.shape().hero); state.counters["opponent_hands"] = double(f.shape().opponent);
  state.counters["depth"] = 3; state.counters["tile"] = double(tile); state.counters["arena_bytes_8_workers"] = double(f.arena(tile) * 8);
}
void BM_TreeletFull(benchmark::State &state, std::size_t workload) { auto &f=fixture(workload); for(auto _:state){(void)_;f.full();benchmark::DoNotOptimize(f.full_output().data());benchmark::ClobberMemory();}metrics(state,f,0); }
template<std::size_t Tile> void BM_TreeletTiled(benchmark::State &state, std::size_t workload) { auto &f=fixture(workload); for(auto _:state){(void)_;f.tiled<Tile>();benchmark::DoNotOptimize(f.tile_output().data());benchmark::ClobberMemory();}metrics(state,f,Tile); }
BENCHMARK_CAPTURE(BM_TreeletFull, tst_p0, 0U); BENCHMARK_CAPTURE(BM_TreeletTiled<32>, tst_p0, 0U); BENCHMARK_CAPTURE(BM_TreeletTiled<64>, tst_p0, 0U);
BENCHMARK_CAPTURE(BM_TreeletFull, tst_p1, 1U); BENCHMARK_CAPTURE(BM_TreeletTiled<32>, tst_p1, 1U); BENCHMARK_CAPTURE(BM_TreeletTiled<64>, tst_p1, 1U);
} // namespace
