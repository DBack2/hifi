//
//  MirrorCamera.h
//  interface/src
//
//  Copyright 2017 High Fidelity, Inc.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#ifndef hifi_MirrorCamera_h
#define hifi_MirrorCamera_h

#include <shared/Camera.h>

#include <EntityTypes.h>
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
    int getRenderJobIndex() const { return _renderJobIndex;  }

private:
    QUuid _entityID;
    int _renderJobIndex{ -1 };
};

#endif // hifi_MirrorCamera_h
