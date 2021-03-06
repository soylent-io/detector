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

#ifndef TFLOW_H
#define TFLOW_H

#include <string>
#include <memory>
#include <atomic>
#include <thread>
#include <mutex>
#include <map>

#include "utils.h"
#include "listener.h"
#include "base.h"
#include "encoder.h"
#include "tracker.h"

#include "edgetpu.h"

#include <tensorflow/lite/builtin_op_data.h>
#include <tensorflow/lite/interpreter.h>
#include <tensorflow/lite/kernels/register.h>
#include <tensorflow/lite/kernels/internal/tensor_ctypes.h>
#include <tensorflow/lite/model.h>

namespace detector {

class Tflow : public Base, Listener<FrameBuf> {
  public:
    static std::unique_ptr<Tflow> create(unsigned int yield_time, bool quiet, 
        Encoder* enc, Tracker* trk, unsigned int width, unsigned int height, 
        const char* model, const char* labels, unsigned int threads, 
        float threshold, bool tpu);
    virtual ~Tflow();

  public:
    virtual bool addMessage(FrameBuf& data);

  protected:
    Tflow() = delete;
    Tflow(unsigned int yield_time);
    bool init(bool quiet, Encoder* enc, Tracker* trk, unsigned int width, 
        unsigned int height, const char* model, const char* labels, 
        unsigned int threads, float threshold, bool tpu);

  protected:
    virtual bool waitingToRun();
    virtual bool running();
    virtual bool paused();
    virtual bool waitingToHalt();

  private:
    bool quiet_;
    bool tpu_;
    Encoder* enc_;
    Tracker* trk_;
    unsigned int width_;
    unsigned int height_;
    const unsigned int channels_ = {3};
    unsigned int model_width_;
    unsigned int model_height_;
    unsigned int model_channels_;
    float threshold_;

    std::string model_fname_;
    unsigned int model_threads_;

    std::string labels_fname_;
    std::map<unsigned int, std::pair<std::string, BoxBuf::Type>> label_pairs_;
    const std::map<std::string, BoxBuf::Type> boxbuf_pairs_ = 
    {
      { "person",     BoxBuf::Type::kPerson  },
      { "cat",        BoxBuf::Type::kPet     },
      { "dog",        BoxBuf::Type::kPet     },
      { "car",        BoxBuf::Type::kVehicle },
      { "bus",        BoxBuf::Type::kVehicle },
      { "truck",      BoxBuf::Type::kVehicle },
      { "bicycle",    BoxBuf::Type::kVehicle },
      { "motorcycle", BoxBuf::Type::kVehicle }
    };

    class Frame {
      public:
        Frame() {}
        ~Frame() {}
      public:
        unsigned int id;
        unsigned int length;
        std::vector<unsigned char> buf;
    };
    unsigned int frame_len_;
    Tflow::Frame frame_;

    std::unique_ptr<tflite::FlatBufferModel> model_;
    std::shared_ptr<edgetpu::EdgeTpuContext> edgetpu_context_;
    std::unique_ptr<tflite::Interpreter> model_interpreter_;
    std::unique_ptr<tflite::Interpreter> resize_interpreter_;

    MicroDiffer<uint32_t> differ_copy_;
    MicroDiffer<uint32_t> differ_prep_;
    MicroDiffer<uint32_t> differ_eval_;
    MicroDiffer<uint32_t> differ_post_;
    MicroDiffer<uint32_t> differ_tot_;

    unsigned int post_id_ = {0};
    const unsigned int result_num_ = {10};

    void resize(std::unique_ptr<tflite::Interpreter>& interpreter,
        uint8_t* out, uint8_t* in,
        int image_height, int image_width, int image_channels, 
        int wanted_height, int wanted_width, int wanted_channels, 
        int yield);
    bool prep();
    bool eval();
    bool post(bool report);
    bool oneRun(bool report);

    std::timed_mutex tflow_lock_;
    std::atomic<bool> tflow_on_;
    std::atomic<bool> tflow_empty_;

#ifdef CAPTURE_ONE_RAW_FRAME
    unsigned int counter = {10};
#endif
};

} // namespace detector

#endif // TFLOW_H
