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
 *
 * ----------
 *
 *  Base state machine for all important threads.
 *
 *     <- no thread | thread ->
 *                  |
 *                  |         ----------
 *  create()        | start() |        |   Paused is wrapped in single-shot state: 'WaitingToPause'
 *   ---\        /--|---------# Paused |   'start' -> WaitingToPause -> Paused
 *       \      /   |         |        |
 *        \    /    |         --------#-
 *    -----#--/--   |          |      |    Stopped is wrapped in single-shot state: 'WaitingToStop'
 *    |         |   |    run() |      |    'stop' -> WaitingToStop -> Stopped
 *    | Stopped |   |          |      |
 *    |         |   |          |      | pause() 
 *    -----#-----   |          |      | 
 *          \       |        --#--------
 *           \      | stop() |         |   Running is wrapped in single-shot state: 'WaitingToRun'
 *            \-----|--------| Running |   'run' -> WaitingToRun -> Running
 *                  |        |         |
 *                  |        -----------
 *
 *  The states 'WaitingToPause', 'WaitingToRun', and 'WaitingToStop' give the individual
 *  threads a place to build-up or tear-down whatever the pipeline requires before the 
 *  thread falls into one of the 'resting' states ('Paused', 'Running', 'Stopped').
 *
 *  The internal thread is created on 'start' and destroyed on 'stop'.
 */

#ifndef BASE_H
#define BASE_H

#include <string>
#include <mutex>
#include <thread>
#include <pthread.h>
#include <vector>
#include <atomic>

#include "utils.h"

namespace detector {

class Base {
  protected:
    Base() = delete;
    Base(unsigned int yield_time);
    virtual ~Base();

  public:
    enum class State {
      kWaitingToStop,
      kStopped,
      kWaitingToPause,
      kPaused,
      kWaitingToRun,
      kRunning
    };

    State getState();
    void wait(State s, int usec);

    bool start(const char* name, int priority=50);  // creates the thread in kPaused state
    bool run();               // moves thread to kRunning state
    bool pause();             // moves thread to kPaused state
    bool stop();              // destroys the thread and leaves in kStopped state

    unsigned int getPriority();
    bool setPriority(int priority);

    std::string getName();
    bool setName(const char* name);

    inline unsigned int getSleepTime()                { return yield_time_; }
    inline void setSleepTime(unsigned int yield_time) { yield_time_ = yield_time; }

  protected:
    virtual bool waitingToRun()   = 0;  // called once before entering kRunning state
    virtual bool running()        = 0;  // called repeatedly while in kRunning state
    virtual bool paused()         = 0;  // called repeatedly while in kPaused state
    virtual bool waitingToHalt()  = 0;  // called once before entering kStopped or kPaused state

  private:
    void wrapper();                     // wrapper around the loop callbacks
    static void wrapper0(Base* self);

  protected:
    std::atomic<unsigned int> yield_time_;
    const unsigned int max_name_len_ = {15};

  private:
    unsigned int priority_;
    std::string name_;
    void setState(State s);
    State state_;
    std::mutex lock_;
    std::thread thread_;
};

} // namespace detector

#endif // BASE_H
