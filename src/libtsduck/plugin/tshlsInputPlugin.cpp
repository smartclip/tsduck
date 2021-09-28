//----------------------------------------------------------------------------
//
// TSDuck - The MPEG Transport Stream Toolkit
// Copyright (c) 2005-2021, Thierry Lelegard
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

#include "tshlsInputPlugin.h"
#include "tsPluginRepository.h"
#include "tsFileUtils.h"
TSDUCK_SOURCE;

#if !defined(TS_UNIX) || !defined(TS_NO_CURL)
TS_REGISTER_INPUT_PLUGIN(u"hls", ts::hls::InputPlugin);
#endif

// A dummy storage value to force inclusion of this module when using the static library.
const int ts::hls::InputPlugin::REFERENCE = 0;


//----------------------------------------------------------------------------
// Input constructor
//----------------------------------------------------------------------------

ts::hls::InputPlugin::InputPlugin(TSP* tsp_) :
    AbstractHTTPInputPlugin(tsp_, u"Receive HTTP Live Streaming (HLS) media", u"[options] url"),
    _url(),
    _minRate(0),
    _maxRate(0),
    _minWidth(0),
    _maxWidth(0),
    _minHeight(0),
    _maxHeight(0),
    _startSegment(0),
    _listVariants(false),
    _lowestRate(false),
    _highestRate(false),
    _lowestRes(false),
    _highestRes(false),
    _maxSegmentCount(0),
    _segmentCount(0),
    _playlist()
{
    option(u"", 0, STRING, 1, 1);
    help(u"",
         u"Specify the URL of an HLS manifest or playlist. "
         u"This is typically an URL ending in .m3u8. "
         u"The playlist can be either a master one, referencing several versions "
         u"of the same content (with various bitrates or resolutions). "
         u"The playlist can also be a media playlist, referencing all segments "
         u"of one single content.");

    option(u"lowest-bitrate");
    help(u"lowest-bitrate",
         u"When the URL is a master playlist, use the content with the lowest bitrate.");

    option(u"highest-bitrate");
    help(u"highest-bitrate",
         u"When the URL is a master playlist, use the content with the highest bitrate.");

    option(u"lowest-resolution");
    help(u"lowest-resolution",
         u"When the URL is a master playlist, use the content with the lowest screen resolution.");

    option(u"highest-resolution");
    help(u"highest-resolution",
         u"When the URL is a master playlist, use the content with the highest screen resolution.");

    option(u"list-variants", 'l');
    help(u"list-variants",
         u"When the URL is a master playlist, list all possible streams bitrates and resolutions.");

    option<BitRate>(u"min-bitrate");
    help(u"min-bitrate",
         u"When the URL is a master playlist, select a content the bitrate of which is higher "
         u"than the specified minimum.");

    option<BitRate>(u"max-bitrate");
    help(u"max-bitrate",
         u"When the URL is a master playlist, select a content the bitrate of which is lower "
         u"than the specified maximum.");

    option(u"min-width", 0, UINT32);
    help(u"min-width",
         u"When the URL is a master playlist, select a content the resolution of which has a "
         u"higher width than the specified minimum.");

    option(u"max-width", 0, UINT32);
    help(u"max-width",
         u"When the URL is a master playlist, select a content the resolution of which has a "
         u"lower width than the specified maximum.");

    option(u"min-height", 0, UINT32);
    help(u"min-height",
         u"When the URL is a master playlist, select a content the resolution of which has a "
         u"higher height than the specified minimum.");

    option(u"max-height", 0, UINT32);
    help(u"max-height",
         u"When the URL is a master playlist, select a content the resolution of which has a "
         u"lower height than the specified maximum.");

    option(u"save-files", 0, STRING);
    help(u"save-files", u"directory-name",
         u"Specify a directory where all downloaded files, media segments and playlists, are saved "
         u"before being passed to the next plugin. "
         u"This is typically a debug option to analyze the input HLS structure.");

    option(u"segment-count", 's', POSITIVE);
    help(u"segment-count",
         u"Stop receiving the HLS stream after receiving the specified number of media segments. "
         u"By default, receive the complete content.");

    option(u"live");
    help(u"live",
         u"Specify that the input is a live stream and the playout shall start at the last segment in the playlist.\n"
         u"This is an alias for --start-segment -1.");

    option(u"start-segment", 0, INT32);
    help(u"start-segment",
         u"Start at the specified segment in the initial playlist. "
         u"By default, start with the first media segment.\n\n"
         u"The value can be positive or negative. "
         u"Positive values are indexes from the start of the playlist: "
         u"0 is the first segment (the default), +1 is the second segment, etc. "
         u"Negative values are indexes from the end of the playlist: "
         u"-1 is the last segment, -2 is the preceding segment, etc.");
}


