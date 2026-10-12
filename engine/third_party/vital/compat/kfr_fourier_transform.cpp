// SUBstation's (not Vital's): Vital's FourierTransform on KFR's real FFT
// (third_party/kfr, GPLv2+), for builds without Intel IPP, JUCE or Apple's vDSP,
// which would otherwise fall back on KISS FFT. Every voice's oscillators run an
// inverse transform a block: KFR's is about 13 times as fast as KISS FFT's.
//
// The layout is the one the IPP build's transforms give and take
// (fourier_transform.h): forward, the N / 2 + 1 bins interleaved, unscaled, the
// imaginary parts at DC and Nyquist 0; inverse, from those bins (both imaginary
// parts ignored), scaled by 1 / N, the upper half of the buffer zeroed.
//
// A plan is read-only once made, and KFR needs no scratch memory at Vital's sizes
// (a stack buffer otherwise), so any number of threads may transform at once.

#include "fourier_transform.h"

#include <cstring>
#include <vector>

#include <kfr/dft.hpp>

namespace vital {

  struct FourierTransform::Plan {
    explicit Plan(size_t size) : real(size, kfr::dft_pack_format::CCs) { }
    kfr::dft_plan_real<float> real;
  };

  namespace {
    constexpr int kMaxStackSize = 4096;  // floats: the inverse's output on the stack up to this size

    // KFR's scratch for a plan (none at Vital's sizes): this thread's own.
    kfr::u8* scratch(size_t bytes) {
      if (bytes == 0)
        return nullptr;
      thread_local std::vector<kfr::u8> memory;
      if (memory.size() < bytes)
        memory.resize(bytes);
      return memory.data();
    }
  }

  FourierTransform::FourierTransform(int bits) :
      plan_(std::make_unique<Plan>(size_t(1) << bits)), size_(1 << bits) { }

  FourierTransform::~FourierTransform() = default;

  void FourierTransform::transformRealForward(float* data) {
    // In place: the N samples become N / 2 + 1 bins (N + 2 floats).
    plan_->real.execute(reinterpret_cast<kfr::complex<float>*>(data), data, scratch(plan_->real.temp_size));
  }

  void FourierTransform::transformRealInverse(float* data) {
    data[1] = 0.0f;
    data[size_ + 1] = 0.0f;
    const auto* bins = reinterpret_cast<const kfr::complex<float>*>(data);
    kfr::u8* temp = scratch(plan_->real.temp_size);
    const float scale = 1.0f / size_;
    if (size_ <= kMaxStackSize) {
      // Out of place (faster than in place), into a buffer of this call's own.
      alignas(64) float time[kMaxStackSize];
      plan_->real.execute(time, bins, temp, kfr::cinvert_t{});
      for (int i = 0; i < size_; ++i)
        data[i] = time[i] * scale;
    }
    else {
      thread_local std::vector<float> time;
      if (time.size() < static_cast<size_t>(size_))
        time.resize(size_);
      plan_->real.execute(time.data(), bins, temp, kfr::cinvert_t{});
      for (int i = 0; i < size_; ++i)
        data[i] = time[i] * scale;
    }
    memset(data + size_, 0, size_ * sizeof(float));
  }

} // namespace vital
