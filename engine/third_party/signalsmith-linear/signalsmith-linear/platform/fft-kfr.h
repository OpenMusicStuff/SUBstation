// SUBstation's own (not Signalsmith's): Signalsmith Linear's power-of-two FFTs
// on KFR's DFT (engine/third_party/kfr, the kfr_fft target), as its other
// platform backends (PFFFT, Accelerate, IPP) put theirs in place: included at
// the end of fft.h with SIGNALSMITH_USE_KFR defined. Everything built on these
// (SplitFFT, RealFFT, the STFT, so Signalsmith Stretch) runs on KFR's FFT.
//
// Plans and scratch are made in resize() (not real-time), never in fft()/ifft().
// Small sizes use the library's own (SimpleFFT), quicker there: complex below 4
// points, real below 128 (benchmarks/fft_bench.cpp). Unscaled both ways, as
// the library's own.

#ifndef SIGNALSMITH_AUDIO_LINEAR_FFT_KFR_H
#define SIGNALSMITH_AUDIO_LINEAR_FFT_KFR_H

#include <kfr/dft.hpp>

#include <cstring>
#include <memory>

namespace signalsmith { namespace linear {

namespace _impl {

inline constexpr size_t kMinKfrSize = 4;
inline constexpr size_t kMinKfrRealSize = 128;

// SimpleFFT with the prefersSplit SimpleRealFFT reads (as the library's own
// Pow2FFT on it): SimpleFFT has none, and MSVC instantiates that read with
// SimpleRealFFT's members, so SimpleRealFFT<Sample, SimpleFFT<Sample>> breaks.
template<typename Sample>
struct SimplePow2FFT : SimpleFFT<Sample> {
	static constexpr bool prefersSplit = true;
	using SimpleFFT<Sample>::SimpleFFT;
};

template<typename Sample>
struct KfrComplexFFT {
	static constexpr bool prefersSplit = false;
	using Complex = std::complex<Sample>;
	using KfrComplex = kfr::complex<Sample>;
	static_assert(sizeof(KfrComplex) == sizeof(Complex), "KFR's complex is laid out as std::complex");

	KfrComplexFFT(size_t size=0) {
		resize(size);
	}
	KfrComplexFFT(const KfrComplexFFT &) = delete;
	KfrComplexFFT(KfrComplexFFT &&) = default;

	void resize(size_t size) {
		size_ = size;
		plan_.reset();
		if (size >= kMinKfrSize) {
			plan_ = std::make_unique<kfr::dft_plan<Sample>>(size);
			temp_.resize(plan_->temp_size);
			small_.resize(0);
		} else {
			temp_.resize(0);
			small_.resize(size);
		}
		inR_.resize(size);
		outR_.resize(size);
	}

	void fft(const Complex *time, Complex *freq) {
		run(false, time, freq);
	}
	void ifft(const Complex *freq, Complex *time) {
		run(true, freq, time);
	}
	void fft(const Sample *inR, const Sample *inI, Sample *outR, Sample *outI) {
		runSplit(false, inR, inI, outR, outI);
	}
	void ifft(const Sample *inR, const Sample *inI, Sample *outR, Sample *outI) {
		runSplit(true, inR, inI, outR, outI);
	}

private:
	void run(bool inverse, const Complex *in, Complex *out) {
		if (!plan_) {
			if (inverse) small_.ifft(in, out);
			else small_.fft(in, out);
			return;
		}
		if (in == out) {  // (in place: through scratch, whatever KFR's plan allows)
			std::memcpy(inR_.data(), in, size_*sizeof(Complex));
			in = inR_.data();
		}
		plan_->execute(reinterpret_cast<KfrComplex *>(out), reinterpret_cast<const KfrComplex *>(in), temp_.data(),
		               inverse);
	}
	void runSplit(bool inverse, const Sample *inR, const Sample *inI, Sample *outR, Sample *outI) {
		for (size_t i = 0; i < size_; ++i) inR_[i] = {inR[i], inI[i]};
		run(inverse, inR_.data(), outR_.data());
		for (size_t i = 0; i < size_; ++i) {
			outR[i] = outR_[i].real();
			outI[i] = outR_[i].imag();
		}
	}

