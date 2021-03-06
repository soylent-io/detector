/*
 * Copyright © 2019 Tyler J. Brooks <tylerjbrooks@digispeaker.com> <https://www.digispeaker.com>
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * <http://www.apache.org/licenses/LICENSE-2.0>
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * Try './detector -h' for usage.
 */

#include <chrono>
#include <vector>
#include <cstring>
#include <iostream>
#include <fstream>
#include <string>
#include <sstream>
#include <algorithm>
#include <iterator>
#include <limits>

#include "tracker.h"
#include "third_party/Hungarian/Hungarian.h"

namespace detector {

const Eigen::Matrix<double, 6, 6> Tracker::Track::A_{
  { 1, 0, 1, 0, 0, 0 },
  { 0, 1, 0, 1, 0, 0 },
  { 0, 0, 1, 0, 1, 0 },
  { 0, 0, 0, 1, 0, 1 },
  { 0, 0, 0, 0, 0, 0 },
  { 0, 0, 0, 0, 0, 0 }
};
const Eigen::Matrix<double, 2, 6> Tracker::Track::H_{
  { 1, 0, 0, 0, 0, 0 },
  { 0, 1, 0, 0, 0, 0 }
};

Tracker::Track::Track(unsigned int track_id, const BoxBuf& box)
  : id(track_id), type(box.type),
    x(box.x), y(box.y), w(box.w), h(box.h),
    touched(true) {

  stamp = std::chrono::steady_clock::now();
  state_ = Tracker::Track::State::kInit;

  // initialize state vector with inital position
  double mid_x = x + w / 2.0;
  double mid_y = y + h / 2.0;
  X_ << mid_x,mid_y,0,0,0,0;

  // initialize error covariance matrix
  P_ << initial_error_,0,0,0,0,0,
        0,initial_error_,0,0,0,0,
        0,0,initial_error_,0,0,0,
        0,0,0,initial_error_,0,0,
        0,0,0,0,initial_error_,0,
        0,0,0,0,0,initial_error_;

  // initialize measurement covariance matrix
  R_ << measure_variance_,0,
        0,measure_variance_;

  // initialize process covariance matrix
  Q_ << process_variance_,0,0,0,0,0,
        0,process_variance_,0,0,0,0,
        0,0,process_variance_,0,0,0,
        0,0,0,process_variance_,0,0,
        0,0,0,0,process_variance_,0,
        0,0,0,0,0,process_variance_;
}

void Tracker::Track::updateTime() {

  touched = true;

  //  predict state transition
  X_ = A_ * X_;

  //  update error matrix
  P_ = A_ * (P_ * A_.transpose()) + Q_;
}

void Tracker::Track::updateMeasure() {
  //  compute kalman gain
  auto K_ = P_ * H_.transpose() * (H_ * P_ * H_.transpose() + R_).inverse();

  //  fuse new measurement
  X_ = X_ + K_ * (Z_ - H_ * X_);

  //  update error matrix
  P_ = (Eigen::Matrix<double,6,6>::Identity() - K_ * H_) * P_;
}

double Tracker::Track::getDistance(double mid_x, double mid_y) {
  return std::sqrt(std::pow(mid_x - X_(0), 2) + std::pow(mid_y - X_(1), 2));
}

void Tracker::Track::addTarget(const BoxBuf& box) {

  stamp = std::chrono::steady_clock::now();
  x = box.x;
  y = box.y;
  w = box.w;
  h = box.h;
  double mid_x = x + w / 2.0;
  double mid_y = y + h / 2.0;

  if (state_ == Tracker::Track::State::kInit) {
    X_(2) = (mid_x - X_(0));
    X_(3) = (mid_y - X_(1));
  }
  updateTime();

  state_ = Tracker::Track::State::kActive;

  Z_ << mid_x, mid_y;
  updateMeasure();
}


Tracker::Tracker(unsigned int yield_time) 
  : Base(yield_time) {
}

Tracker::~Tracker() {
}

std::unique_ptr<Tracker> Tracker::create(
    unsigned int yield_time, bool quiet, 
    Encoder* enc, double max_dist, unsigned int max_time) {
  auto obj = std::unique_ptr<Tracker>(new Tracker(yield_time));
  obj->init(quiet, enc, max_dist, max_time);
  return obj;
}

bool Tracker::init(bool quiet, Encoder* enc, double max_dist, unsigned int max_time) {

  quiet_ = quiet;
  enc_ = enc;
  max_dist_ = max_dist;
  max_time_ = max_time;

  track_cnt_ = 0;

  tracker_on_ = false;
  
  return true; 
}

bool Tracker::addMessage(std::shared_ptr<std::vector<BoxBuf>>& boxes) {

  std::unique_lock<std::timed_mutex> lck(targets_lock_, std::defer_lock);

  if (!lck.try_lock_for(std::chrono::microseconds(
          Listener<std::shared_ptr<std::vector<BoxBuf>>>::timeout_))) {
    dbgMsg("encoder target lock busy\n");
    return false;
  }

  // only copy targets types we are tracking
  targets_.resize(boxes->size());
  std::copy_if(boxes->begin(), boxes->end(), targets_.begin(),
      [&](const BoxBuf& box) {
        return target_types_.find(box.type) != target_types_.end();
      });

  return true;
}

bool Tracker::waitingToRun() {

  if (!tracker_on_) {

    differ_tot_.begin();
    tracker_on_ = true;
  }

  return true;
}

bool Tracker::untouchTracks() {

  differ_untouch_.begin();

  std::for_each(tracks_.begin(), tracks_.end(), 
      [](Tracker::Track& track) {
        track.touched = false;
        });

  differ_untouch_.end();

  return true;
}

bool Tracker::associateTracks() {

  if (tracks_.size() && targets_.size()) {

    differ_associate_.begin();

    // compute cost matrix
    std::vector<std::vector<double>> mat(tracks_.size(),
        std::vector<double>(targets_.size(), 1.0e7));
    for (unsigned int k = 0; k < targets_.size(); k++) {
      double mid_x = targets_[k].x + targets_[k].w / 2.0;
      double mid_y = targets_[k].y + targets_[k].h / 2.0;
      for (unsigned int i = 0; i < tracks_.size(); i++) {
        if (tracks_[i].type == targets_[k].type) {
          mat[i][k] = tracks_[i].getDistance(mid_x, mid_y);
        }
      }
    }

    // assign targets to tracks
    HungarianAlgorithm hung_algo;
    vector<int> assignments;
    hung_algo.Solve(mat, assignments);

    // add targets to tracks
    for (unsigned int i = 0; i < assignments.size(); i++) {
      int k = assignments[i];
      double mid_x = targets_[k].x + targets_[k].w / 2.0;
      double mid_y = targets_[k].y + targets_[k].h / 2.0;
      if (tracks_[i].getDistance(mid_x, mid_y) <= max_dist_) {
        tracks_[i].addTarget(targets_[k]);
        targets_[k].id = std::numeric_limits<unsigned int>::max();
      }
    }

    // remove used targets
    targets_.erase(
        std::remove_if(targets_.begin(), targets_.end(),
          [&] (const BoxBuf& b) {
            return b.id == std::numeric_limits<unsigned int>::max();
          }), 
        targets_.end());

    differ_associate_.end();
  }

  return true;
}

bool Tracker::createNewTracks() {

  differ_create_.begin();

  if (targets_.size()) {
    std::for_each(targets_.begin(), targets_.end(),
        [&](const BoxBuf& b) {
          tracks_.push_back(Tracker::Track(++track_cnt_, b));
        });
  }

  targets_.resize(0);

  differ_create_.end();

  return true;
}

bool Tracker::touchTracks() {
  differ_touch_.begin();

  std::for_each(tracks_.begin(), tracks_.end(),
      [&](Tracker::Track& track) {
        if (!track.touched) {
          track.updateTime();
        }
      });

  differ_touch_.end();
  return true;
}

bool Tracker::cleanupTracks() {

  differ_cleanup_.begin();

  auto now = std::chrono::steady_clock::now();

  // remove old tracks
  tracks_.erase(
      std::remove_if(tracks_.begin(), tracks_.end(),
        [&] (const Tracker::Track& t) {
          using namespace std::chrono;
          duration<unsigned int,std::milli> span = 
            duration_cast<duration<unsigned int,std::milli>>(now - t.stamp);
          return max_time_ < span.count();
        }), 
      tracks_.end());

  differ_cleanup_.end();

  return true;
}

bool Tracker::postTracks() {

  differ_post_.begin();

  auto tracks = std::make_shared<std::vector<TrackBuf>>();

  std::for_each(tracks_.begin(), tracks_.end(),
      [&](const Tracker::Track& t) {
        tracks->push_back(TrackBuf(
              t.type, t.id,
              round(t.x), round(t.y), round(t.w), round(t.h)));
      });

  if (enc_) {
    if (!enc_->addMessage(tracks)) {
      dbgMsg("encoder busy");
    }
  }

  differ_post_.end();
  return true;
}

bool Tracker::running() {

  if (tracker_on_) {

    std::unique_lock<std::timed_mutex> lck(targets_lock_);

    if (targets_.size() != 0) {
      untouchTracks();
      associateTracks();
      createNewTracks();
      touchTracks();
    }

    cleanupTracks();
    postTracks();
  }

  return true;
}

bool Tracker::paused() {
  return true;
}

bool Tracker::waitingToHalt() {

  if (tracker_on_) {
    differ_tot_.end();
    tracker_on_ = false;

    if (!quiet_) {
      fprintf(stderr, "\nTracker Results...\n");
      fprintf(stderr, "      target untouch time (us): high:%u avg:%u low:%u cnt:%u\n", 
          differ_untouch_.high, differ_untouch_.avg, 
          differ_untouch_.low,  differ_untouch_.cnt);
      fprintf(stderr, "  target association time (us): high:%u avg:%u low:%u cnt:%u\n", 
          differ_associate_.high, differ_associate_.avg, 
          differ_associate_.low,  differ_associate_.cnt);
      fprintf(stderr, "        track create time (us): high:%u avg:%u low:%u cnt:%u\n", 
          differ_create_.high, differ_create_.avg, 
          differ_create_.low,  differ_create_.cnt);
      fprintf(stderr, "        target touch time (us): high:%u avg:%u low:%u cnt:%u\n", 
          differ_touch_.high, differ_touch_.avg, 
          differ_touch_.low,  differ_touch_.cnt);
      fprintf(stderr, "       track cleanup time (us): high:%u avg:%u low:%u cnt:%u\n", 
          differ_cleanup_.high, differ_cleanup_.avg, 
          differ_cleanup_.low,  differ_cleanup_.cnt);
      fprintf(stderr, "          track post time (us): high:%u avg:%u low:%u cnt:%u\n", 
          differ_post_.high, differ_post_.avg, 
          differ_post_.low,  differ_post_.cnt);
      fprintf(stderr, "                  total tracks: %u\n", track_cnt_);
      fprintf(stderr, "               total test time: %f sec\n", 
          differ_tot_.avg / 1000000.f);
      fprintf(stderr, "\n");
    }

  }
  return true;
}

} // namespace detector

