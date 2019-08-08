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

#include "tsswitchCore.h"
#include "tsGuard.h"
#include "tsGuardCondition.h"
#include "tsFatal.h"
TSDUCK_SOURCE;


//----------------------------------------------------------------------------
// Constructor and destructor.
//----------------------------------------------------------------------------

ts::tsswitch::Core::Core(Options& opt, Report& log) :
    _opt(opt),
    _log(log),
    _inputs(_opt.inputs.size(), nullptr),
    _output(*this, opt, log), // load output plugin and analyze options
    _watchDog(this, _opt.receiveTimeout, 0, _log),
    _mutex(),
    _gotInput(),
    _state(CORE_STOPPED),
    _curPlugin(_opt.firstInput),
    _nextPlugin(_curPlugin),
    _timeoutPlugin(_inputs.size()),
    _curCycle(0),
    _terminate(false),
    _inStates(_opt.inputs.size(), INPUT_STOPPED)
{
    // Load all input plugins, analyze their options.
    for (size_t i = 0; i < _inputs.size(); ++i) {
        _inputs[i] = new InputExecutor(i, *this, opt, log);
        CheckNonNull(_inputs[i]);
        // Set the asynchronous logger as report method for all executors.
        _inputs[i]->setReport(&_log);
        _inputs[i]->setMaxSeverity(_log.maxSeverity());
    }

    // Set the asynchronous logger as report method for output as well.
    _output.setReport(&_log);
    _output.setMaxSeverity(_log.maxSeverity());
}

ts::tsswitch::Core::~Core()
{
    // Deallocate all input plugins.
    // The destructor of each plugin waits for its termination.
    for (size_t i = 0; i < _inputs.size(); ++i) {
        delete _inputs[i];
    }
    _inputs.clear();
}


//----------------------------------------------------------------------------
// Get some packets to output (called by output plugin).
//----------------------------------------------------------------------------

bool ts::tsswitch::Core::getOutputArea(size_t& pluginIndex, TSPacket*& first, TSPacketMetadata*& data, size_t& count)
{
    assert(pluginIndex < _inputs.size());

    // Loop on _gotInput condition until the current input plugin has something to output.
    GuardCondition lock(_mutex, _gotInput);
    for (;;) {
        if (_terminate) {
            first = nullptr;
            count = 0;
        }
        else {
            _inputs[_curPlugin]->getOutputArea(first, data, count);
        }
        // Return when there is something to output in current plugin or the application terminates.
        if (count > 0 || _terminate) {
            // Tell the output plugin which input plugin is used.
            pluginIndex = _curPlugin;
            // Return false when the application terminates.
            return !_terminate;
        }
        // Otherwise, sleep on _gotInput condition.
        lock.waitCondition();
    }
}


//----------------------------------------------------------------------------
// Report output packets (called by output plugin).
//----------------------------------------------------------------------------

bool ts::tsswitch::Core::outputSent(size_t pluginIndex, size_t count)
{
    assert(pluginIndex < _inputs.size());

    // Inform the input plugin that the packets can be reused for input.
    // We notify the original input plugin from which the packets came.
    // The "current" input plugin may have changed in the meantime.
    _inputs[pluginIndex]->freeOutput(count);

    // Return false when the application terminates.
    return !_terminate;
}


//----------------------------------------------------------------------------
// Cancel or restart current timeout. Must be called with mutex held.
//----------------------------------------------------------------------------

void ts::tsswitch::Core::cancelTimeout()
{
    _timeoutPlugin = _inputs.size();
    _watchDog.suspend();
}

void ts::tsswitch::Core::restartTimeout(size_t index)
{
    _timeoutPlugin = index;
    _watchDog.restart();
}


//----------------------------------------------------------------------------
// Start or stop an input plugin. Must be called with mutex held.
//----------------------------------------------------------------------------

void ts::tsswitch::Core::startPlugin(size_t index, bool flowControl)
{
    assert(index < _inputs.size());
    _log.debug(u"Core: starting plugin %d", {index});

    _inStates[index] = INPUT_STARTING;
    _inputs[index]->startInput(flowControl);
}

