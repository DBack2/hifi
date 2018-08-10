//
//  RegionState.cpp
//  libraries/workload/src/workload
//
//  Created by Andrew Meadows 2018.03.07
//  Copyright 2018 High Fidelity, Inc.
//
//  Originally from lighthouse3d. Modified to utilize glm::vec3 and clean up to our coding standards
//  Simple plane class.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#include "RegionState.h"

using namespace workload;

void RegionState::configure(const Config& config) {
}


void RegionState::run(const workload::WorkloadContextPointer& renderContext, const Inputs& inputs) {
    PROFILE_RANGE(workload, "RegionState::run");
    // inputs is a vector of vectors of proxyId's:
    //
    // inputs[0] = vector of ids exiting region 0
    // inputs[1] = vector of ids entering region 0
    // ...
    // inputs[2N] = vector of ids exiting region N
    // inputs[2N + 1] = vector of ids entering region N
    assert(inputs.size() == 2 * Region::UNKNOWN);

    // The id's in each vector are sorted in ascending order
    // because the source vectors are scanned in ascending order.
    {
        PROFILE_RANGE(workload, "RegionState::run - loop");
        for (uint32_t i = 0; i < _state.size(); ++i) {
            PROFILE_RANGE(workload, "RegionState::run - loop internal");
            const IndexVector& going = inputs[2 * i];
            const IndexVector& coming = inputs[2 * i + 1];
            if (coming.size() == 0 && going.size() == 0) {
                continue;
            }
            if (_state[i].empty()) {
                assert(going.empty());
                _state[i] = coming;
            }
            else {
                PROFILE_RANGE(workload, "RegionState::run - loop else");
                // NOTE: all vectors are sorted by proxyId!
                // which means we can build the new vector by walking three vectors (going, current, coming) in one pass
                IndexVector& oldState = _state[i];
                IndexVector newState;
                {
                    PROFILE_RANGE(workload, "RegionState::run - loop else - newState.reserve");
                    newState.reserve(oldState.size() - going.size() + coming.size());
                }
                uint32_t goingIndex = 0;
                uint32_t comingIndex = 0;
                {
                    PROFILE_RANGE(workload, "RegionState::run - loop else - sub loop");
                    for (uint32_t j = 0; j < oldState.size(); ++j) {
                        PROFILE_RANGE(workload, "RegionState::run - loop else - sub loop internal");
                        int32_t proxyId = oldState[j];
                        while (comingIndex < coming.size() && coming[comingIndex] < proxyId) {
                            newState.push_back(coming[comingIndex]);
                            ++comingIndex;
                        }
                        if (goingIndex < going.size() && going[goingIndex] == proxyId) {
                            ++goingIndex;
                        }
                        else {
                            newState.push_back(proxyId);
                        }
                    }
                }
                {
                    PROFILE_RANGE(workload, "RegionState::run - loop else - end loop/swap");
                    assert(goingIndex == going.size());
                    while (comingIndex < coming.size()) {
                        PROFILE_RANGE(workload, "RegionState::run - loop else - end loop internal");
                        newState.push_back(coming[comingIndex]);
                        ++comingIndex;
                    }
                    oldState.swap(newState);
                }
            }
        }
    }

    {
        PROFILE_RANGE(workload, "RegionState::run - config->setNum");
        auto config = std::static_pointer_cast<Config>(renderContext->jobConfig);
        config->setNum(0, (uint32_t)_state[0].size(), (uint32_t)_state[1].size(), (uint32_t)_state[2].size());
    }
}
