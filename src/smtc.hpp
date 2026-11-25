#pragma once

#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Media.Control.h>

#include <algorithm>
#include <chrono>
#include <string>

#include "tracker.hpp"

namespace euilscrr {

class SmtcReader {
 public:
  SmtcSample poll() {
    using namespace winrt::Windows::Media::Control;
    SmtcSample sample;

    try {
      if (!manager_) {
        manager_ = GlobalSystemMediaTransportControlsSessionManager::RequestAsync().get();
      }
      if (!manager_) return sample;

      GlobalSystemMediaTransportControlsSession spotify{nullptr};
      for (const auto& s : manager_.GetSessions()) {
        std::string app_id = ascii_lower(winrt::to_string(s.SourceAppUserModelId()));
        if (app_id.find("spotify") != std::string::npos) {
          spotify = s;
          if (s.GetPlaybackInfo().PlaybackStatus() ==
              GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing) {
            break;
          }
        }
      }

      if (!spotify) return sample;

      auto info = spotify.GetPlaybackInfo();
      auto timeline = spotify.GetTimelineProperties();
      auto props = spotify.TryGetMediaPropertiesAsync().get();

      auto status = info.PlaybackStatus();
      if (status == GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing) {
        sample.status = "Playing";
      } else if (status == GlobalSystemMediaTransportControlsSessionPlaybackStatus::Paused) {
        sample.status = "Paused";
      } else {
        sample.status = "Idle";
      }

      if (props) {
        sample.title = winrt::to_string(props.Title());
        sample.artist = winrt::to_string(props.Artist());
        sample.album = winrt::to_string(props.AlbumTitle());
      }

      int64_t end_ms = std::max<int64_t>(
          0,
          std::chrono::duration_cast<std::chrono::milliseconds>(timeline.EndTime()).count());
      int64_t pos_ms = std::max<int64_t>(
          0,
          std::chrono::duration_cast<std::chrono::milliseconds>(timeline.Position()).count());

      if (sample.status == "Playing") {
        auto updated = timeline.LastUpdatedTime();
        if (updated.time_since_epoch().count() > 0) {
          int64_t drift_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                 winrt::clock::now() - updated)
                                 .count();
          if (drift_ms > 0 && drift_ms < 600'000) {
            pos_ms += drift_ms;
          }
        }
      }
      if (end_ms > 0 && pos_ms > end_ms) pos_ms = end_ms;

      if (end_ms > 0) sample.duration_ms = end_ms;
      sample.position_ms = pos_ms;
    } catch (...) {
      manager_ = nullptr;
      sample.status = "Idle";
    }

    return sample;
  }

 private:
  winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSessionManager manager_{nullptr};
};

}  // namespace euilscrr