void ts::tsswitch::Core::stopPlugin(size_t index, bool abortInput)
{
    assert(index < _inputs.size());
    _log.debug(u"Core: stopping plugin %d", {index});

    _inStates[index] = INPUT_STOPPING;
    // Abort current input operation if requested. This is immediate, no wait.
    if (abortInput && !_inputs[index]->plugin()->abortInput()) {
        _log.warning(u"input plugin %s does not support interruption, blocking may occur", {_inputs[index]->pluginName()});
    }
    _inputs[index]->stopInput();
}


//----------------------------------------------------------------------------
// Start the tsswitch processing.
//----------------------------------------------------------------------------

bool ts::tsswitch::Core::start()
{
    // Must be stopped to start.
    if (_state != CORE_STOPPED) {
        _log.error(u"wrong switch core state %d, cannot start", {_state});
        return false;
    }

    // Get all input plugin options.
    for (size_t i = 0; i < _inputs.size(); ++i) {
        if (!_inputs[i]->plugin()->getOptions()) {
            return false;
        }
    }

    // Start output plugin.
    if (!_output.plugin()->getOptions() ||  // Let plugin fetch its command line options.
        !_output.plugin()->start() ||       // Open the output "device", whatever it means.
        !_output.start())                   // Start the output thread.
    {
        return false;
    }

    // Start with the designated first input plugin.
    assert(_opt.firstInput < _inputs.size());
    _curPlugin = _nextPlugin = _opt.firstInput;

    // Start all input threads (but do not open the input "devices").
    for (size_t i = 0; i < _inputs.size(); ++i) {
        // Here, start() means start the thread, not start input plugin.
        if (!_inputs[i]->start()) {
            // If one input thread could not start, abort all started threads.
            stop(false);
            return false;
        }
    }

    if (_opt._strategy == FAST_SWITCH) {
        // Option --fast-switch, start all plugins, they continue to receive in parallel.
        for (size_t i = 0; i < _inputs.size(); ++i) {
            // Flow control is enabled for the current and primary input plugin (if there is one).
            // If the primary is defined and produces input, it will rapidly become the current
            // plugin (after the first input) and the initial current one will immediately drop
            // flow control.
            startPlugin(i, i == _curPlugin || i == _opt.primaryInput);
        }
    }
    else {
        // Start the first plugin only.
        startPlugin(_curPlugin, true);

        // If there is a primary input which is not the first one, start it as well.
        // See comment above about flow control.
        if (_opt.primaryInput < _inputs.size() && _opt.primaryInput != _curPlugin) {
            startPlugin(_opt.primaryInput, true);
        }
    }

    _state = CORE_STARTING_NEXT;
    return true;
}


//----------------------------------------------------------------------------
// Stop the tsswitch processing.
//----------------------------------------------------------------------------

void ts::tsswitch::Core::stop(bool success)
{
    // Wake up all threads waiting for something on the tsswitch::Core object.
    {
        GuardCondition lock(_mutex, _gotInput);
        _terminate = true;
        lock.signal();
    }

    // Tell the output plugin to terminate.
    _output.terminateOutput();

    // Tell all input plugins to terminate.
    for (size_t i = 0; success && i < _inputs.size(); ++i) {
        _inputs[i]->terminateInput();
        _inStates[i] = INPUT_STOPPED;
    }

    _state = CORE_STOPPED;
}


//----------------------------------------------------------------------------
// Get next input plugin index, either upward or downward.
//----------------------------------------------------------------------------

size_t ts::tsswitch::Core::nextInputIndex(size_t index, Direction dir) const
{
    switch (dir) {
        case UPWARD:
            return (index + 1) % _inputs.size();
        case DOWNWARD:
            return index > 0 ? index - 1 : _inputs.size() - 1;
        case UNCHANGED:
            return index;
        default:
            assert(false);          // should not get there
            return _inputs.size();  // invalid value
    }
}


//----------------------------------------------------------------------------
// Switch input plugins.
//----------------------------------------------------------------------------

void ts::tsswitch::Core::setInput(size_t index)
{
    Guard lock(_mutex);
    setInputLocked(index, false, UNCHANGED);
}

// For next and previous commands, use _nextPlugin and not _curPlugin.
// When the two are different, we are in a switching phase and,
// in that case, _nextPlugin is the last selected one by the user.

