// Copyright (c) 2014-2015 Dropbox, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef PYSTON_ANALYSIS_FPC_H
#define PYSTON_ANALYSIS_FPC_H

#include <queue>

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"

#include "core/cfg.h"
#include "core/common.h"
#include "core/options.h"

namespace pyston {

template <typename T> class BBAnalyzer {
public:
    typedef llvm::DenseMap<InternedString, T> Map;
    typedef llvm::DenseMap<CFGBlock*, Map> AllMap;

    virtual ~BBAnalyzer() {}

    virtual T merge(T from, T into) const = 0;
    virtual T mergeBlank(T into) const = 0;
    virtual void processBB(Map& starting, CFGBlock* block) const = 0;
};

class CFGBlockMinIndex {
public:
    bool operator()(const CFGBlock* lhs, const CFGBlock* rhs) { return lhs->idx > rhs->idx; }
};

template <typename T>
typename BBAnalyzer<T>::AllMap computeFixedPoint(CFG* cfg, const BBAnalyzer<T>& analyzer, bool reverse) {
    assert(!reverse);

    typedef typename BBAnalyzer<T>::Map Map;
    typedef typename BBAnalyzer<T>::AllMap AllMap;
    AllMap starting_states;
    AllMap ending_states;

    llvm::SmallPtrSet<CFGBlock*, 32> in_queue;
    std::priority_queue<CFGBlock*, llvm::SmallVector<CFGBlock*, 32>, CFGBlockMinIndex> q;

    starting_states.insert(make_pair(cfg->getStartingBlock(), Map()));
    q.push(cfg->getStartingBlock());
    in_queue.insert(cfg->getStartingBlock());

    int num_evaluations = 0;
    while (!q.empty()) {
        num_evaluations++;
        CFGBlock* block = q.top();
        q.pop();
        in_queue.erase(block);

        Map& initial = starting_states[block];
        if (VERBOSITY("analysis") >= 2)
            printf("fpc on block %d - %d entries\n", block->idx, initial.size());

        Map ending = Map(initial);

        analyzer.processBB(ending, block);

        for (int i = 0; i < block->successors.size(); i++) {
            CFGBlock* next_block = block->successors[i];
            bool changed = false;
            bool initial = false;
            if (starting_states.count(next_block) == 0) {
                changed = true;
                initial = true;
            }

            Map& next = starting_states[next_block];
            for (const auto& p : ending) {
                if (next.count(p.first) == 0) {
                    changed = true;
                    if (initial) {
                        next[p.first] = p.second;
                    } else {
                        next[p.first] = analyzer.mergeBlank(p.second);
                    }
                } else {
                    T& next_elt = next[p.first];

                    T new_elt = analyzer.merge(p.second, next_elt);
                    if (next_elt != new_elt) {
                        next_elt = new_elt;
                        changed = true;
                    }
                }
            }

            for (const auto& p : next) {
                if (ending.count(p.first))
                    continue;

                T next_elt = analyzer.mergeBlank(p.second);
                if (next_elt != p.second) {
                    next[p.first] = next_elt;
                    changed = true;
                }
            }

            if (changed && in_queue.insert(next_block).second) {
                q.push(next_block);
            }
        }

        ending_states[block] = std::move(ending);
    }

    if (VERBOSITY("analysis")) {
        printf("%ld BBs, %d evaluations = %.1f evaluations/block\n", cfg->blocks.size(), num_evaluations,
               1.0 * num_evaluations / cfg->blocks.size());
    }


    return ending_states;
}
}

#endif
