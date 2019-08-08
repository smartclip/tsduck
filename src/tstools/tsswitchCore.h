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
//!
//!  @file
//!  Input switch (tsswitch) core engine.
//!
//----------------------------------------------------------------------------

#pragma once
#include "tsswitchOptions.h"
#include "tsswitchInputExecutor.h"
#include "tsswitchOutputExecutor.h"
#include "tsMutex.h"
#include "tsCondition.h"
#include "tsWatchDog.h"

namespace ts {
    //!
    //! Input switch (tsswitch) namespace.
    //!
    namespace tsswitch {
        //!
        //! Input switch (tsswitch) core engine.
        //! @ingroup plugin
        //!
        class Core: private WatchDogHandlerInterface
        {
            TS_NOBUILD_NOCOPY(Core);
        public:
            //!
            //! Constructor.
            //! @param [in,out] opt Command line options.
            //! @param [in,out] log Log report.
            //!
            Core(Options& opt, Report& log);

            //!
            //! Destructor.
            //!
            ~Core();

            //========= COMMANDS
            // The following methods are commands to control tsswitch.
            // They are called either by the tsswitch main code of the remote command control.

            //!
            //! Start the @c tsswitch processing.
            //! @return True on success, false on error.
            //!
            bool start();

            //!
            //! Stop the @c tsswitch processing.
            //! @param [in] success False if the stop is triggered by an error.
            //!
            void stop(bool success);

            //!
            //! Wait for completion of all plugin threads.
            //!
            void waitForTermination();

            //!
            //! Switch to another input plugin.
            //! @param [in] pluginIndex Index of the new input plugin.
            //!
            void setInput(size_t pluginIndex);

            //!
            //! Switch to the next input plugin.
            //!
            void nextInput();

            //!
            //! Switch to the previous input plugin.
            //!
            void previousInput();

            //========= EVENTS
            // The following methods are called when input events occur.
            // They are called either by the input plugins from their respective threads.
            // The private methods handleWatchDogTimeout() is also an event trigger.

            //!
            //! Called by an input plugin when it started an input session.
            //! @param [in] pluginIndex Index of the input plugin.
            //! @param [in] success True if the start operation succeeded.
            //! @return False when @c tsswitch is terminating.
            //!
            bool inputStarted(size_t pluginIndex, bool success);

            //!
            //! Called by an input plugin when it received input packets.
            //! @param [in] pluginIndex Index of the input plugin.
            //! @return False when @c tsswitch is terminating.
            //!
            bool inputReceived(size_t pluginIndex);

            //!
            //! Called by an input plugin when it stopped an input session.
            //! @param [in] pluginIndex Index of the input plugin.
            //! @param [in] success True if the stop operation succeeded.
            //! @return False when @c tsswitch is terminating.
            //!
            bool inputStopped(size_t pluginIndex, bool success);

            //========= BUFFER MANAGEMENT
            // The following methods are called by the output plugin to get access to
            // the current input buffer.

            //!
            //! Called by the output plugin when it needs some packets to output.
            //! Wait until there is some packets to output.
            //! @param [out] pluginIndex Returned index of the input plugin.
            //! @param [out] first Returned address of first packet to output.
            //! @param [out] data Returned address of metadata for the first packet to output.
            //! @param [out] count Returned number of packets to output.
            //! Never zero, except when @c tsswitch is terminating.
            //! @return False when @c tsswitch is terminating.
            //!
            bool getOutputArea(size_t& pluginIndex, TSPacket*& first, TSPacketMetadata*& data, size_t& count);

            //!
            //! Called by the output plugin after sending packets.
            //! @param [in] pluginIndex Index of the input plugin from which the packets were sent.
            //! @param [in] count Number of output packets to release.
            //! @return False when @c tsswitch is terminating.
            //!
            bool outputSent(size_t pluginIndex, size_t count);

        private:
            // State of an input plugin.
            enum InputState {INPUT_STARTING, INPUT_RUNNING, INPUT_STOPPING, INPUT_STOPPED};
            typedef std::vector<InputState> InputStateVector;

            // Input switching direction.
            enum Direction {DOWNWARD, UNCHANGED, UPWARD};

            // State of the Core object.
            enum CoreState {CORE_STOPPED, CORE_STARTING_NEXT, CORE_RUNNING, CORE_STOPPING_PREVIOUS};

            // Core private members.
            Options&            _opt;             // Command line options.
            Report&             _log;             // Asynchronous log report.
            InputExecutorVector _inputs;          // Input plugins threads.
            OutputExecutor      _output;          // Output plugin thread.
            WatchDog            _watchDog;        // Handle reception timeout.
            Mutex               _mutex;           // Global mutex, protect access to all subsequent fields.
            Condition           _gotInput;        // Signaled each time an input plugin reports new packets.
            CoreState           _state;           // State of the tsswitch::Core object.
            size_t              _curPlugin;       // Index of current input plugin.
            size_t              _nextPlugin;      // Next plugin during switching phase, same as _curPlugin otherwise.
            size_t              _timeoutPlugin;   // Plugin on which the current timeout applies.
            size_t              _curCycle;        // Current input cycle number.
            volatile bool       _terminate;       // Terminate complete processing.
            InputStateVector    _inStates;        // States of input plugins.

            // Get next input plugin index, either upward or downward.
            size_t nextInputIndex(size_t index, Direction dir) const;

            // Cancel current timeout. Must be called with mutex held.
            void cancelTimeout();

            // Restart reception timeout on a specific plugin. Must be called with mutex held.
            void restartTimeout(size_t index);

            // Start an input session on a plugin thread. Must be called with mutex held.
            void startPlugin(size_t index, bool flowControl);

            // Stop an input session on a plugin thread. Must be called with mutex held.
            void stopPlugin(size_t index, bool abortInput);

            // Change input plugin with mutex already held.
            // In case of error, try next input plugin upward or downward.
            void setInputLocked(size_t index, bool abortCurrent, Direction dir);

            // Implementation of WatchDogHandlerInterface
            virtual void handleWatchDogTimeout(WatchDog& watchdog) override;
        };
    }
}