void ts::tsswitch::Core::nextInput()
{
    Guard lock(_mutex);
    setInputLocked(nextInputIndex(_nextPlugin, UPWARD), false, UPWARD);
}

void ts::tsswitch::Core::previousInput()
{
    Guard lock(_mutex);
    setInputLocked(nextInputIndex(_nextPlugin, DOWNWARD), false, DOWNWARD);
}


//----------------------------------------------------------------------------
// Change input plugin with mutex already held.
//----------------------------------------------------------------------------

void ts::tsswitch::Core::setInputLocked(size_t index, bool abortCurrent, Direction dir)
{
    if (index == _nextPlugin) {
        // We are already switching to this one.
        return;
    }

    if (index >= _inputs.size()) {
        _log.warning(u"invalid input index %d", {index});
        return;
    }

    // Check core state. We can switch only when we are stable.
    switch (_state) {
        case CORE_RUNNING:
            // Correct state, can continue.
            _log.debug(u"Core: switching input %d to %d", {_nextPlugin, index});
            break;
        case CORE_STARTING_NEXT:
            _log.verbose(u"currently starting input %d, cannot switch to plugin %d now, try later", {_nextPlugin, index});
            return;
        case CORE_STOPPING_PREVIOUS:
            _log.verbose(u"currently stopping input %d, cannot switch to plugin %d now, try later", {_curPlugin, index});
            return;
        case CORE_STOPPED:
        default:
            _log.error(u"wrong switch core state %d, cannot switch to plugin %d", {_state, index});
            return;
    }

    // The processing depends on the switching mode.
    switch (_opt._strategy) {
        case SEQUENTIAL_SWITCH: {
            // Stop the current plugin and then start the next one when the current stop is completed.
            _nextPlugin = index;
            cancelTimeout();
            if (_curPlugin == _opt.primaryInput) {
                // The primary input is never stopped (and consequently never restarted).
                _state = CORE_STARTING_NEXT;
                _curPlugin = index;
                // Directly start the next plugin. This is asynchronous and will be notified by inputStarted().
                // See you there for the rest of the switching operation.
                startPlugin(index, true);
            }
            else {
                // Current input is not the primary input, stop it.
                // This is asynchronous and will be notified by inputStopped().
                // See you there for the rest of the switching operation.
                _state = CORE_STOPPING_PREVIOUS;
                stopPlugin(_curPlugin, abortCurrent);
            }
            break;
        }
        case DELAYED_SWITCH: {
            // With delayed switch, first start the next plugin.
            // The current plugin will be stopped when the first packet is received in the next plugin.
            _nextPlugin = index;
            cancelTimeout();
            if (index == _opt.primaryInput && _inStates[index] == INPUT_RUNNING) {
                // The primary input is never stopped (and consequently never restarted).
                // Stop the current plugin. This is asynchronous and will be notified by inputStopped().
                stopPlugin(_curPlugin, false);
                // But we are immediately operational for input on the primary.
                _state = CORE_RUNNING;
                _curPlugin = index;
                restartTimeout(index);
            }
            else {
                // Directly start the next plugin. This is asynchronous and will be notified by inputStarted().
                // See you there for the rest of the switching operation.
                startPlugin(index, true);
                _state = CORE_STARTING_NEXT;
            }
            break;
        }
        case FAST_SWITCH: {
            // With fast switching, there is no switching phase, current and next are always identical.
            assert(_curPlugin == _nextPlugin);
            // Make sure the target plugin is started (can be in startup phase or plugin could not start).
            // If not started, automatically switch to next one.
            size_t target = index;
            while (_inStates[target] != INPUT_RUNNING) {
                if (dir == UNCHANGED) {
                    // Don't try another one.
                    _log.warning(u"input plugin %d not started", {target});
                    return;
                }
                _log.warning(u"input plugin %d not started, trying next one", {target});
                target = nextInputIndex(target, dir);
                if (target == index) {
                    // Back to the beginning, no plugin is started.
                    _log.warning(u"no input plugin started, won't switch");
                    return;
                }
            }
            // Now we know where to switch. Do nothing if you are back to current.
            if (target != _curPlugin) {
                _inputs[_curPlugin]->setFlowControl(false);
                _curPlugin = _nextPlugin = target;
                _inputs[_curPlugin]->setFlowControl(true);
                restartTimeout(_curPlugin);
            }
            break;
        }
        default: {
            _log.error(u"invalid input switching strategy %d", {_opt._strategy});
            return;
        }
    }
}


