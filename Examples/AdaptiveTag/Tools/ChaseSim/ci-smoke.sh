#!/bin/sh
# smoke tests for chase_sim, run by CI (and by hand)
# usage: ci-smoke.sh path/to/chase_sim
set -u
SIM=${1:-Saved/ChaseSim/chase_sim}
OUT=$(mktemp)
fail=0
check() { # check "name" "pattern"
  if grep -qE "$2" "$OUT"; then echo "PASS  $1"; else echo "FAIL  $1"; fail=1; fi
}

# short session: snapshot, train a bit, then compare everything
cat > /tmp/ci.script <<'S'
pattern rightturn
snapshot
train 10
eval 20
quit
S

if ! "$SIM" --seed 1 --script /tmp/ci.script > "$OUT" 2>&1; then
  echo "FAIL  chase_sim exited with an error"; cat "$OUT"; exit 1
fi
echo "PASS  scripted session runs to the end"

check "snapshot is taken at v0"               "snapshot taken: policy v0"
check "learner publishes new policy versions" "CURRENT \(v[1-9][0-9]*\)"
check "snapshot stays frozen after training"  "SNAPSHOT \(v0\)"
check "eval reports all 3 baselines"          "GREEDY"
check "eval reports intercept baseline"       "INTERCEPT"
check "eval reports random baseline"          "RANDOM"

[ $fail -eq 0 ] || { echo "--- chase_sim output ---"; cat "$OUT"; }
exit $fail
