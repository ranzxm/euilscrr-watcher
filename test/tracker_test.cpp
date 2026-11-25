#include "../src/tracker.hpp"

#include <cassert>
#include <iostream>

using namespace euilscrr;

static void test_thresholds() {
  assert(compute_scrobble_threshold(200'000) == 100'000);
  assert(compute_scrobble_threshold(600'000) == 240'000);
  assert(compute_scrobble_threshold(std::nullopt) == 30'000);
  assert(compute_scrobble_threshold(20'000) == 30'000);
  assert(compute_scrobble_threshold(45'000) == 30'000);
}

static void test_normal_scrobble() {
  PlaybackTracker tracker;
  const int64_t base = 1'000'000;
  SmtcSample sample{"Multo", "Cup of Joe", "Silakbo", "Playing", 0, 200'000};

  auto first = tracker.update(sample, base);
  assert(first.has_now_playing && first.now_playing.has_value());
  assert(first.now_playing->title == "Multo");
  assert(!first.scrobble.has_value());

  int64_t t = base;
  for (int i = 1; i <= 19; ++i) {
    t += 5'000;
    sample.position_ms = i * 5'000;
    auto step = tracker.update(sample, t);
    assert(!step.scrobble.has_value());
  }

  t += 5'000;
  sample.position_ms = 100'000;
  auto hit = tracker.update(sample, t);
  assert(hit.scrobble.has_value());
  assert(hit.scrobble->title == "Multo");
  assert(hit.scrobble->played_at == base);

  t += 5'000;
  sample.position_ms = 105'000;
  assert(!tracker.update(sample, t).scrobble.has_value());
}

static void test_pause_seek_resume_and_idle() {
  PlaybackTracker tracker;
  SmtcSample sample{"About You", "The 1975", "", "Playing", 0, 80'000};

  tracker.update(sample, 0);
  sample.position_ms = 5'000;
  tracker.update(sample, 5'000);

  SmtcSample paused = sample;
  paused.status = "Paused";
  auto p1 = tracker.update(paused, 10'000);
  assert(p1.has_now_playing && p1.now_playing.has_value());
  assert(!p1.now_playing->is_playing && p1.now_playing->position_ms == 5'000 && p1.now_playing->started_at == 0);

  assert(!tracker.update(paused, 26'000).has_now_playing);

  paused.position_ms = 32'000;
  auto p2 = tracker.update(paused, 28'000);
  assert(p2.has_now_playing && p2.now_playing->position_ms == 32'000 && !p2.now_playing->is_playing);

  sample.position_ms = 32'000;
  auto resumed = tracker.update(sample, 30'000);
  assert(resumed.has_now_playing && resumed.now_playing->is_playing && resumed.now_playing->started_at == 0);
  assert(!resumed.scrobble.has_value());

  SmtcSample idle{};
  assert(!tracker.update(idle, 35'000).has_now_playing);
  auto cleared = tracker.update(idle, 50'000);
  assert(cleared.has_now_playing && !cleared.now_playing.has_value());
}

static void test_repeat_one() {
  PlaybackTracker tracker;
  SmtcSample sample{"Creep", "Radiohead", "", "Playing", 0, 60'000};

  int64_t t = 0;
  tracker.update(sample, t);
  for (int i = 1; i <= 11; ++i) {
    t += 5'000;
    sample.position_ms = i * 5'000;
    tracker.update(sample, t);
  }

  t += 5'000;
  sample.position_ms = 1'000;
  auto restarted = tracker.update(sample, t);
  assert(restarted.has_now_playing && restarted.now_playing->started_at == t);

  bool second_scrobble = false;
  for (int i = 1; i <= 6; ++i) {
    t += 5'000;
    sample.position_ms = i * 5'000;
    if (tracker.update(sample, t).scrobble.has_value()) {
      second_scrobble = true;
    }
  }
  assert(second_scrobble);
}

static void test_ad_filter() {
  PlaybackTracker tracker;
  SmtcSample ad{"Listen to music, ad-free.", "Spotify", "", "Playing", 1'000, 29'597};
  auto first = tracker.update(ad, 0);
  assert(!first.has_now_playing && !first.scrobble.has_value());
  ad.position_ms = 25'000;
  assert(!tracker.update(ad, 35'000).scrobble.has_value());
}

static void test_seek_resync_and_late_timeline() {
  PlaybackTracker tracker;
  SmtcSample sample{"RPG", "SEKAI NO OWARI", "", "Playing", 0, 290'000};
  assert(tracker.update(sample, 0).now_playing->position_ms == 0);
  sample.position_ms = 5'000;
  assert(!tracker.update(sample, 5'000).has_now_playing);

  sample.position_ms = 120'000;
  auto seeked = tracker.update(sample, 7'500);
  assert(seeked.has_now_playing && seeked.now_playing->position_ms == 120'000);

  PlaybackTracker late;
  SmtcSample no_tl{"Multo", "Cup of Joe", "", "Playing", std::nullopt, std::nullopt};
  auto a = late.update(no_tl, 0);
  assert(a.has_now_playing && !a.now_playing->duration_ms.has_value());
  no_tl.duration_ms = 238'000;
  no_tl.position_ms = 8'000;
  auto b = late.update(no_tl, 2'500);
  assert(b.has_now_playing && b.now_playing->duration_ms == 238'000 && b.now_playing->position_ms == 8'000);
}

int main() {
  test_thresholds();
  test_normal_scrobble();
  test_pause_seek_resume_and_idle();
  test_repeat_one();
  test_ad_filter();
  test_seek_resync_and_late_timeline();
  std::cout << "All 6 tracker test groups passed.\n";
  return 0;
}