//----------------------------------------------------------------------------
// Invoked when the receive timeout expires.
// Implementation of WatchDogHandlerInterface.
//----------------------------------------------------------------------------

void ts::tsswitch::Core::handleWatchDogTimeout(WatchDog& watchdog)
{
    Guard lock(_mutex);

    // Filter out spurious call.
    // May happen when the notification is delivered after the timeout was canceled.
    if (_timeoutPlugin < _inputs.size()) {

        // Check if you are in the middle of a delayed switch.
        if (_opt._strategy == DELAYED_SWITCH && _state == CORE_STARTING_NEXT && _timeoutPlugin == _nextPlugin) {
            // We started the next plugin while the current one was still running.
            // But we could not receive data on this plugin within the timeout.
            // Stop the plugin (unless this the primary input).
            if (_nextPlugin != _opt.primaryInput) {
                stopPlugin(_nextPlugin, true);
            }
            // Revert to previous plugin (cancel the switch operation).
            _nextPlugin = _curPlugin;
            _state = CORE_RUNNING;
        }

        // Switch to the next plugin after the one that timed-out.
        _log.verbose(u"receive timeout, switching to next plugin");
        setInputLocked(nextInputIndex(_timeoutPlugin, UPWARD), false, UPWARD);
    }
}


//----------------------------------------------------------------------------
// Report completion of input start (called by input plugins).
//----------------------------------------------------------------------------

bool ts::tsswitch::Core::inputStarted(size_t index, bool success)
{
    assert(index < _inputs.size());
    _log.debug(u"Core: plugin %d started", {index});

    Guard lock(_mutex);

    // If already started, do nothing. Must be a spurious call.
    if (_inStates[index] == INPUT_RUNNING) {
        return !_terminate;
    }

    // Update plugin states.
    _inStates[index] = INPUT_RUNNING;

    // If this is not the "next" plugin, then nothing more to do.
    if (index != _nextPlugin) {
        // Return false when the application terminates.
        return !_terminate;
    }

    // The processing depends on the switching mode.
    switch (_opt._strategy) {
        case SEQUENTIAL_SWITCH: {
            // Stop the current plugin and then start the next one when the current stop is completed.
            // This is the end of a switching process.
            _state = CORE_RUNNING;
            // With sequential switch, the previous plugin was already stopped and the current one is already the starting one.
            assert(_curPlugin == _nextPlugin);
            break;
        }
        case DELAYED_SWITCH: {
            // The previous plugin is still running and current.
            // The next plugin has just started (this notification).
            // We now wait for input in the next plugin to make it current and stop the previous one.
            assert(_state == CORE_STARTING_NEXT);
            break;
        }
        case FAST_SWITCH: {
            // With fast switching, there is no switching phase, current and next are always identical.
            assert(_curPlugin == _nextPlugin);
            break;
        }
        default: {
            _log.error(u"invalid input switching strategy %d", {_opt._strategy});
            return !_terminate;
        }
    }

    // Place a timeout on the first input operation.
    restartTimeout(_curPlugin);

    // Return false when the application terminates.
    return !_terminate;
}


//----------------------------------------------------------------------------
// Report input reception of packets (called by input plugins).
//----------------------------------------------------------------------------

