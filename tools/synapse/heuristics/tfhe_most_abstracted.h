#pragma once

#include "heuristic.h"
#include "score.h"

namespace synapse {

struct TfheMostAbstractedComparator : public HeuristicConfiguration {
  Score get_score(const ExecutionPlan &ep) const override {
    Score score(ep, {
//                        {Score::Category::NumberOfNodes, Score::MIN},
                        {Score::Category::CostOfBivariatePBSs, Score::MAX},
                        {Score::Category::CostOfUnivariatePBSs, Score::MIN},
                        // Uncomment, if needed
                        // {Score::Category::ProcessedBDDPercentage, Score::MAX},
                    });
    return score;
  }

  bool terminate_on_first_solution() const override { return false; }
};

using tfheMostAbstracted = Heuristic<TfheMostAbstractedComparator>;
} // namespace synapse