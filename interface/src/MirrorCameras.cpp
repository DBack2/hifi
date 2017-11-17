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
#include "UUID.h"

MirrorCameras::~MirrorCameras() {
    QWriteLocker writeLocker{ &_camerasLock };
    for (auto& camera : _cameras) {
        delete camera;
    }
    _cameras.clear();
    writeLocker.unlock();
}

void MirrorCameras::addCamera(const QUuid& entityID) {
    QWriteLocker writeLocker { &_camerasLock };
    
    int renderJob = getAvailableRenderJob();
    if (renderJob == -1) {
        return;
    }

    if (!_cameras.contains(entityID)) {
        _cameras[entityID] = new MirrorCamera(entityID, renderJob);
    }

    writeLocker.unlock();
}

void MirrorCameras::removeCamera(const QUuid& entityID) {
    QWriteLocker writeLocker{ &_camerasLock };
    auto iter = _cameras.find(entityID);
    if (iter != _cameras.end()) {
        MirrorCamera* camera = (*iter);
        _cameras.erase(iter);
        delete camera;
        //MirrorCamera* camera = (*iter);
        //camera->markForDelete();
    }
    writeLocker.unlock();
}

void MirrorCameras::deleteCameras() {
    QWriteLocker writeLocker{ &_camerasLock };
    for (auto& iter = _cameras.begin(); iter != _cameras.end();) {
        MirrorCamera* camera = (*iter);
        if (camera->markedForDelete()) {
            iter = _cameras.erase(iter);
            delete camera;
        } else {
            ++iter;
        }
    }
    writeLocker.unlock();
}

int MirrorCameras::getAvailableRenderJob() {
    for (int i = 0; i < _cameraRenderJobs.size(); ++i) {
        if (!_cameraRenderJobs[i]) {
            _cameraRenderJobs[i] = true;
            return i;
        }
    }
    return -1;
}