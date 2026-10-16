/* Copyright 2013-2019 Matt Tytel
 *
 * vital is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * vital is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with vital.  If not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

#include "JuceHeader.h"

namespace vital {
  #if INTEL_IPP

  #include "ipps.h"

  class FourierTransform {
    public:
      FourierTransform(int bits) : size_(1 << bits) {
        int spec_size = 0;
        int spec_buffer_size = 0;
        int buffer_size = 0;
        ippsFFTGetSize_R_32f(bits, IPP_FFT_DIV_INV_BY_N, ippAlgHintNone, &spec_size, &spec_buffer_size, &buffer_size);

        spec_ = std::make_unique<Ipp8u[]>(spec_size);
        spec_buffer_ = std::make_unique<Ipp8u[]>(spec_buffer_size);
        buffer_ = std::make_unique<Ipp8u[]>(buffer_size);

        ippsFFTInit_R_32f(&ipp_specs_, bits, IPP_FFT_DIV_INV_BY_N, ippAlgHintNone, spec_.get(), spec_buffer_.get());
      }

      void transformRealForward(float* data) {
        data[size_] = 0.0f;
        ippsFFTFwd_RToPerm_32f_I((Ipp32f*)data, ipp_specs_, buffer_.get());
        data[size_] = data[1];
        data[size_ + 1] = 0.0f;
        data[1] = 0.0f;
      }

      void transformRealInverse(float* data) {
        data[1] = data[size_];
        ippsFFTInv_PermToR_32f_I((Ipp32f*)data, ipp_specs_, buffer_.get());
        memset(data + size_, 0, size_ * sizeof(float));
      }

    private:
      int size_;
      IppsFFTSpec_R_32f *ipp_specs_;
      std::unique_ptr<Ipp8u[]> spec_;
      std::unique_ptr<Ipp8u[]> spec_buffer_;
      std::unique_ptr<Ipp8u[]> buffer_;

      JUCE_LEAK_DETECTOR(FourierTransform)
  };

  #elif SUBSTATION_KFR_FFT

  // SUBstation: KFR's real FFT (compat/kfr_fourier_transform.cpp), in the
  // layout the IPP build's has: forward, bins 0..N/2 interleaved, unscaled, both
  // imaginary parts at DC and Nyquist 0; inverse, scaled by 1 / N, those two
  // ignored, the upper half zeroed. Any number of threads may use one at once.
  class FourierTransform {
    public:
      FourierTransform(int bits);
      ~FourierTransform();

      void transformRealForward(float* data);
      void transformRealInverse(float* data);

    private:
      struct Plan;
      std::unique_ptr<Plan> plan_;
      int size_;

      JUCE_LEAK_DETECTOR(FourierTransform)
  };

  #elif JUCE_MODULE_AVAILABLE_juce_dsp

  class FourierTransform {
    public:
      FourierTransform(int bits) : fft_(bits) { }

      void transformRealForward(float* data) { fft_.performRealOnlyForwardTransform(data, true); }
      void transformRealInverse(float* data) { fft_.performRealOnlyInverseTransform(data); }

    private:
      dsp::FFT fft_;

      JUCE_LEAK_DETECTOR(FourierTransform)
  };

  #elif __APPLE__
  #define VIMAGE_H
  #include <Accelerate/Accelerate.h>

  class FourierTransform {
    public:
      FourierTransform(vDSP_Length bits) : setup_(vDSP_create_fftsetup(bits, 2)), bits_(bits), size_(1 << bits) { }
      ~FourierTransform() {
        vDSP_destroy_fftsetup(setup_);
      }

      void transformRealForward(float* data) {
        static const float kMult = 0.5f;
        data[size_] = 0.0f;
        DSPSplitComplex split = { data, data + 1 };
        vDSP_fft_zrip(setup_, &split, 2, bits_, kFFTDirection_Forward);
        vDSP_vsmul(data, 1, &kMult, data, 1, size_);

        data[size_] = data[1];
        data[size_ + 1] = 0.0f;
        data[1] = 0.0f;
      }

      void transformRealInverse(float* data) {
        float multiplier = 1.0f / size_;
        DSPSplitComplex split = { data, data + 1 };
        data[1] = data[size_];

        vDSP_fft_zrip(setup_, &split, 2, bits_, kFFTDirection_Inverse);
        vDSP_vsmul(data, 1, &multiplier, data, 1, size_ * 2);
        memset(data + size_, 0, size_ * sizeof(float));
      }

    private:
      FFTSetup setup_;
      vDSP_Length bits_;
      vDSP_Length size_;

      JUCE_LEAK_DETECTOR(FourierTransform)
  };

  #else

  // SUBstation: KISS FFT, Vital's fallback, isn't vendored: SUBSTATION_KFR_FFT
  // (engine/CMakeLists.txt) puts FourierTransform on KFR's FFT everywhere.
  #error "Vital's FourierTransform needs SUBSTATION_KFR_FFT"

  #endif

  template <size_t bits>
  class FFT {
    public:
      static FourierTransform* transform() {
        static FFT<bits> instance;
        return &instance.fourier_transform_;
      }

    private:
      FFT() : fourier_transform_(bits) { }

      FourierTransform fourier_transform_;
  };

} // namespace vital
