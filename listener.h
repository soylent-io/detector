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
 */

#ifndef LISTENER_H
#define LISTENER_H

#include <string>
#include <mutex>
#include <thread>
#include <pthread.h>
#include <vector>
#include <atomic>

#include "utils.h"

namespace detector {

// encapsulate a frame buffer
class FrameBuf {
  public:
    FrameBuf() : id(0), length(0), addr(nullptr) {}
    ~FrameBuf() {}
  public:
    unsigned int id;
    unsigned int length;
    unsigned char* addr;
};

// encapsulate box
class BoxBuf {
  public:
    enum class Type {
      kUnknown = 0,
      kPerson,
      kPet,
      kVehicle
    };
  public:
    BoxBuf() = default;
    BoxBuf(BoxBuf::Type type, unsigned int id, unsigned int left, 
        unsigned int top, unsigned int width, unsigned int height) 
      : type(type), id(id), x(left), y(top), w(width), h(height) {}
    ~BoxBuf() {}
  public:
    BoxBuf::Type type;
    unsigned int id;
    unsigned int x, y, w, h;
};

// encapsulate track
class TrackBuf : public BoxBuf {
  public:
    TrackBuf() = default;
    TrackBuf(BoxBuf::Type type, unsigned int id, unsigned int left, 
        unsigned int top, unsigned int width, unsigned int height) 
      : BoxBuf(type, id, left, top, width, height) {}
    ~TrackBuf() {}
};

// encapsulate NAL
class NalBuf {
  public:
    NalBuf() = delete;
    NalBuf(unsigned int l, unsigned char* a) 
      : length(l), addr(a) {}
    NalBuf(NalBuf const & n) = delete;
    ~NalBuf() {}
  public:
    unsigned int length;
    unsigned char* addr;
};


// listen for a message
template<typename T>
class Listener {
  public:
    Listener() {};
    virtual ~Listener() {}

  public:
    const unsigned int timeout_{1000};
    virtual bool addMessage(T& data) = 0;
};

} // namespace detector

#endif // BASE_H
