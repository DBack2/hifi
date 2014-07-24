//
//  InboundAudioStream.cpp
//  libraries/audio/src
//
//  Created by Yixin Wang on 7/17/2014
//  Copyright 2013 High Fidelity, Inc.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#include "InboundAudioStream.h"
#include "PacketHeaders.h"

InboundAudioStream::InboundAudioStream(int numFrameSamples, int numFramesCapacity, bool dynamicJitterBuffers) :
    _ringBuffer(numFrameSamples, false, numFramesCapacity),
    _dynamicJitterBuffers(dynamicJitterBuffers),
    _desiredJitterBufferFrames(1),
    _isStarved(true),
    _hasStarted(false),
    _consecutiveNotMixedCount(0),
    _starveCount(0),
    _silentFramesDropped(0),
    _incomingSequenceNumberStats(INCOMING_SEQ_STATS_HISTORY_LENGTH_SECONDS),
    _lastFrameReceivedTime(0),
    _interframeTimeGapStatsForJitterCalc(TIME_GAPS_FOR_JITTER_CALC_INTERVAL_SAMPLES, TIME_GAPS_FOR_JITTER_CALC_WINDOW_INTERVALS),
    _interframeTimeGapStatsForStatsPacket(TIME_GAPS_FOR_STATS_PACKET_INTERVAL_SAMPLES, TIME_GAPS_FOR_STATS_PACKET_WINDOW_INTERVALS),
    _framesAvailableStats(FRAMES_AVAILABLE_STATS_INTERVAL_SAMPLES, FRAMES_AVAILABLE_STATS_WINDOW_INTERVALS)
{
}

void InboundAudioStream::reset() {
    _ringBuffer.reset();
    _desiredJitterBufferFrames = 1;
    _isStarved = true;
    _hasStarted = false;
    _consecutiveNotMixedCount = 0;
    _starveCount = 0;
    _silentFramesDropped = 0;
    _incomingSequenceNumberStats.reset();
    _lastFrameReceivedTime = 0;
    _interframeTimeGapStatsForJitterCalc.reset();
    _interframeTimeGapStatsForStatsPacket.reset();
    _framesAvailableStats.reset();
}

int InboundAudioStream::parseData(const QByteArray& packet) {
    frameReceivedUpdateTimingStats();

    PacketType packetType = packetTypeForPacket(packet);
    QUuid senderUUID = uuidFromPacketHeader(packet);

    // parse header 
    int numBytesHeader = numBytesForPacketHeader(packet);
    const char* sequenceAt = packet.constData() + numBytesHeader;
    int readBytes = numBytesHeader;

    // parse sequence number and track it
    quint16 sequence = *(reinterpret_cast<const quint16*>(sequenceAt));
    readBytes += sizeof(quint16);
    SequenceNumberStats::ArrivalInfo arrivalInfo = _incomingSequenceNumberStats.sequenceNumberReceived(sequence, senderUUID);

    // TODO: handle generalized silent packet here?????


    // parse the info after the seq number and before the audio data.(the stream properties)
    int numAudioSamples;
    readBytes += parseStreamProperties(packetType, packet.mid(readBytes), numAudioSamples);

    // handle this packet based on its arrival status.
    // For now, late packets are ignored.  It may be good in the future to insert the late audio frame
    // into the ring buffer to fill in the missing frame if it hasn't been mixed yet.
    switch (arrivalInfo._status) {
    case SequenceNumberStats::Early: {
        int packetsDropped = arrivalInfo._seqDiffFromExpected;
        writeSamplesForDroppedPackets(packetsDropped * numAudioSamples);
        // fall through to OnTime case
    }
    case SequenceNumberStats::OnTime: {
        readBytes += parseAudioData(packetType, packet.mid(readBytes), numAudioSamples);
        break;
    }
    default: {
        break;
    }
    }

    if (_isStarved && _ringBuffer.samplesAvailable() >= _desiredJitterBufferFrames * _ringBuffer.getNumFrameSamples()) {
        _isStarved = false;
    }

    _framesAvailableStats.update(_ringBuffer.framesAvailable());

    return readBytes;
}

