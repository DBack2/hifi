//
//  MirrorCamera.cpp
//  interface/src
//
//  Copyright 2017 High Fidelity, Inc.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#include "MirrorCamera.h"

#include "Application.h"
#include "task\Task.h"
#include "LODManager.h"
#include <glm/gtx/transform.hpp>

using RenderArgsPointer = std::shared_ptr<RenderArgs>;

class MirrorCameraJob {  // Changes renderContext for our framebuffer and view.
public:
    using Config = MirrorCameraJobConfig;
    using JobModel = render::Job::ModelO<MirrorCameraJob, RenderArgsPointer, Config>;
    MirrorCameraJob() {
        _cachedArgsPointer = std::make_shared<RenderArgs>(_cachedArgs);
    }

    void configure(const Config& config) {
        _entityID = config.getEntityID();
    }

    void run(const render::RenderContextPointer& renderContext, RenderArgsPointer& cachedArgs) {
        if (_entityID.isNull()) {
            cachedArgs = _cachedArgsPointer;
            return;
        }

        auto args = renderContext->args;
        auto textureCache = DependencyManager::get<TextureCache>();
        gpu::FramebufferPointer destFramebuffer;

        EntityPropertyFlags entityPropFlags;
        EntityItemProperties entityProperties = DependencyManager::get<EntityScriptingInterface>()->getEntityProperties(_entityID, entityPropFlags);
        glm::vec3 mirrorPropertiesPosition = entityProperties.getPosition();
        glm::quat mirrorPropertiesRotation = entityProperties.getRotation();
        glm::vec3 mirrorPropertiesDimensions = entityProperties.getDimensions();
        glm::vec3 halfMirrorPropertiesDimensions = 0.5f * mirrorPropertiesDimensions;

        float textureWidth = TextureCache::DEFAULT_MIRROR_CAM_RESOLUTION * mirrorPropertiesDimensions.x;
        float textureHeight = TextureCache::DEFAULT_MIRROR_CAM_RESOLUTION * mirrorPropertiesDimensions.y;

        destFramebuffer = textureCache->getMirrorCameraFramebuffer(_entityID, textureWidth, textureHeight);
        if (destFramebuffer) {
            _cachedArgsPointer->_blitFramebuffer = args->_blitFramebuffer;
            _cachedArgsPointer->_viewport = args->_viewport;
            _cachedArgsPointer->_displayMode = args->_displayMode;
            _cachedArgsPointer->_renderMode = args->_renderMode;
            args->_blitFramebuffer = destFramebuffer;
            args->_viewport = glm::ivec4(0, 0, destFramebuffer->getWidth(), destFramebuffer->getHeight());
            args->_displayMode = RenderArgs::MONO;
            args->_renderMode = RenderArgs::RenderMode::SECONDARY_CAMERA_RENDER_MODE;

            gpu::doInBatch(args->_context, [&](gpu::Batch& batch) {
                batch.disableContextStereo();
                batch.disableContextViewCorrection();
            });
            auto srcViewFrustum = args->getViewFrustum();

            // setup mirror from world as inverse of world from mirror transformation using inverted x and z for mirrored image
            // TODO: we are assuming here that UP is world y-axis
            glm::mat4 worldFromMirrorRotation = glm::mat4_cast(mirrorPropertiesRotation) * glm::scale(vec3(-1.0f, 1.0f, -1.0f));
            glm::mat4 worldFromMirrorTranslation = glm::translate(mirrorPropertiesPosition);
            glm::mat4 worldFromMirror = worldFromMirrorTranslation * worldFromMirrorRotation;
            glm::mat4 mirrorFromWorld = glm::inverse(worldFromMirror);

            // get mirror camera position by reflecting main camera position's z coordinate in mirror space
            glm::vec3 mainCameraPositionWorld = qApp->getCamera().getPosition();
            glm::vec3 mainCameraPositionMirror = vec3(mirrorFromWorld * vec4(mainCameraPositionWorld, 1.0f));
            glm::vec3 mirrorCameraPositionMirror = vec3(mainCameraPositionMirror.x, mainCameraPositionMirror.y, -mainCameraPositionMirror.z);
            glm::vec3 mirrorCameraPositionWorld = vec3(worldFromMirror * vec4(mirrorCameraPositionMirror, 1.0f));

            // set frustum position to be mirrored camera and set orientation to mirror's adjusted rotation
            glm::quat mirrorCameraOrientation = glm::quat_cast(worldFromMirrorRotation);
            srcViewFrustum.setPosition(mirrorCameraPositionWorld);
            srcViewFrustum.setOrientation(mirrorCameraOrientation);

            // build frustum using mirror space translation of mirrored camera
            float nearClip = mirrorCameraPositionMirror.z + mirrorPropertiesDimensions.z * 2.0f;
            glm::vec3 upperRight = halfMirrorPropertiesDimensions - mirrorCameraPositionMirror;
            glm::vec3 bottomLeft = -halfMirrorPropertiesDimensions - mirrorCameraPositionMirror;
            glm::mat4 frustum = glm::frustum(bottomLeft.x, upperRight.x, bottomLeft.y, upperRight.y, nearClip, 16384.0f);
            srcViewFrustum.setProjection(frustum);

            srcViewFrustum.calculate();
            args->pushViewFrustum(srcViewFrustum);
            cachedArgs = _cachedArgsPointer;
        }
    }

protected:
    RenderArgs _cachedArgs;
    RenderArgsPointer _cachedArgsPointer;

private:
    QUuid _entityID;
};

