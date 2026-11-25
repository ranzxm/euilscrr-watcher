#pragma once

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string>

namespace euilscrr {

struct SmtcSample {
  std::string title;
  std::string artist;
  std::string album;
  std::string status = "Idle";
  std::optional<int64_t> position_ms;
  std::optional<int64_t> duration_ms;
};

struct TrackSnapshot {
  std::string title;
  std::string artist;
  std::string album;
  std::optional<int64_t> duration_ms;
};

struct NowPlayingSnapshot {
  std::string title;
  std::string artist;
  std::string album;
  std::optional<int64_t> duration_ms;
  std::optional<int64_t> position_ms;
  int64_t started_at = 0;
  bool is_playing = false;
};

struct ScrobbleEvent {
  std::string title;
  std::string artist;
  std::string album;
  std::optional<int64_t> duration_ms;
  int64_t played_at = 0;
};

struct TrackerAction {
  bool has_now_playing = false;
  std::optional<NowPlayingSnapshot> now_playing;
  std::optional<ScrobbleEvent> scrobble;
};

inline constexpr int64_t MIN_SCROBBLE_MS = 30'000;
inline constexpr int64_t MAX_SCROBBLE_MS = 240'000;
inline constexpr int64_t MAX_TICK_MS = 10'000;
inline constexpr int64_t IDLE_CLEAR_MS = 15'000;
inline constexpr int64_t SEEK_RESYNC_MS = 1'500;
inline constexpr int64_t HEARTBEAT_MS = 5 * 60'000;
inline constexpr int64_t REPEAT_WINDOW_MS = 5'000;

inline std::string trim_copy(const std::string& input) {
  size_t start = 0;
  while (start < input.size() && std::isspace(static_cast<unsigned char>(input[start]))) {
    ++start;
  }
  size_t end = input.size();
  while (end > start && std::isspace(static_cast<unsigned char>(input[end - 1]))) {
    --end;
  }
  return input.substr(start, end - start);
}

inline std::string ascii_lower(std::string s) {
  for (char& ch : s) {
    unsigned char u = static_cast<unsigned char>(ch);
    if (u >= 'A' && u <= 'Z') ch = static_cast<char>(u + ('a' - 'A'));
  }
  return s;
}

inline int64_t compute_scrobble_threshold(std::optional<int64_t> duration_ms) {
  if (!duration_ms || *duration_ms < MIN_SCROBBLE_MS) return MIN_SCROBBLE_MS;
  int64_t half = *duration_ms / 2;
  return std::min(std::max(half, MIN_SCROBBLE_MS), MAX_SCROBBLE_MS);
}

inline std::string track_key(const std::string& artist, const std::string& title) {
  return ascii_lower(trim_copy(artist)) + "::" + ascii_lower(trim_copy(title));
}

inline bool is_spotify_ad(const std::string& artist, const std::string& title, const std::string& album) {
  const std::string a = ascii_lower(trim_copy(artist));
  const std::string t = ascii_lower(trim_copy(title));
  if (t == "advertisement" || a == "advertisement") return true;
  if (a == "spotify" && trim_copy(album).empty()) return true;
  return false;
}

class PlaybackTracker {
 public:
  TrackerAction update(const SmtcSample& sample, int64_t now_ms) {
    TrackerAction action;
    const std::string title = trim_copy(sample.title);
    const std::string artist = trim_copy(sample.artist);
    const std::string album = trim_copy(sample.album);

    std::optional<std::string> key;
    if (!title.empty() && !artist.empty() && !is_spotify_ad(artist, title, album)) {
      key = track_key(artist, title);
    }

    const bool is_playing = sample.status == "Playing";
    const bool is_paused =
        sample.status == "Paused" && key.has_value() && current_key_ == key && synced_.has_value();

    if (!key.has_value() || (!is_playing && !is_paused)) {
      last_playing_at_.reset();
      if (synced_.has_value()) {
        if (!inactive_since_.has_value()) {
          inactive_since_ = now_ms;
        } else if (now_ms - *inactive_since_ >= IDLE_CLEAR_MS) {
          synced_.reset();
          inactive_since_.reset();
          action.has_now_playing = true;
          action.now_playing.reset();
        }
      }
      return action;
    }

    inactive_since_.reset();
    std::optional<int64_t> duration_ms;
    if (sample.duration_ms.has_value() && *sample.duration_ms > 0) {
      duration_ms = *sample.duration_ms;
    }
    std::optional<int64_t> position_ms;
    if (sample.position_ms.has_value() && *sample.position_ms >= 0) {
      position_ms = *sample.position_ms;
    }

    const std::optional<int64_t> known_duration =
        duration_ms.has_value() ? duration_ms
                                : (current_track_.has_value() ? current_track_->duration_ms : std::nullopt);

    const bool restarted_same_track =
        is_playing &&
        current_key_ == key &&
        last_playing_at_.has_value() &&
        known_duration.has_value() &&
        last_position_ms_.has_value() &&
        *last_position_ms_ >= std::max(*known_duration / 2, *known_duration - REPEAT_WINDOW_MS) &&
        position_ms.has_value() &&
        *position_ms < REPEAT_WINDOW_MS &&
        *position_ms < *last_position_ms_;

    const bool new_play = !current_track_.has_value() || current_key_ != key || restarted_same_track;
    if (new_play) {
      current_key_ = *key;
      current_track_ = TrackSnapshot{title, artist, album, duration_ms};
      started_at_ = now_ms;
      elapsed_ms_ = 0;
      last_playing_at_.reset();
      scrobbled_ = false;
    }

    TrackSnapshot& track = *current_track_;
    if (track.album.empty() && !album.empty()) track.album = album;
    if (duration_ms.has_value()) track.duration_ms = duration_ms;

    std::optional<int64_t> expected_position_ms;
    if (synced_.has_value() && synced_->position_ms.has_value()) {
      int64_t projected = *synced_->position_ms + (synced_->is_playing ? (now_ms - synced_at_) : 0);
      if (track.duration_ms.has_value()) {
        projected = std::min(*track.duration_ms, projected);
      }
      expected_position_ms = projected;
    }

    const bool seeked =
        position_ms.has_value() &&
        (!expected_position_ms.has_value() ||
         std::llabs(*position_ms - *expected_position_ms) >= SEEK_RESYNC_MS);

    if (new_play ||
        !synced_.has_value() ||
        is_playing != synced_->is_playing ||
        seeked ||
        track.duration_ms != synced_->duration_ms ||
        (is_playing && (!last_playing_at_.has_value() || now_ms - synced_at_ >= HEARTBEAT_MS))) {
      synced_ = NowPlayingSnapshot{
          track.title,
          track.artist,
          track.album,
          track.duration_ms,
          position_ms.has_value() ? position_ms : (new_play ? std::nullopt : expected_position_ms),
          started_at_,
          is_playing,
      };
      synced_at_ = now_ms;
      action.has_now_playing = true;
      action.now_playing = synced_;
    }

    if (is_playing && last_playing_at_.has_value()) {
      int64_t delta = std::clamp<int64_t>(now_ms - *last_playing_at_, 0, MAX_TICK_MS);
      elapsed_ms_ += delta;
    }
    last_playing_at_ = is_playing ? std::optional<int64_t>(now_ms) : std::nullopt;
    last_position_ms_ = position_ms;

    if (is_playing && !scrobbled_) {
      const int64_t threshold = compute_scrobble_threshold(track.duration_ms);
      if (elapsed_ms_ >= threshold) {
        scrobbled_ = true;
        action.scrobble = ScrobbleEvent{
            track.title,
            track.artist,
            track.album,
            track.duration_ms,
            started_at_,
        };
      }
    }

    return action;
  }

  bool is_scrobbled() const { return scrobbled_; }
  const std::optional<NowPlayingSnapshot>& synced() const { return synced_; }

  void force_resync() {
    synced_.reset();
    inactive_since_.reset();
  }

 private:
  std::optional<std::string> current_key_;
  std::optional<TrackSnapshot> current_track_;
  int64_t started_at_ = 0;
  int64_t elapsed_ms_ = 0;
  std::optional<int64_t> last_playing_at_;
  std::optional<int64_t> last_position_ms_;
  std::optional<NowPlayingSnapshot> synced_;
  int64_t synced_at_ = 0;
  bool scrobbled_ = false;
  std::optional<int64_t> inactive_since_;
};

}  // namespace euilscrr