bool InboundAudioStream::popFrames(int numFrames, bool starveOnFail) {
    if (_isStarved) {
        _consecutiveNotMixedCount++;
        return false;
    }

    bool popped = false;

    int numSamplesRequested = numFrames * _ringBuffer.getNumFrameSamples();
    if (_ringBuffer.samplesAvailable() >= numSamplesRequested) {
        _ringBuffer.shiftReadPosition(numSamplesRequested);
        _hasStarted = true;
        popped = true;
    } else {
        if (starveOnFail) {
            setToStarved();
            _consecutiveNotMixedCount++;
        }
    }

    _framesAvailableStats.update(_ringBuffer.framesAvailable());

    return popped;
}

bool InboundAudioStream::popFrames(int16_t* dest, int numFrames, bool starveOnFail) {
    if (_isStarved) {
        _consecutiveNotMixedCount++;
        return false;
    }

    bool popped = false;

    int numSamplesRequested = numFrames * _ringBuffer.getNumFrameSamples();
    if (_ringBuffer.samplesAvailable() >= numSamplesRequested) {
        _ringBuffer.readSamples(dest, numSamplesRequested);
        _hasStarted = true;
        popped = true;
    } else {
        if (starveOnFail) {
            setToStarved();
            _consecutiveNotMixedCount++;
        }
    }

    _framesAvailableStats.update(_ringBuffer.framesAvailable());

    return popped;
}

bool InboundAudioStream::popFrames(AudioRingBuffer::ConstIterator* nextOutput, int numFrames, bool starveOnFail) {
    if (_isStarved) {
        _consecutiveNotMixedCount++;
        return false;
    }

    bool popped = false;

    int numSamplesRequested = numFrames * _ringBuffer.getNumFrameSamples();
    if (_ringBuffer.samplesAvailable() >= numSamplesRequested) {
        *nextOutput = _ringBuffer.nextOutput();
        _ringBuffer.shiftReadPosition(numSamplesRequested);
        _hasStarted = true;
        popped = true;
    } else {
        if (starveOnFail) {
            setToStarved();
            _consecutiveNotMixedCount++;
        }
    }

    _framesAvailableStats.update(_ringBuffer.framesAvailable());

    return popped;
}

void InboundAudioStream::setToStarved() {
    _isStarved = true;
    _consecutiveNotMixedCount = 0;
    _starveCount++;
}


int InboundAudioStream::getCalculatedDesiredJitterBufferFrames() const {
    const float USECS_PER_FRAME = NETWORK_BUFFER_LENGTH_SAMPLES_PER_CHANNEL * USECS_PER_SECOND / (float)SAMPLE_RATE;

    int calculatedDesiredJitterBufferFrames = ceilf((float)_interframeTimeGapStatsForJitterCalc.getWindowMax() / USECS_PER_FRAME);
    if (calculatedDesiredJitterBufferFrames < 1) {
        calculatedDesiredJitterBufferFrames = 1;
    }
    return calculatedDesiredJitterBufferFrames;
}


void InboundAudioStream::frameReceivedUpdateTimingStats() {
    // update the two time gap stats we're keeping
    quint64 now = usecTimestampNow();
    if (_lastFrameReceivedTime != 0) {
        quint64 gap = now - _lastFrameReceivedTime;
        _interframeTimeGapStatsForJitterCalc.update(gap);
        _interframeTimeGapStatsForStatsPacket.update(gap);
    }
    _lastFrameReceivedTime = now;

    // recalculate the _desiredJitterBufferFrames if _interframeTimeGapStatsForJitterCalc has updated stats for us
    if (_interframeTimeGapStatsForJitterCalc.getNewStatsAvailableFlag()) {
        if (!_dynamicJitterBuffers) {
            _desiredJitterBufferFrames = 1; // HACK to see if this fixes the audio silence
        } else {
            _desiredJitterBufferFrames = getCalculatedDesiredJitterBufferFrames();

            const int maxDesired = _ringBuffer.getFrameCapacity() - 1;
            if (_desiredJitterBufferFrames > maxDesired) {
                _desiredJitterBufferFrames = maxDesired;
            }
        }
        _interframeTimeGapStatsForJitterCalc.clearNewStatsAvailableFlag();
    }
}