bool ts::tsswitch::Core::inputReceived(size_t index)
{
    assert(index < _inputs.size());
    _log.log(10, u"Core: input received from plugin %d", {index});

    GuardCondition lock(_mutex, _gotInput);

    // If we receive the first input of the next plugin in a delayed switch, complete the switch operation.
    if (_opt._strategy == DELAYED_SWITCH && _state == CORE_STARTING_NEXT && index == _nextPlugin) {
        assert(_curPlugin != _nextPlugin);
        // Stop the previous plugin if not the primary one.
        if (_curPlugin != _opt.primaryInput) {
            stopPlugin(_curPlugin, false);
        }
        // Switch to next plugin to current.
        _curPlugin = _nextPlugin;
        _state = CORE_RUNNING;
    }

    // If input is detected on the primary input and the current plugin is not this one, automatically switch to it.
    if (index == _opt.primaryInput && _curPlugin != _opt.primaryInput) {
        if (_opt._strategy == FAST_SWITCH) {
            // With fast switching, simply make the current plugin stop flow control and continuously receive packets.
            _inputs[_curPlugin]->setFlowControl(false);
            if (_nextPlugin != _curPlugin && _nextPlugin != index) {
                _inputs[_nextPlugin]->setFlowControl(false);
            }
        }
        else {
            // If no fast switching, abort and close all other plugins.
            for (size_t i = 0; i < _inputs.size(); ++i) {
                if (i != index && _inStates[i] != INPUT_STOPPING && _inStates[i] != INPUT_STOPPED) {
                    stopPlugin(i, true);
                }
            }
        }
        // Make the plugin current.
        _curPlugin = _nextPlugin = index;
    }

    // If input is received on the current plugin (maybe after switching to primary input).
    if (index == _curPlugin) {
        // Restart the receive timeout.
        restartTimeout(_curPlugin);
        // Wake up output plugin if it is sleeping, waiting for packets to output.
        lock.signal();
    }

    // Return false when the application terminates.
    return !_terminate;
}


//----------------------------------------------------------------------------
// Report completion of input session (called by input plugins).
//----------------------------------------------------------------------------

bool ts::tsswitch::Core::inputStopped(size_t index, bool success)
{
    assert(index < _inputs.size());
    _log.debug(u"Core: plugin %d stopped", {index});

    bool stopRequest = false;

    // Locked sequence.
    {
        Guard lock(_mutex);
        _log.debug(u"Core: input %d completed, success: %s", {index, success});

        // If already stopped, do nothing. Must be a spurious call.
        if (_inStates[index] == INPUT_STOPPED) {
            return !_terminate;
        }

        // Update plugin states.
        _inStates[index] = INPUT_STOPPED;

        // Count end of cycle when the last plugin terminates.
        if (index == _inputs.size() - 1) {
            _curCycle++;
        }

        // Check if the complete processing is terminated.
        stopRequest = _opt.terminate || (_opt.cycleCount > 0 && _curCycle >= _opt.cycleCount);

        if (stopRequest) {
            // Do not trigger receive timeout while terminating.
            cancelTimeout();
        }
        else {
            // Not stopping, decide what to do depending on core state.
            switch (_state) {
                case CORE_STOPPED: {
                    // Already stopped, nothing to do.
                    break;
                }
                case CORE_RUNNING: {
                    // Core normally running, no switch in progress.
                    // If the current input is terminating, switch to next one.
                    if (index == _curPlugin) {
                        setInputLocked(nextInputIndex(index, UPWARD), false, UPWARD);
                    }
                    break;
                }
                case CORE_STARTING_NEXT: {
                    // We are in the middle of a switch operation but we do not expect to do anything on a plugin stop.
                    break;
                }
                case CORE_STOPPING_PREVIOUS: {
                    if (_opt._strategy == SEQUENTIAL_SWITCH && index == _curPlugin) {
                        // We are in the middle of a delayed switch operation.
                        // Now start the next plugin.
                        _state = CORE_STARTING_NEXT;
                        _curPlugin = _nextPlugin;
                        startPlugin(_curPlugin, true);
                    }
                    break;
                }
                default: {
                    _log.error(u"wrong switch core state %d when plugin %d stopped", {_state, index});
                    break;
                }
            }
        }
    }

    // Stop everything when we reach the end of the tsswitch processing.
    // This must be done outside the locked sequence to avoid deadlocks.
    if (stopRequest) {
        stop(true);
    }

    // Return false when the application terminates.
    return !_terminate;
}


//----------------------------------------------------------------------------
// Wait for completion of all plugins.
//----------------------------------------------------------------------------

void ts::tsswitch::Core::waitForTermination()
{
    // Wait for output termination.
    _output.waitForTermination();

    // Wait for all input termination.
    for (size_t i = 0; i < _inputs.size(); ++i) {
        _inputs[i]->waitForTermination();
    }
}
