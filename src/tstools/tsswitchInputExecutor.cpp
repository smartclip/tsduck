//----------------------------------------------------------------------------
//
// TSDuck - The MPEG Transport Stream Toolkit
// Copyright (c) 2005-2019, Thierry Lelegard
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
// 1. Redistributions of source code must retain the above copyright notice,
//    this list of conditions and the following disclaimer.
// 2. Redistributions in binary form must reproduce the above copyright
//    notice, this list of conditions and the following disclaimer in the
//    documentation and/or other materials provided with the distribution.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
// THE POSSIBILITY OF SUCH DAMAGE.
//
//----------------------------------------------------------------------------

#include "tsswitchInputExecutor.h"
#include "tsswitchCore.h"
#include "tsGuard.h"
#include "tsGuardCondition.h"
TSDUCK_SOURCE;


//----------------------------------------------------------------------------
// Constructor and destructor.
//----------------------------------------------------------------------------

ts::tsswitch::InputExecutor::InputExecutor(size_t index, Core& core, Options& opt, Report& log) :
    // Input threads have a high priority to be always ready to load incoming packets in the buffer.
    PluginThread(&opt, opt.appName(), opt.inputs[index], ThreadAttributes().setPriority(ThreadAttributes::GetHighPriority())),
    _core(core),
    _opt(opt),
    _input(dynamic_cast<InputPlugin*>(PluginThread::plugin())),
    _pluginIndex(index),
    _buffer(opt.bufferedPackets),
    _metadata(opt.bufferedPackets),
    _mutex(),
    _todo(),
    _flowControl(false),
    _outputInUse(false),
    _terminated(false),
    _startRequests(0),
    _stopRequests(0),
    _outFirst(0),
    _outCount(0)
{
    // Make sure that the input plugins display their index.
    setLogName(UString::Format(u"%s[%d]", {pluginName(), _pluginIndex}));
}

ts::tsswitch::InputExecutor::~InputExecutor()
{
    // Wait for thread termination.
    waitForTermination();
}


//----------------------------------------------------------------------------
// Implementation of TSP. We do not use "joint termination" in tsswitch.
//----------------------------------------------------------------------------

void ts::tsswitch::InputExecutor::useJointTermination(bool)
{
}

void ts::tsswitch::InputExecutor::jointTerminate()
{
}

bool ts::tsswitch::InputExecutor::useJointTermination() const
{
    return false;
}

bool ts::tsswitch::InputExecutor::thisJointTerminated() const
{
    return false;
}


//----------------------------------------------------------------------------
// Start input.
//----------------------------------------------------------------------------

void ts::tsswitch::InputExecutor::startInput(bool flowControl)
{
    debug(u"InputExecutor: received start request, flow control: %s", {flowControl});

    GuardCondition lock(_mutex, _todo);
    _flowControl = flowControl;
    _startRequests++;
    lock.signal();
}


//----------------------------------------------------------------------------
// Stop input.
//----------------------------------------------------------------------------

void ts::tsswitch::InputExecutor::stopInput()
{
    debug(u"received stop request");

    GuardCondition lock(_mutex, _todo);
    _stopRequests++;
    lock.signal();
}


//----------------------------------------------------------------------------
// Notify the input executor thread of the flow control policy to use.
//----------------------------------------------------------------------------

void ts::tsswitch::InputExecutor::setFlowControl(bool flowControl)
{
    Guard lock(_mutex);
    _flowControl = flowControl;
}


//----------------------------------------------------------------------------
// Terminate input.
//----------------------------------------------------------------------------

void ts::tsswitch::InputExecutor::terminateInput()
{
    GuardCondition lock(_mutex, _todo);
    _terminated = true;
    lock.signal();
}


//----------------------------------------------------------------------------
// Get some packets to output.
// Indirectly called from the output plugin when it needs some packets.
//----------------------------------------------------------------------------

void ts::tsswitch::InputExecutor::getOutputArea(ts::TSPacket*& first, TSPacketMetadata*& data, size_t& count)
{
    GuardCondition lock(_mutex, _todo);
    first = &_buffer[_outFirst];
    data = &_metadata[_outFirst];
    count = std::min(_outCount, _buffer.size() - _outFirst);
    _outputInUse = count > 0;
    lock.signal();
}


//----------------------------------------------------------------------------
// Free output packets (after being sent).
// Indirectly called from the output plugin after sending packets.
//----------------------------------------------------------------------------

void ts::tsswitch::InputExecutor::freeOutput(size_t count)
{
    GuardCondition lock(_mutex, _todo);
    assert(count <= _outCount);
    _outFirst = (_outFirst + count) % _buffer.size();
    _outCount -= count;
    _outputInUse = false;
    lock.signal();
}


//----------------------------------------------------------------------------
// Invoked in the context of the plugin thread.
//----------------------------------------------------------------------------