	size_t size_ = 0;
	std::unique_ptr<kfr::dft_plan<Sample>> plan_;
	std::vector<kfr::u8> temp_;
	std::vector<Complex> inR_, outR_;  // interleaved scratch: split data, in-place calls
	SimpleFFT<Sample> small_;
};

template<typename Sample>
struct KfrRealFFT {
	static constexpr bool prefersSplit = false;
	using Complex = std::complex<Sample>;
	using KfrComplex = kfr::complex<Sample>;

	KfrRealFFT(size_t size=0) {
		resize(size);
	}
	KfrRealFFT(const KfrRealFFT &) = delete;
	KfrRealFFT(KfrRealFFT &&) = default;

	void resize(size_t size) {
		size_ = size;
		plan_.reset();
		if (size >= kMinKfrRealSize) {
			// Perm: bin 0 holds DC and Nyquist (real, imaginary), as the library packs it.
			plan_ = std::make_unique<kfr::dft_plan_real<Sample>>(size, kfr::dft_pack_format::Perm);
			temp_.resize(plan_->temp_size);
			small_.resize(0);
		} else {
			temp_.resize(0);
			small_.resize(size);
		}
		freq_.resize(size/2);
	}

	void fft(const Sample *time, Complex *freq) {
		if (!plan_) return small_.fft(time, freq);
		plan_->execute(reinterpret_cast<KfrComplex *>(freq), time, temp_.data());
	}
	void fft(const Sample *inR, Sample *outR, Sample *outI) {
		if (!plan_) return small_.fft(inR, outR, outI);
		fft(inR, freq_.data());
		for (size_t i = 0; i < size_/2; ++i) {
			outR[i] = freq_[i].real();
			outI[i] = freq_[i].imag();
		}
	}
	void ifft(const Complex *freq, Sample *time) {
		if (!plan_) return small_.ifft(freq, time);
		// (KFR's inverse may use its input as scratch: a copy.)
		std::memcpy(freq_.data(), freq, size_/2*sizeof(Complex));
		plan_->execute(time, reinterpret_cast<const KfrComplex *>(freq_.data()), temp_.data(), kfr::cinvert_t{});
	}
	void ifft(const Sample *inR, const Sample *inI, Sample *outR) {
		if (!plan_) return small_.ifft(inR, inI, outR);
		for (size_t i = 0; i < size_/2; ++i) freq_[i] = {inR[i], inI[i]};
		plan_->execute(outR, reinterpret_cast<const KfrComplex *>(freq_.data()), temp_.data(), kfr::cinvert_t{});
	}

private:
	size_t size_ = 0;
	std::unique_ptr<kfr::dft_plan_real<Sample>> plan_;
	std::vector<kfr::u8> temp_;
	std::vector<Complex> freq_;
	SimpleRealFFT<Sample, SimplePow2FFT<Sample>> small_;
};

}  // namespace _impl

template<>
struct Pow2FFT<float> : _impl::KfrComplexFFT<float> {
	using _impl::KfrComplexFFT<float>::KfrComplexFFT;
};
template<>
struct Pow2FFT<double> : _impl::KfrComplexFFT<double> {
	using _impl::KfrComplexFFT<double>::KfrComplexFFT;
};
template<>
struct Pow2RealFFT<float> : _impl::KfrRealFFT<float> {
	using _impl::KfrRealFFT<float>::KfrRealFFT;
};
template<>
struct Pow2RealFFT<double> : _impl::KfrRealFFT<double> {
	using _impl::KfrRealFFT<double>::KfrRealFFT;
};

}}  // namespace

#endif  // include guard
