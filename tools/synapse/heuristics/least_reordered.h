#pragma once

#include "heuristic.h"
#include "score.h"

namespace synapse {

struct LeastReorderedComparator : public HeuristicConfiguration {
  Score get_score(const ExecutionPlan &ep) const override {
    Score score(ep, {
                        {Score::Category::NumberOfReorderedNodes, Score::MIN},
                        {Score::Category::NumberOfNodes, Score::MAX},
                    });
    return score;
  }

  bool terminate_on_first_solution() const override { return false; }
};

using LeastReordered = Heuristic<LeastReorderedComparator>;
} // namespace synapse
