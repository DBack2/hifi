//
//  MirrorCameras.h
//  interface/src
//
//  Created by David Back on 11/21/2017.
//  Copyright 2017 High Fidelity, Inc.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#ifndef hifi_MirrorCameras_h
#define hifi_MirrorCameras_h

#include <RenderDeferredTask.h>

class MirrorCameraJob;

class MirrorCameraJobConfig : public render::Task::Config { // Exposes secondary camera parameters to JavaScript.
    Q_OBJECT
public:
    MirrorCameraJobConfig() : render::Task::Config(false) {}

    void setEntityID(const QUuid& entityID);
    const QUuid& getEntityID() const { return _entityID; }

signals:
    void dirty();

private:
    QUuid _entityID;
};

class MirrorCameraRenderTaskConfig : public render::Task::Config {
    Q_OBJECT
public:
    MirrorCameraRenderTaskConfig() : render::Task::Config(false) {}
};

class MirrorCameraRenderTask {
public:
    using Config = MirrorCameraRenderTaskConfig;
    using JobModel = render::Task::Model<MirrorCameraRenderTask, Config>;
    MirrorCameraRenderTask() {}
    void configure(const Config& config) {}
    void build(JobModel& task, const render::Varying& inputs, render::Varying& outputs, render::CullFunctor cullFunctor, int jobIndex);
};

class MirrorCamera {
public:
    MirrorCamera(const QUuid& entityID, int renderJobIndex);
    ~MirrorCamera();

    const QUuid& getEntityID() const { return _entityID; }
    int getRenderJobIndex() const { return _renderJobIndex; }

private:
    QUuid _entityID;
    int _renderJobIndex{ -1 };
};

class MirrorCameras : public QObject {
    Q_OBJECT

public:
    MirrorCameras() {};
    ~MirrorCameras();

    void setRenderJobs(int numJobs) { _availableRenderJobs.resize(numJobs); }

public slots:
    void addCamera(const QUuid& entityID);
    void removeCamera(const QUuid& entityID);

signals:
    void cameraRemoved(const QUuid& entityID);

private:
    int getAvailableRenderJob();
    int claimFurthestMirrorRenderJob();

    QHash<QUuid, MirrorCamera*> _cameras;
    std::vector<bool> _availableRenderJobs;
};

#endif // hifi_MirrorCameras_h