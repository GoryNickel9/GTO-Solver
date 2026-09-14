"""Exact expectation of the local two-pass averaging rule on a tiny game.

P0 chooses Enter with probability x; P1 continues with probability y;
P0 then chooses A with probability a. The target information set remembers
both earlier actions. Both policies are frozen within each two-pass update.
No chance, no abstraction and no floating point arithmetic are involved.
"""
from fractions import Fraction as F
from pathlib import Path
import json

profiles = [(F(1,10), F(1,2), F(1)), (F(9,10), F(1,2), F(0))]
target = [F(0), F(0)]
local = [F(0), F(0)]
standard = [F(0), F(0)]
for x,y,a in profiles:
    sigma = [a,1-a]
    for action in range(2):
        target[action] += x*sigma[action]
        # P0 traversal enumerates P0 actions, samples P1's preceding action.
        local[action] += y*x*sigma[action]
        # P1 traversal samples P0's preceding action, enumerates P1 actions.
        local[action] += x*x*sigma[action]
        # Correct HU external-sampling opponent-pass averaging cancels x.
        standard[action] += x*sigma[action]

def norm(v): return [x/sum(v) for x in v]
assert norm(target)==[F(1,10),F(9,10)]
assert norm(local)==[F(1,22),F(21,22)]
assert norm(standard)==norm(target)
result={
    'correct_average':[str(v) for v in norm(target)],
    'local_two_pass_average_in_expectation':[str(v) for v in norm(local)],
    'standard_opponent_pass_average_in_expectation':[str(v) for v in norm(standard)],
    'absolute_action_A_error_pp':float(abs(norm(target)[0]-norm(local)[0])*100),
    'scope':'exact algebraic counterexample to averaging only; not a poker convergence benchmark',
}
weighted_target=[F(0),F(0)]
weighted_local=[F(0),F(0)]
for iteration,(x,y,a) in enumerate(profiles,start=1):
    weight=F((iteration+1)**3)
    for action,prob in enumerate([a,1-a]):
        weighted_target[action]+=weight*x*prob
        weighted_local[action]+=weight*x*(x+y)*prob
assert norm(weighted_target)[0]==F(8,251)
assert norm(weighted_local)[0]==F(8,575)
result['gamma3_correct_average']=[str(v) for v in norm(weighted_target)]
result['gamma3_local_average_in_expectation']=[str(v) for v in norm(weighted_local)]
result['gamma3_action_A_correct_percent']=float(100*norm(weighted_target)[0])
result['gamma3_action_A_local_percent']=float(100*norm(weighted_local)[0])
Path(__file__).with_suffix('.json').write_text(json.dumps(result,indent=2))
print(json.dumps(result,indent=2))
