#include "gtosd/preflop/hu_preflop.hpp"
#include <algorithm>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <stdexcept>

int main() {
  using namespace gtosd;
  using Clock = std::chrono::steady_clock;
  const auto seconds = [](auto start) { return std::chrono::duration<double>(Clock::now()-start).count(); };
  std::cout << std::setprecision(12);
  const auto started = Clock::now();
  const auto tree = build_hu_preflop_tree(make_hu_co40_benchmark_config()).value();
  const auto blueprint = make_uniform_hu_preflop_blueprint(tree).value();
  const auto plan = derive_hu_preflop_decomposition_plan(tree,blueprint).value();
  const auto work = estimate_hu_preflop_river_work(tree,plan).value();
  const auto catalog = build_hu_preflop_river_root_catalog(tree,plan,work).value();
  const auto batches = derive_hu_preflop_river_batch_plan(tree,plan,work,catalog,64ULL*1024*1024).value();
  const auto& task = catalog.task_spans.front();
  const auto begin = catalog.river_shapes.begin()+static_cast<std::ptrdiff_t>(task.first_shape_index);
  const auto end = begin+static_cast<std::ptrdiff_t>(task.shape_count);
  const auto shape=std::ranges::min_element(begin,end,{},[](const auto& item){return item.state.remaining_stacks[0].units();});
  const auto ordinal=task.first_resolver_root+static_cast<std::uint64_t>(std::distance(begin,shape))*task.board_count*2;
  const auto root=hu_preflop_river_resolver_root_at(catalog,batches,ordinal).value();
  HuPreflopSampledPostflopPolicy policy;
  policy.tree_fingerprint=tree.fingerprint;
  policy.algorithm="research_uniform_policy";
  policy.abstraction_id="preflop_exact81_postflop_physical_lossless_suit_isomorphism_v2";
  policy.iterations=7;
  policy.seed=0x554E49464F524D01ULL;
  policy.representation=HuPreflopPostflopRepresentation::ExactPhysical;
  policy.fingerprint=fingerprint_hu_preflop_sampled_postflop_policy(policy);
  std::cout<<"SETUP seconds="<<seconds(started)<<" root_ordinal="<<ordinal
           <<" stack_units="<<root.state.remaining_stacks[0].units()
           <<" public_history_actions="<<root.action_history.size()<<std::endl;
  for(const auto mode:{PostflopRootValueMode::AverageStrategy,PostflopRootValueMode::ExactBestResponse}) {
    const auto phase=Clock::now();
    const auto evaluated=evaluate_hu_preflop_sampled_policy_river_root(tree,plan,root,policy,mode);
    if(!evaluated) {std::cerr<<hu_preflop_error_name(evaluated.error())<<std::endl;return 1;}
    const auto& value=evaluated.value();
    std::cout<<"RIVER_EVAL mode="<<(mode==PostflopRootValueMode::AverageStrategy?"profile":"best_response")
             <<" seconds="<<seconds(phase)<<" p0="<<value.recomposed_value_antes[0]
             <<" p1="<<value.recomposed_value_antes[1]<<" rows0="<<value.players[0].size()
             <<" rows1="<<value.players[1].size()<<" recomposition_error="<<value.maximum_recomposition_error_antes<<std::endl;
  }
}
