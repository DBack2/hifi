//
//  MirrorCameras.cpp
//  interface/src
//
//  Copyright 2017 High Fidelity, Inc.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#include "MirrorCameras.h"
#include "avatar/AvatarManager.h"
#include <EntityScriptingInterface.h>

MirrorCameras::~MirrorCameras() {
    for (auto& camera : _cameras) {
        delete camera;
    }
    _cameras.clear();
}

void MirrorCameras::addCamera(const QUuid& entityID) {
    if (_cameras.contains(entityID)) {
        return;
    }
    int renderJob = getAvailableRenderJob();
    if (renderJob == -1) {
        renderJob = claimFurthestMirrorRenderJob();
    }
    _cameras[entityID] = new MirrorCamera(entityID, renderJob);
}

void MirrorCameras::removeCamera(const QUuid& entityID) {
    auto iter = _cameras.find(entityID);
    if (iter != _cameras.end()) {
        MirrorCamera* camera = (*iter);
        _availableRenderJobs[camera->getRenderJobIndex()] = false;
        _cameras.erase(iter);
        delete camera;
    }
}

int MirrorCameras::getAvailableRenderJob() {
    for (int i = 0; i < _availableRenderJobs.size(); ++i) {
        if (!_availableRenderJobs[i]) {
            _availableRenderJobs[i] = true;
            return i;
        }
    }
    return -1;
}

int MirrorCameras::claimFurthestMirrorRenderJob() {
    glm::vec3 avatarPosition = DependencyManager::get<AvatarManager>()->getMyAvatar()->getPosition();

    float maxDistance = 0.0f;
    QUuid maxDistanceEntityID;
    for (auto& camera : _cameras) {
        const QUuid& entityID = camera->getEntityID();
        EntityPropertyFlags entityPropFlags;
        EntityItemProperties entityProperties = DependencyManager::get<EntityScriptingInterface>()->getEntityProperties(entityID, entityPropFlags);
        glm::vec3 mirrorPosition = entityProperties.getPosition();
        float distance = glm::distance(mirrorPosition, avatarPosition);
        if (distance > maxDistance) {
            maxDistance = distance;
            maxDistanceEntityID = entityID;
        }
    }

    int renderJobIndex = _cameras[maxDistanceEntityID]->getRenderJobIndex();
    removeCamera(maxDistanceEntityID);
    _availableRenderJobs[renderJobIndex] = true;

    return renderJobIndex;
}