class EndMirrorCameraFrame {  // Restores renderContext.
public:
    using JobModel = render::Job::ModelI<EndMirrorCameraFrame, RenderArgsPointer>;

    void run(const render::RenderContextPointer& renderContext, const RenderArgsPointer& cachedArgs) {
        auto args = renderContext->args;
        args->_blitFramebuffer = cachedArgs->_blitFramebuffer;
        args->_viewport = cachedArgs->_viewport;
        args->popViewFrustum();
        args->_displayMode = cachedArgs->_displayMode;
        args->_renderMode = cachedArgs->_renderMode;

        gpu::doInBatch(args->_context, [&](gpu::Batch& batch) {
            batch.restoreContextStereo();
            batch.restoreContextViewCorrection();
        });
    }
};

void MirrorCameraRenderTask::build(JobModel& task, const render::Varying& inputs, render::Varying& outputs, render::CullFunctor cullFunctor, int jobIndex) {
    const auto cachedArg = task.addJob<MirrorCameraJob>("MirrorCamera" + jobIndex);
    const auto items = task.addJob<RenderFetchCullSortTask>("FetchCullSort", cullFunctor);
    assert(items.canCast<RenderFetchCullSortTask::Output>());
    task.addJob<RenderDeferredTask>("RenderDeferredTask", items);
    task.addJob<EndMirrorCameraFrame>("EndMirrorCamera", cachedArg);
}

MirrorCamera::MirrorCamera(const QUuid& entityID, int renderJobIndex)
    :_entityID(entityID),
    _renderJobIndex(renderJobIndex) {
    MirrorCameraJobConfig* jobConfig = qobject_cast<MirrorCameraJobConfig*>(qApp->getRenderEngine()->getConfiguration()->getConfig("MirrorCamera" + _renderJobIndex));
    jobConfig->setEntityID(_entityID);
    jobConfig->setEnabled(true);
    qobject_cast<MirrorCameraRenderTaskConfig*>(qApp->getRenderEngine()->getConfiguration()->getConfig("MirrorCameraJob" + _renderJobIndex))->setEnabled(true);
}

MirrorCamera::~MirrorCamera() {
    MirrorCameraJobConfig* jobConfig = qobject_cast<MirrorCameraJobConfig*>(qApp->getRenderEngine()->getConfiguration()->getConfig("MirrorCamera" + _renderJobIndex));
    jobConfig->setEntityID(QUuid());
    jobConfig->setEnabled(false);
    qobject_cast<MirrorCameraRenderTaskConfig*>(qApp->getRenderEngine()->getConfiguration()->getConfig("MirrorCameraJob" + _renderJobIndex))->setEnabled(false);
}

void MirrorCameraJobConfig::setEntityID(const QUuid& entityID) {
    _entityID = entityID;
    emit dirty();
}