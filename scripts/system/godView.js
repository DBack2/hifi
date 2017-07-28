"use strict";
//
//  hmd.js
//  scripts/system/
//
//  Created by Howard Stearns on 2 Jun 2016
//  Copyright 2016 High Fidelity, Inc.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//
/* globals HMD, Script, Menu, Tablet, Camera */
/* eslint indent: ["error", 4, { "outerIIFEBody": 0 }] */

print("[godView.js] outside scope");

(function() { // BEGIN LOCAL_SCOPE

var logEnabled = false;
function printd(str) {
    if (logEnabled)
        print("[godView.js] " + str);
}

printd("local scope");

var godView = false;

var GOD_CAMERA_OFFSET = -1; // 1 meter below the avatar
var GOD_VIEW_HEIGHT = 10; // meters above the avatar (30 original)
var ABOVE_GROUND_DROP = 2;
var MOVE_BY = 1;

// Swipe/Drag vars
var PINCH_INCREMENT = 0.4; // 0.1 meters zoom in - out
var GOD_VIEW_HEIGHT_MAX_PLUS_AVATAR = 40;
var GOD_VIEW_HEIGHT_MIN_PLUS_AVATAR = 2;
var GOD_VIEW_CAMERA_DISTANCE_TO_ICONS = 0.5; // Icons are near the camera to prevent the LOD manager dismissing them
var GOD_VIEW_ICONS_APPARENT_DISTANCE_TO_AVATAR_BASE = 1; // How much above the avatar base should the icon appear
var lastDragAt;
var lastDeltaDrag;

function moveTo(position) {
    if (godView) {
        MyAvatar.position = position;
        //Camera.position = Vec3.sum(MyAvatar.position, {x:0, y: GOD_CAMERA_OFFSET, z: 0});
        Camera.position = Vec3.sum(MyAvatar.position, {x:0, y: GOD_VIEW_HEIGHT, z: 0});
    } else {
        MyAvatar.position = position;
    }
}

function keyPressEvent(event) {
    //print(event.text);
    if (godView) {
        switch(event.text) {
            case "UP":
                moveTo(Vec3.sum(MyAvatar.position, {x:0.0, y: 0, z: -1 * MOVE_BY}));
                break;
            case "DOWN":
                moveTo(Vec3.sum(MyAvatar.position, {x:0, y: 0, z: MOVE_BY}));
                break;
            case "LEFT":
                moveTo(Vec3.sum(MyAvatar.position, {x:-1 * MOVE_BY, y: 0, z: 0}));
                break;
            case "RIGHT":
                moveTo(Vec3.sum(MyAvatar.position, {x:MOVE_BY, y: 0, z: 0}));
                break;
        }
    }
}

// App.isAndroid()

function mousePressOrTouchEnd(event) {
    if (!currentTouchIsValid) {
        return;
    }
    if (godView) {
        // TODO remove this return; and enable the feature to move again
        //return;
        printd("-- mousePressOrTouchEnd in godView");
        printd("-- event.x, event.y:", event.x, ",", event.y);
        var pickRay = Camera.computePickRay(event.x, event.y);

        printd("-- pr.o:", pickRay.origin.x, ",", pickRay.origin.y, ",", pickRay.origin.z);
        printd("-- pr.d:", pickRay.direction.x, ",", pickRay.direction.y, ",", pickRay.direction.z);
        printd("-- c.p:", Camera.position.x, ",", Camera.position.y, ",", Camera.position.z);

        var pointingAt = Vec3.sum(pickRay.origin, Vec3.multiply(pickRay.direction,GOD_VIEW_HEIGHT));

        printd("-- pointing at:", pointingAt.x, ",", pointingAt.y, ",", pointingAt.z);

        var moveToPosition = { x: pointingAt.x, y: MyAvatar.position.y, z: pointingAt.z };

        printd("-- moveToPosition:", moveToPosition.x, ",", moveToPosition.y, ",", moveToPosition.z);

        moveTo(moveToPosition);
    }
}

function toggleGodViewMode() {
    printd("-- toggleGodViewMode");
    if (godView) {
        endGodView();
    } else {
        startGodView();
    }
}

function fakeDoubleTap() {
    toggleGodViewMode();
}

var currentTouchIsValid = false;
var DOUBLE_TAP_TIME = 200;
var fakeDoubleTapStart = Date.now();
var touchEndCount = 0;
function touchEnd(event) {
    lastDragAt = null;
    lastDeltaDrag = null;
    startedDraggingCamera = false;
    if (draggingCamera) {
        draggingCamera = false;
        return;
    }
    if (event.isPinching) return;
    if (event.isPinchOpening) return;
    if (event.isMoved) return;
    if (!currentTouchIsValid) return;

    printd("-- touchEndEvent ... touchEndCount:" + touchEndCount);
    var fakeDoubleTapEnd = Date.now();
    printd("-- fakeDoubleTapEnd:" + fakeDoubleTapEnd);
    var elapsed = fakeDoubleTapEnd - fakeDoubleTapStart;
    printd("-- elapsed:" + elapsed);
    if (elapsed > DOUBLE_TAP_TIME) {
        printd("-- elapsed:" + elapsed + " to long for double tap, resetting!");
        touchEndCount = 0;
    }
    
    // if this is our first "up" then record time so we can
    // later determine if second "up" is a double tap
    if (touchEndCount == 0) {
        fakeDoubleTapStart = Date.now();
        printd("-- starting out as a first click... fakeDoubleTapStart:" + fakeDoubleTapStart);
    }
    touchEndCount++;
    
    if (touchEndCount >= 2) {
        var fakeDoubleTapEnd = Date.now();
        printd("-- fakeDoubleTapEnd:" + fakeDoubleTapEnd);
        var elapsed = fakeDoubleTapEnd - fakeDoubleTapStart;
        printd("-- elapsed:" + elapsed);
        if (elapsed <= DOUBLE_TAP_TIME) {
            printd("-- FAKE double tap event!!!  elapsed:" + elapsed);

            touchEndCount = 0;
            fakeDoubleTap();
            return; // don't do the normal touch end processing
        } else {
            printd("-- too slow.... not a double tap elapsed:" + elapsed);
        }

        touchEndCount = 0;
    }
    mousePressOrTouchEnd(event);
}

/**
* Polyfill for sign(x)
*/
if (!Math.sign) {
  Math.sign = function(x) {
    // If x is NaN, the result is NaN.
    // If x is -0, the result is -0.
    // If x is +0, the result is +0.
    // If x is negative and not -0, the result is -1.
    // If x is positive and not +0, the result is +1.
    x = +x; // convert to a number
    if (x === 0 || isNaN(x)) {
      return Number(x);
    }
    return x > 0 ? 1 : -1;
  };
}

/**
* findLinePlaneIntersection
* Given points p {x: y: z:} and q that define a line, and the plane
* of formula ax+by+cz+d = 0, returns the intersection point or null if none.
*/
function findLinePlaneIntersection(p, q, a, b, c, d) {
    return findLinePlaneIntersectionCoords(p.x, p.y, p.z, q.x, q.y, q.z, a, b, c, d);
}

/**
* findLineToHeightIntersection
* Given points p {x: y: z:} and q that define a line, and a planeY
* value that defines a plane paralel to 'the floor' xz plane,
* returns the intersection to that plane or null if none.
*/
function findLineToHeightIntersection(p, q, planeY) {
    return findLinePlaneIntersection(p, q, 0, 1, 0, -planeY);
}

/**
* findLinePlaneIntersectionCoords (to avoid requiring unnecessary instantiation)
* Given points p with px py pz and q that define a line, and the plane
* of formula ax+by+cz+d = 0, returns the intersection point or null if none.
*/
function findLinePlaneIntersectionCoords(px, py, pz, qx, qy, qz, a, b, c, d) {
    var tDenom = a*(qx-px) + b*(qy-py) + c*(qz-pz);
    if (tDenom == 0) return null;

    var t = - ( a*px + b*py + c*pz + d ) / tDenom;

    return {
        x: (px+t*(qx-px)),
        y: (py+t*(qy-py)),
        z: (pz+t*(qz-pz))
    };
}

/**
* findLineToHeightIntersection
* Given points p with px py pz and q that define a line, and a planeY
* value that defines a plane paralel to 'the floor' xz plane,
* returns the intersection to that plane or null if none.
*/
function findLineToHeightIntersectionCoords(px, py, pz, qx, qy, qz, planeY) {
    return findLinePlaneIntersectionCoords(px, py, pz, qx, qy, qz, 0, 1, 0, -planeY);
}

function touchBegin(event) {
  if (tablet.getItemAtPoint({ x: event.x, y: event.y }) ) {
    currentTouchIsValid = false;
  } else {
    currentTouchIsValid = true;

  }
}

var startedDraggingCamera = false;
var draggingCamera = false;

function touchUpdate(event) {
    if (!currentTouchIsValid) {
        // avoid moving and zooming when tap is over UI entities
        return;
    }
    if (event.isPinching || event.isPinchOpening) {
        printd("touchUpdate HERE - " + "isPinching");
        if (event.isMoved) {
            // pinch management
            var avatarY = MyAvatar.position.y;
            if (event.isPinching) {
                if (GOD_VIEW_HEIGHT + PINCH_INCREMENT > GOD_VIEW_HEIGHT_MAX_PLUS_AVATAR + avatarY) {
                    GOD_VIEW_HEIGHT = GOD_VIEW_HEIGHT_MAX_PLUS_AVATAR + avatarY;
                } else {
                    GOD_VIEW_HEIGHT = GOD_VIEW_HEIGHT + PINCH_INCREMENT;
                }
            } else if (event.isPinchOpening) {
                if (GOD_VIEW_HEIGHT - PINCH_INCREMENT < GOD_VIEW_HEIGHT_MIN_PLUS_AVATAR + avatarY) {
                    GOD_VIEW_HEIGHT = GOD_VIEW_HEIGHT_MIN_PLUS_AVATAR + avatarY;
                } else {
                    GOD_VIEW_HEIGHT = GOD_VIEW_HEIGHT - PINCH_INCREMENT;
                }
            }
            var deltaHeight = avatarY + GOD_VIEW_HEIGHT - Camera.position.y;
            Camera.position = Vec3.sum(Camera.position, {x:0, y: deltaHeight, z: 0});
            if (!draggingCamera) {
                startedDraggingCamera = true;
                draggingCamera = true;
            }
        }
    } else {
        if (event.isMoved) {
            // drag management
            printd("touchUpdate HERE - " + "isMoved --------------------");
            //event.x
            var pickRay = Camera.computePickRay(event.x, event.y);
            var dragAt = Vec3.sum(pickRay.origin, Vec3.multiply(pickRay.direction, GOD_VIEW_HEIGHT));

            printd("touchUpdate HERE - " + " pickRay.direction " + JSON.stringify(pickRay.direction));

            if (lastDragAt === undefined || lastDragAt === null) {
                lastDragAt = dragAt;
                // TODO CLEANUP WHEN RELEASING
            } else {
                printd("touchUpdate HERE - " + " event " + event.x + " x " + event.y);
                printd("touchUpdate HERE - " + " lastDragAt " + JSON.stringify(lastDragAt));
                printd("touchUpdate HERE - " + " dragAt " + JSON.stringify(dragAt));

                var deltaDrag = {x: (lastDragAt.x - dragAt.x), y: 0, z: (lastDragAt.z-dragAt.z)};

                lastDragAt = dragAt;
                if (lastDeltaDrag === undefined || lastDeltaDrag === null) {
                    lastDeltaDrag = deltaDrag;
                    return;
                }

                if (Math.sign(deltaDrag.x) == Math.sign(lastDeltaDrag.x) && Math.sign(deltaDrag.z) == Math.sign(lastDeltaDrag.z)) {
                    // Process movement if direction of the movement is the same than the previous frame

                    // process delta
                    var moveCameraTo = Vec3.sum(Camera.position, deltaDrag);
                    printd("touchUpdate HERE - " + " x diff " + (lastDragAt.x - dragAt.x));
                    printd("touchUpdate HERE - " + " moveCameraFrom " + JSON.stringify(Camera.position));
                    printd("touchUpdate HERE - " + " moveCameraTo " + JSON.stringify(moveCameraTo));
                    // move camera
                    Camera.position = moveCameraTo;
                    if (!draggingCamera) {
                        startedDraggingCamera = true;
                        draggingCamera = true;
                    }
                } else {
                    // Do not move camera if it's changing direction in this case, wait until the next direction confirmation..
                }
                lastDeltaDrag = deltaDrag;
                // save last
            }
        }
    }
}

// by QUuid
var avatarsData = {};
var avatarsIcons = []; // a parallel list of icons (overlays) to easily run through

var ICON_MY_AVATAR_MODEL_URL = Script.resolvePath("assets/models/teleport-destination.fbx"); // FIXME - use correct model&texture
var ICON_AVATAR_MODEL_URL = Script.resolvePath("assets/models/teleport-cancel.fbx"); // FIXME - use correct model&texture
var ICON_AVATAR_DEFAULT_DIMENSIONS = {
    x: 0.10,
    y: 0.00001,
    z: 0.10
};

var targetModelDimensionsVal = { x: 0, y: 0.00001, z: 0};
function targetModelDimensions() {
    // given the current height, give a size
    var xz = -0.002831 * GOD_VIEW_HEIGHT + 0.1;
    targetModelDimensionsVal.x = xz;
    targetModelDimensionsVal.z = xz;
    // reuse object
    return targetModelDimensionsVal;
}

function currentOverlayForAvatar(QUuid) {
    if (avatarsData[QUuid] != undefined) {
        return avatarsData[QUuid].icon;
    } else {
        return null;
    }
}

function saveAvatarData(QUuid) {
    if (QUuid == null) return;
    var avat = AvatarList.getAvatar(QUuid);
    printd("avatar added save avatar " + QUuid);
    if (avatarsData[QUuid] != undefined) {
        avatarsData[QUuid].position = avat.position;
    } else {
        var avatarIcon = Overlays.addOverlay("model", {
            url: ICON_AVATAR_MODEL_URL,
            dimensions: ICON_AVATAR_DEFAULT_DIMENSIONS,
            visible: false
        });
        avatarsIcons.push(avatarIcon);
        avatarsData[QUuid] = { position: avat.position, icon: avatarIcon};
        printd("avatar added save avatar DONE " + JSON.stringify(avatarsData[QUuid]));
    }
}

function removeAvatarData(QUuid) {
    if (QUuid == null) return;
    delete avatarsData[QUuid];
    // icon overlay not taken care here
}

function saveAllOthersAvatarsData() {
    var avatarIds = AvatarList.getAvatarIdentifiers();
    var len = avatarIds.length;
    for (var i = 0; i < len; i++) {
        if (avatarIds[i]) {
            saveAvatarData(avatarIds[i]);
        }
    }
}

function renderAllOthersAvatarIcons() {
    var avatarPos;
    var iconDimensions = targetModelDimensions();
    for (var QUuid in avatarsData) {
        //printd("avatar icon avatar possible " + QUuid);
        if (avatarsData.hasOwnProperty(QUuid)) {
            if (AvatarList.getAvatar(QUuid) != null) {
                avatarPos = AvatarList.getAvatar(QUuid).position;
                //printd("avatar icon for avatar " + QUuid);
                if (avatarsData[QUuid].icon != undefined) {
                    //printd("avatar icon " + avatarsData[QUuid].icon + " for avatar " + QUuid);
                    var iconPos = findLineToHeightIntersectionCoords(   avatarPos.x, avatarPos.y + GOD_VIEW_ICONS_APPARENT_DISTANCE_TO_AVATAR_BASE, avatarPos.z,
                                                                        Camera.position.x, Camera.position.y, Camera.position.z,
                                                                        Camera.position.y - GOD_VIEW_CAMERA_DISTANCE_TO_ICONS);
                    if (!iconPos) { print ("avatar icon pos bad for " + QUuid); continue; }
                    printd("avatar icon pos " + QUuid + " pos " + JSON.stringify(iconPos));
                    Overlays.editOverlay(avatarsData[QUuid].icon, {
                        visible: true,
                        dimensions: iconDimensions,
                        position: iconPos
                    });
                }
            }
        }
    }
}

function startGodView() {
    printd("avatar added list " + JSON.stringify(AvatarList.getAvatarIdentifiers()));
    printd("avatar added my avatar is  " + MyAvatar.sessionUUID);
    saveAllOthersAvatarsData();
    printd("-- startGodView " + JSON.stringify(avatarsData));
    // Do not move the avatar when going to GodView, only the camera.
    //Camera.mode = "first person";
    //MyAvatar.position = Vec3.sum(MyAvatar.position, {x:0, y: GOD_VIEW_HEIGHT, z: 0});
    Camera.mode = "independent";

    Camera.position = Vec3.sum(MyAvatar.position, {x:0, y: GOD_VIEW_HEIGHT, z: 0});
    Camera.orientation = Quat.fromPitchYawRollDegrees(-90,0,0);
    godView = true;
}

function endGodView() {
    printd("-- endGodView");
    Camera.mode = "first person";
    godView = false;
}

var button;
var tablet = Tablet.getTablet("com.highfidelity.interface.tablet.system");

function onClicked() {
    toggleGodViewMode();
}

var MY_AVATAR_CIRCLE_COLOR = { red: 255, green: 0, blue: 0 };
var MY_AVATAR_CIRCLE_ALPHA = 1;//0.5;
var MY_AVATAR_CIRCLE_ROTATION = Quat.fromPitchYawRollDegrees(0, 90, 0);

var myAvatarIcon = Overlays.addOverlay("model", {
    url: ICON_MY_AVATAR_MODEL_URL,
    dimensions: ICON_AVATAR_DEFAULT_DIMENSIONS,
    visible: false
});


function renderMyAvatarIcon() {
    var iconPos = findLineToHeightIntersectionCoords(   MyAvatar.position.x,
                                                        MyAvatar.position.y + GOD_VIEW_ICONS_APPARENT_DISTANCE_TO_AVATAR_BASE,
                                                        MyAvatar.position.z,
                                                        Camera.position.x, Camera.position.y, Camera.position.z,
                                                        Camera.position.y - GOD_VIEW_CAMERA_DISTANCE_TO_ICONS);
    if (!iconPos) { printd("avatarmy icon pos null"); return;}
    var iconDimensions = targetModelDimensions();
    printd("avatarmy icon pos " + JSON.stringify(iconPos));
    Overlays.editOverlay(myAvatarIcon, {
            visible: true,
            dimensions: iconDimensions,
            position: iconPos
    });
}

function hideAllAvatarIcons() {
    var len = avatarsIcons.length;
    for (var i = 0; i < len; i++) {
        Overlays.editOverlay(avatarsIcons[i], {visible: false});
    }
    Overlays.editOverlay(myAvatarIcon, {visible: false})
}

function updateGodView() {
    // Update avatar icons
    if (godView) {
        if (startedDraggingCamera) {
            hideAllAvatarIcons();
            startedDraggingCamera = false;
        } else if (!draggingCamera) {
            renderMyAvatarIcon();
            renderAllOthersAvatarIcons();
        }
    }
}

function avatarAdded(QUuid) {
    printd("avatar added " + QUuid + " at " + JSON.stringify(AvatarList.getAvatar(QUuid).position));
    saveAvatarData(QUuid);
}

function avatarRemoved(QUuid) {
    printd("avatar removed " + QUuid);
    var itsOverlay =  currentOverlayForAvatar(QUuid);
    if (itsOverlay != null) {
        Overlays.deleteOverlay(itsOverlay);
    }
    var idx = avatarsIcons.indexOf(itsOverlay);
    avatarsIcons.splice(idx, 1);
    removeAvatarData(QUuid);
}

button = tablet.addButton({
    icon: "icons/tablet-icons/switch-desk-i.svg", // FIXME - use correct icon
    text: "God View",
    sortOrder: 2
});

button.clicked.connect(onClicked);
Controller.keyPressEvent.connect(keyPressEvent);
Controller.mousePressEvent.connect(mousePressOrTouchEnd);
Controller.touchEndEvent.connect(touchEnd);

Controller.touchUpdateEvent.connect(touchUpdate);
Controller.touchBeginEvent.connect(touchBegin);

Script.update.connect(updateGodView);
printd("avatar icon - connected update?.. maybe");

AvatarList.avatarAddedEvent.connect(avatarAdded);
AvatarList.avatarRemovedEvent.connect(avatarRemoved);

LODManager.LODDecreased.connect(function() {printd("LOD DECREASED --");});
LODManager.LODIncreased.connect(function() {printd("LOD INCREASED ++");});

Script.scriptEnding.connect(function () {
    if (godView) {
        endGodView();
    }
    button.clicked.disconnect(onClicked);
    if (tablet) {
        tablet.removeButton(button);
    }
    Controller.keyPressEvent.disconnect(keyPressEvent);
    Controller.mousePressEvent.disconnect(mousePressOrTouchEnd);
    Controller.touchEndEvent.disconnect(touchEnd);
});

}()); // END LOCAL_SCOPE