void ts::tsswitch::InputExecutor::main()
{
    debug(u"InputExecutor: input thread started");

    // Success of the last start and stop operations.
    bool startStatus = false;
    bool stopStatus = false;

    // Main loop. Each iteration is a complete input session.
    for (;;) {

        // First part: notify previous stop, wait for start request, start, notify start.
        // Loop until terminate request or successful session start.
        // Note that _terminate is volatile and never reverts to false once set.
        debug(u"InputExecutor: waiting for input session");
        while (!_terminated && !startStatus) {

            size_t startRequestCount = 0;
            size_t stopRequestCount = 0;

            // Wait for something to notify or something to do.
            {
                // The sequence under mutex protection.
                GuardCondition lock(_mutex, _todo);
                // Reset input buffer.
                _outFirst = 0;
                _outCount = 0;
                // Wait for start or terminate.
                while (_startRequests == 0 && _stopRequests == 0 && !_terminated) {
                    lock.waitCondition();
                }
                startRequestCount = _startRequests;
                stopRequestCount = _stopRequests;
            }
            debug(u"InputExecutor: startRequestCount = %d, stopRequestCount = %d", {startRequestCount, stopRequestCount});

            // Notify a stopped event (we are already stopped) for each stop request.
            for (size_t i = 0; i < stopRequestCount; i--) {
                _core.inputStopped(_pluginIndex, stopStatus);
            }

            // Start the input plugin if requested to do so.
            if (!_terminated && startRequestCount > 0) {
                debug(u"InputExecutor: starting input plugin");
                startStatus = _input->start();
                debug(u"InputExecutor: input plugin started, status: %s", {startStatus});

                // Notify the tsswitch core of the start.
                for (size_t i = 0; i < startRequestCount; i--) {
                    _core.inputStarted(_pluginIndex, startStatus);
                }
            }

            // Update global request counters under mutex protection.
            GuardCondition lock(_mutex, _todo);
            _startRequests -= startRequestCount;
            _stopRequests -= stopRequestCount;
        }

        // Exit main loop when termination is requested.
        if (_terminated) {
            break;
        }

        // Second part: Loop on incoming packets until the end of the session.
        for (;;) {

            // Input area (first packet index and packet count).
            size_t inFirst = 0;
            size_t inCount = 0;

            // Initial sequence under mutex protection.
            {
                // Wait for free buffer or stop.
                GuardCondition lock(_mutex, _todo);
                while (_outCount >= _buffer.size() && _stopRequests == 0 && !_terminated) {
                    if (_flowControl) {
                        // This is typically the current input, we must not lose packet.
                        // Wait for the output thread to free some packets.
                        lock.waitCondition();
                    }
                    else {
                        // Continue input, overwriting old packets.
                        // Drop older packets, free at most --max-input-packets.
                        assert(_outFirst < _buffer.size());
                        const size_t freeCount = std::min(_opt.maxInputPackets, _buffer.size() - _outFirst);
                        assert(freeCount <= _outCount);
                        _outFirst = (_outFirst + freeCount) % _buffer.size();
                        _outCount -= freeCount;
                    }
                }
                // Exit input when termination is requested.
                if (_stopRequests > 0 || _terminated) {
                    break;
                }
                // There is some free buffer, compute first index and size of receive area.
                // The receive area is limited by end of buffer and max input size.
                inFirst = (_outFirst + _outCount) % _buffer.size();
                inCount = std::min(_opt.maxInputPackets, std::min(_buffer.size() - _outCount, _buffer.size() - inFirst));
            }

            assert(inFirst < _buffer.size());
            assert(inFirst + inCount <= _buffer.size());

            // Reset packet metadata.
            for (size_t n = inFirst; n < inFirst + inCount; ++n) {
                _metadata[n].reset();
            }

            // Receive packets.
            if ((inCount = _input->receive(&_buffer[inFirst], &_metadata[inFirst], inCount)) == 0) {
                // End of input.
                debug(u"InputExecutor: received end of input from plugin");
                // Consider it as a stop request.
                Guard lock(_mutex);
                _stopRequests++;
                break;
            }
            log(10, u"InputExecutor: received %d packets from plugin", {inCount});
            addPluginPackets(inCount);

            // Signal the presence of received packets.
            {
                Guard lock(_mutex);
                _outCount += inCount;
            }
            _core.inputReceived(_pluginIndex);
        }

        // At end of session, make sure that the output buffer is not in use by the output plugin.
        {
            // Wait for the output plugin to release the buffer.
            // In case of normal end of input (no stop, no terminate), wait for all output to be gone.
            GuardCondition lock(_mutex, _todo);
            while (_outputInUse || (_outCount > 0 && _stopRequests == 0 && !_terminated)) {
                debug(u"InputExecutor: input terminated, waiting for output plugin to release the buffer");
                lock.waitCondition();
            }
            // And reset the output part of the buffer.
            _outFirst = 0;
            _outCount = 0;
        }

        // End of input session.
        debug(u"InputExecutor: stopping input plugin");
        stopStatus = _input->stop();
        startStatus = false; // no longer started

        // Note: the stop notifications are performed on loopback.
    }

    debug(u"InputExecutor: input thread terminated, %d packets", {pluginPackets()});
}
