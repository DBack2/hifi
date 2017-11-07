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

using RenderArgsPointer = std::shared_ptr<RenderArgs>;

MirrorCamera::MirrorCamera(const QUuid& entityID)
    :_entityID(entityID) {
    qApp->getRenderEngine()->addJob<MirrorCameraRenderTask>("MirrorCameraJob" + _entityID.toString().toStdString(), LODManager::shouldRender, entityID);
}

MirrorCamera::~MirrorCamera() {
    qApp->getRenderEngine()->removeJob<MirrorCameraRenderTask>("MirrorCameraJob" + _entityID.toString().toStdString());
}

class MirrorCameraJob {  // Changes renderContext for our framebuffer and view.
public:
    using Config = MirrorCameraJobConfig;
    using JobModel = render::Job::ModelO<MirrorCameraJob, RenderArgsPointer, Config>;
    MirrorCameraJob(const QUuid& entityID) 
        :_entityID(entityID) {
        _cachedArgsPointer = std::make_shared<RenderArgs>(_cachedArgs);
    }

    void configure(const Config& config) {};

    void setProjection(ViewFrustum& srcViewFrustum, float aspect) {
        glm::vec3 eyePos = qApp->getCamera().getPosition();

        EntityPropertyFlags entityPropFlags;
        EntityItemProperties entityProperties = DependencyManager::get<EntityScriptingInterface>()->getEntityProperties(_entityID, entityPropFlags);
        glm::vec3 mirrorPropsPos = entityProperties.getPosition();
        glm::quat mirrorPropsRot = entityProperties.getRotation();
        glm::vec3 mirrorPropsDim = entityProperties.getDimensions();

        // get the 4 mirror's vertices positions on main camera side of the mirror
        // mirrorVertices[0] = upper left vertex (if facing from mirrored camera side of mirror)
        // mirrorVertices[1] = lower left vertex (if facing from mirrored camera side of mirror)
        // mirrorVertices[2] = upper right vertex (if facing from mirrored camera side of mirror)
        // mirrorVertices[3] = lower right vertex (if facing from mirrored camera side of mirror)
        glm::vec3 mirrorVertices[4];
        float vertexPosX, vertexPosY, vertexPosZ;
        for (int i = 0; i < 2; ++i) {
            if (i == 0) {
                vertexPosX = 0.5f * mirrorPropsDim.x;
            } else {
                vertexPosX = -0.5f * mirrorPropsDim.x;
            }
            for (int j = 0; j < 2; ++j) {
                if (j == 0) {
                    vertexPosY = 0.5f * mirrorPropsDim.y;
                } else {
                    vertexPosY = -0.5f * mirrorPropsDim.y;
                }
                vertexPosZ = mirrorPropsDim.z;
                glm::vec3 localPos = glm::vec3(vertexPosX, vertexPosY, vertexPosZ);
                mirrorVertices[j + i * 2] = (mirrorPropsRot * localPos) + mirrorPropsPos;
            }
        }

        // get mirrored camera's position and rotation reflected about the mirror plane
        glm::vec3 mirrorLocalPos = mirrorPropsRot * glm::vec3(0.f, 0.f, 0.f);
        glm::vec3 mirrorWorldPos = mirrorPropsPos + mirrorLocalPos;
        glm::vec3 mirrorToHeadVec = eyePos - mirrorWorldPos;
        glm::vec3 zLocalVecNormalized = mirrorPropsRot * Vectors::UNIT_Z;
        float distanceFromMirror = glm::dot(zLocalVecNormalized, mirrorToHeadVec);
        glm::vec3 mirrorCamPos = eyePos - (2.f * distanceFromMirror * zLocalVecNormalized);
        glm::quat mirrorCamOrientation = glm::inverse(glm::lookAt(mirrorCamPos, mirrorWorldPos, mirrorPropsRot * Vectors::UP));
        srcViewFrustum.setPosition(mirrorCamPos);
        srcViewFrustum.setOrientation(mirrorCamOrientation);

        glm::vec3 pa = mirrorVertices[1]; // lower left vertex (if facing from mirrored camera side of mirror)
        glm::vec3 pb = mirrorVertices[3]; // lower right vertex (if facing from mirrored camera side of mirror)
        glm::vec3 pc = mirrorVertices[0]; // upper left vertex (if facing from mirrored camera side of mirror)
        glm::vec3 pe = mirrorCamPos;
        glm::vec3 papb = pb - pa;
        glm::vec3 papc = pc - pa;
        glm::vec3 va = pa - pe;
        glm::vec3 mirrorNormal = -glm::normalize(glm::cross(papb, papc)); // negate normal to point away from mirrored camera
        float fovRadians = glm::atan((glm::length(papb) + glm::length(papc)) / glm::length(va));

        float minDistance = FLT_MAX;
        float maxDistance = 0.f;
        for (uint8_t i = 0; i < sizeof(mirrorVertices) / sizeof(glm::vec3); ++i) {
            float distance = glm::abs(glm::distance(mirrorVertices[i], mirrorCamPos));
            if (distance < minDistance) {
                minDistance = distance;
            }
            if (distance > maxDistance) {
                maxDistance = distance;
            }
        }
        // initial near clip plane rests at the mirror vertex the minimum distance away from mirrored camera
        // should we use max vertex distance instead? or maybe average distance?
        float _nearClipPlaneDistance = maxDistance;
        float _farClipPlaneDistance = 16000.f;

        // get our standard perspective projection matrix
        glm::mat4 projection = glm::perspective(fovRadians, aspect, _nearClipPlaneDistance, _farClipPlaneDistance);

        srcViewFrustum.setProjection(projection);
    }

    void run(const render::RenderContextPointer& renderContext, RenderArgsPointer& cachedArgs) {
        auto args = renderContext->args;
        auto textureCache = DependencyManager::get<TextureCache>();
        gpu::FramebufferPointer destFramebuffer;
        destFramebuffer = textureCache->getMirrorCameraFramebuffer(_entityID);
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
            setProjection(srcViewFrustum, (float)args->_viewport.z / (float)args->_viewport.w);
            srcViewFrustum.calculate();
            args->pushViewFrustum(srcViewFrustum);
            cachedArgs = _cachedArgsPointer;
        }
    }

protected:
    RenderArgs _cachedArgs;
    RenderArgsPointer _cachedArgsPointer;
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

void MirrorCameraRenderTask::build(JobModel& task, const render::Varying& inputs, render::Varying& outputs, render::CullFunctor cullFunctor, const QUuid& entityID) {
    auto& test = task._jobs.back();
    const auto cachedArg = task.addJob<MirrorCameraJob>("MirrorCamera", entityID);
    const auto items = task.addJob<RenderFetchCullSortTask>("FetchCullSort", cullFunctor);
    assert(items.canCast<RenderFetchCullSortTask::Output>());
    task.addJob<RenderDeferredTask>("RenderDeferredTask", items);
    task.addJob<EndMirrorCameraFrame>("EndMirrorCamera", cachedArg);
}