int InboundAudioStream::writeDroppableSilentSamples(int numSilentSamples) {

    // This adds some number of frames to the desired jitter buffer frames target we use.
    // The larger this value is, the less aggressive we are about reducing the jitter buffer length.
    // Setting this to 0 will try to get the jitter buffer to be exactly _desiredJitterBufferFrames long,
    // which could lead immediately to a starve.
    const int DESIRED_JITTER_BUFFER_FRAMES_PADDING = 1;

    // calculate how many silent frames we should drop.  We only drop silent frames if
    // the running avg num frames available has stabilized and it's more than
    // our desired number of frames by the margin defined above.
    int samplesPerFrame = _ringBuffer.getNumFrameSamples();
    int numSilentFramesToDrop = 0;
    if (_framesAvailableStats.getNewStatsAvailableFlag() && _framesAvailableStats.isWindowFilled()
        && numSilentSamples >= samplesPerFrame) {
        _framesAvailableStats.clearNewStatsAvailableFlag();
        int averageJitterBufferFrames = (int)getFramesAvailableAverage();
        int desiredJitterBufferFramesPlusPadding = _desiredJitterBufferFrames + DESIRED_JITTER_BUFFER_FRAMES_PADDING;

        if (averageJitterBufferFrames > desiredJitterBufferFramesPlusPadding) {
            // our avg jitter buffer size exceeds its desired value, so ignore some silent
            // frames to get that size as close to desired as possible
            int numSilentFramesToDropDesired = averageJitterBufferFrames - desiredJitterBufferFramesPlusPadding;
            int numSilentFramesReceived = numSilentSamples / samplesPerFrame;
            numSilentFramesToDrop = std::min(numSilentFramesToDropDesired, numSilentFramesReceived);

            // since we now have a new jitter buffer length, reset the frames available stats.
            _framesAvailableStats.reset();

            _silentFramesDropped += numSilentFramesToDrop;
        }
    }
    return _ringBuffer.addSilentFrame(numSilentSamples - numSilentFramesToDrop * samplesPerFrame);
}

int InboundAudioStream::writeSamplesForDroppedPackets(int numSamples) {
    return writeDroppableSilentSamples(numSamples);
}

AudioStreamStats InboundAudioStream::getAudioStreamStats() const {
    AudioStreamStats streamStats;

    streamStats._timeGapMin = _interframeTimeGapStatsForStatsPacket.getMin();
    streamStats._timeGapMax = _interframeTimeGapStatsForStatsPacket.getMax();
    streamStats._timeGapAverage = _interframeTimeGapStatsForStatsPacket.getAverage();
    streamStats._timeGapWindowMin = _interframeTimeGapStatsForStatsPacket.getWindowMin();
    streamStats._timeGapWindowMax = _interframeTimeGapStatsForStatsPacket.getWindowMax();
    streamStats._timeGapWindowAverage = _interframeTimeGapStatsForStatsPacket.getWindowAverage();

    streamStats._ringBufferFramesAvailable = _ringBuffer.framesAvailable();
    streamStats._ringBufferFramesAvailableAverage = _framesAvailableStats.getWindowAverage();
    streamStats._ringBufferDesiredJitterBufferFrames = _desiredJitterBufferFrames;
    streamStats._ringBufferStarveCount = _starveCount;
    streamStats._ringBufferConsecutiveNotMixedCount = _consecutiveNotMixedCount;
    streamStats._ringBufferOverflowCount = _ringBuffer.getOverflowCount();
    streamStats._ringBufferSilentFramesDropped = _silentFramesDropped;

    streamStats._packetStreamStats = _incomingSequenceNumberStats.getStats();
    streamStats._packetStreamWindowStats = _incomingSequenceNumberStats.getStatsForHistoryWindow();

    return streamStats;
}

AudioStreamStats InboundAudioStream::updateSeqHistoryAndGetAudioStreamStats() {
    _incomingSequenceNumberStats.pushStatsToHistory();
    return getAudioStreamStats();
}