//----------------------------------------------------------------------------
// Simple virtual methods.
//----------------------------------------------------------------------------

bool ts::hls::InputPlugin::isRealTime()
{
    return true;
}


//----------------------------------------------------------------------------
// Input command line options method
//----------------------------------------------------------------------------

bool ts::hls::InputPlugin::getOptions()
{
    _url.setURL(value(u""));
    const UString saveDirectory(value(u"save-files"));
    getIntValue(_maxSegmentCount, u"segment-count");
    getValue(_minRate, u"min-bitrate");
    getValue(_maxRate, u"max-bitrate");
    getIntValue(_minWidth, u"min-width");
    getIntValue(_maxWidth, u"max-width");
    getIntValue(_minHeight, u"min-height");
    getIntValue(_maxHeight, u"max-height");
    getIntValue(_startSegment, u"start-segment");
    _lowestRate = present(u"lowest-bitrate");
    _highestRate = present(u"highest-bitrate");
    _lowestRes = present(u"lowest-resolution");
    _highestRes = present(u"highest-resolution");
    _listVariants = present(u"list-variants");

    // Invoke superclass to initialize webArgs.
    AbstractHTTPInputPlugin::getOptions();

    // Enable authentication tokens from master playlist to media playlist
    // and from media playlists to media segments.
    webArgs.useCookies = true;
    webArgs.cookiesFile = TempFile(u".cookies");

    if (present(u"live")) {
        // With live streams, start at the last segment.
        if (_startSegment != 0) {
            tsp->error(u"--live and --start-segment are mutually exclusive");
            return false;
        }
        _startSegment = -1;
    }

    if (!_url.isValid()) {
        tsp->error(u"invalid URL");
        return false;
    }

    // Check consistency of selection options.
    const int singleSelect = _lowestRate + _highestRate + _lowestRes + _highestRes;
    const int multiSelect = (_minRate > 0) + (_maxRate > 0) + (_minWidth > 0) + (_maxWidth > 0) + (_minHeight > 0) + (_maxHeight > 0);
    if (singleSelect > 1) {
        tsp->error(u"specify only one of --lowest-bitrate, --highest-bitrate, --lowest-resolution, --highest-resolution");
        return false;
    }
    if (singleSelect > 0 && multiSelect > 0) {
        tsp->error(u"incompatible combination of stream selection options");
        return false;
    }

    // Automatically save media segments and playlists.
    setAutoSaveDirectory(saveDirectory);
    _playlist.setAutoSaveDirectory(saveDirectory);

    return true;
}


//----------------------------------------------------------------------------
// Input start method
//----------------------------------------------------------------------------

bool ts::hls::InputPlugin::start()
{
    // Load the HLS playlist, can be a master playlist or a media playlist.
    _playlist.clear();
    if (!_playlist.loadURL(_url.toString(), false, webArgs, hls::UNKNOWN_PLAYLIST, *tsp)) {
        return false;
    }

    // In the case of a master play list, select one media playlist.
    if (_playlist.type() == hls::MASTER_PLAYLIST) {
        tsp->verbose(u"downloaded %s", {_playlist});

        // Get a copy of the master playlist. The media playlist will be loaded in _playlist.
        PlayList master(_playlist);

        // List all variants when requested.
        if (_listVariants) {
            for (size_t i = 0; i < master.playListCount(); ++i) {
                tsp->info(master.playList(i).toString());
            }
        }

        // Apply command line selection criteria.
        // Loop until one media playlist is loaded (skip missing playlists).
        for (;;) {
            size_t index = 0;
            if (_lowestRate) {
                index = master.selectPlayListLowestBitRate();
            }
            else if (_highestRate) {
                index = master.selectPlayListHighestBitRate();
            }
            else if (_lowestRes) {
                index = master.selectPlayListLowestResolution();
            }
            else if (_highestRes) {
                index = master.selectPlayListHighestResolution();
            }
            else {
                index = master.selectPlayList(_minRate, _maxRate, _minWidth, _maxWidth, _minHeight, _maxHeight);
            }
            if (index == NPOS) {
                tsp->error(u"could not find a matching stream in master playlist");
                return false;
            }
            assert(index < master.playListCount());
            tsp->verbose(u"selected playlist: %s", {master.playList(index)});
            const UString nextURL(master.playList(index).urlString());

            // Download selected media playlist.
            _playlist.clear();
            if (_playlist.loadURL(nextURL, false, webArgs, hls::UNKNOWN_PLAYLIST, *tsp)) {
                break; // media playlist loaded
            }
            else if (master.playListCount() == 1) {
                tsp->error(u"no more media playlist to try, giving up");
                return false;
            }
            else {
                // Remove the failing playlist and retry playlist selection.
                master.deletePlayList(index);
            }
        }
    }

    // Now, we must have a media playlist.
    if (_playlist.type() != hls::MEDIA_PLAYLIST) {
        tsp->error(u"invalid HLS playlist type, expected a media playlist");
        return false;
    }
    tsp->verbose(u"downloaded %s", {_playlist});

    // Manage the number of media segments and starting point.
    size_t segCount = _playlist.segmentCount();
    if (segCount == 0) {
        tsp->error(u"empty HLS media playlist");
        return false;
    }
    else if (_startSegment > 0) {
        // Start index from the start of playlist.
        if (segCount + 1 < size_t(_startSegment)) {
            tsp->warning(u"playlist has only %d segments, starting at last one", {segCount});
            segCount = 1;
        }
        else {
            // Remaining number of segments to play.
            segCount = segCount - size_t(_startSegment);
        }
    }
    else if (_startSegment < 0) {
        // Start index from the end of playlist.
        if (segCount < size_t(- _startSegment)) {
            tsp->warning(u"playlist has only %d segments, starting at first one", {segCount});
        }
        else {
            // Remaining number of segments to play.
            segCount = size_t(- _startSegment);
        }
    }

    // If the start point is not the first segment, then drop unused initial segments.
    while (_playlist.segmentCount() > segCount) {
        _playlist.popFirstSegment();
        tsp->debug(u"dropped initial segment, %d remaining segments", {_playlist.segmentCount()});
    }

    _segmentCount = 0;

    // Invoke superclass.
    return AbstractHTTPInputPlugin::start();
}


//----------------------------------------------------------------------------
// Called by AbstractHTTPInputPlugin to open an URL.
//----------------------------------------------------------------------------

bool ts::hls::InputPlugin::openURL(WebRequest& request)
{
    // Check if the playlist is completed
    bool completed =
        // the playlist is originally empty
        (_segmentCount == 0 && _playlist.segmentCount() == 0) ||
        // reached maximum number of segments
        (_maxSegmentCount > 0 && _segmentCount >= _maxSegmentCount) ||
        // user interruption
        tsp->aborting();

    // If there is only one or zero remaining segment, try to reload the playlist.
    if (!completed && _playlist.segmentCount() < 2 && _playlist.updatable()) {

        // Reload the playlist, ignore errors, continue to play next segments.
        _playlist.reload(false, webArgs, *tsp);

        // If the playlist is still empty, this means that we have read all segments before the server
        // could produce new segments. For live streams, this is possible because new segments
        // can be produced as late as the estimated end time of the previous playlist. So, we retry
        // at regular intervals until we get new segments.

        while (_playlist.segmentCount() == 0 && Time::CurrentUTC() <= _playlist.terminationUTC() && !tsp->aborting()) {
            // The wait between two retries is half the target duration of a segment, with a minimum of 2 seconds.
            SleepThread(std::max<MilliSecond>(2000, (MilliSecPerSec * _playlist.targetDuration()) / 2));
            // This time, we stop on reload error.
            if (!_playlist.reload(false, webArgs, *tsp)) {
                break;
            }
        }

        // End of playlist if we cannot find new segments.
        completed = _playlist.segmentCount() == 0;
    }

    if (completed) {
        tsp->verbose(u"HLS playlist completed");
        return false;
    }

    // Remove first segment from the playlist.
    hls::MediaSegment seg;
    _playlist.popFirstSegment(seg);
    _segmentCount++;

    // Open the segment.
    tsp->debug(u"downloading segment %s", {seg.urlString()});
    request.enableCookies(webArgs.cookiesFile);
    return request.open(seg.urlString());
}
