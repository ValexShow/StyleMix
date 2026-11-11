#pragma once

// Питч-шифтер с сохранением формант (без JUCE, чтобы можно было тестировать отдельно).
//
// Как работает:
//  1. Сигнал режется на перекрывающиеся окна (STFT, перекрытие x8).
//  2. В каждом окне ищутся спектральные пики (гармоники голоса). Каждый пик вместе
//     со своей "областью" переносится на новую частоту целиком, а фазы внутри области
//     сохраняют свои соотношения (phase-locked vocoder, Laroche & Dolson). Это убирает
//     "металлический"/"фазовый" призвук обычного вокодера.
//  3. Огибающая спектра (форманты = тембр голоса) считается через кепстр. После переноса
//     гармоник их громкость корректируется так, чтобы огибающая осталась на месте.
//     Именно сдвиг формант вместе с высотой даёт эффект "бурундука" (вверх) и
//     "демона" (вниз) — здесь форманты стоят на месте, поэтому голос звучит естественно.
//  4. При понижении верхний "воздух" (шипящие, дыхание выше границы) берётся из
//     оригинала, чтобы голос не становился глухим.

#include <algorithm>
#include <cmath>
#include <complex>
#include <vector>

namespace pitchshift
{

constexpr double kPi = 3.14159265358979323846;

class FFT
{
public:
    void prepare (int size)
    {
        n = size;
        levels = 0;
        while ((1 << levels) < n) ++levels;
        rev.resize ((size_t) n);
        for (int i = 0; i < n; ++i)
        {
            int r = 0;
            for (int b = 0; b < levels; ++b)
                if (i & (1 << b)) r |= 1 << (levels - 1 - b);
            rev[(size_t) i] = r;
        }
        tw.resize ((size_t) n / 2);
        for (int i = 0; i < n / 2; ++i)
            tw[(size_t) i] = std::polar (1.0f, (float) (-2.0 * kPi * i / n));
    }

    // Неотнормированное прямое; обратное делится на n.
    void transform (std::complex<float>* d, bool inverse) const
    {
        for (int i = 0; i < n; ++i)
            if (i < rev[(size_t) i]) std::swap (d[i], d[rev[(size_t) i]]);

        for (int size = 2; size <= n; size <<= 1)
        {
            const int half = size / 2, step = n / size;
            for (int i = 0; i < n; i += size)
                for (int j = 0; j < half; ++j)
                {
                    auto w = tw[(size_t) (j * step)];
                    if (inverse) w = std::conj (w);
                    const auto t = d[i + j + half] * w;
                    d[i + j + half] = d[i + j] - t;
                    d[i + j] += t;
                }
        }

        if (inverse)
        {
            const float s = 1.0f / (float) n;
            for (int i = 0; i < n; ++i) d[i] *= s;
        }
    }

private:
    int n = 0, levels = 0;
    std::vector<int> rev;
    std::vector<std::complex<float>> tw;
};

class FormantPitchShifter
{
public:
    void prepare (double sampleRate)
    {
        sr = sampleRate;
        N = sampleRate > 60000.0 ? 4096 : 2048;
        H = N / 8;
        bins = N / 2 + 1;
        latency = N - H;

        fft.prepare (N);
        window.assign ((size_t) N, 0.0f);
        for (int i = 0; i < N; ++i)
            window[(size_t) i] = 0.5f - 0.5f * std::cos (2.0f * (float) kPi * (float) i / (float) N);

        // Нормировка overlap-add для окна анализа * окна синтеза.
        double total = 0.0;
        for (int i = 0; i < N; ++i)
            total += (double) window[(size_t) i] * window[(size_t) i];
        olaScale = (float) ((double) H / total);

        // Кепстральный фильтр: оставляем всё короче ~1.6 мс — это огибающая, а не высота тона
        // (период голоса даже у высокого женского > 2 мс).
        lifter = std::max (8, (int) std::lround (sampleRate * 0.0016));

        inFifo.assign ((size_t) N, 0.0f);
        outFifo.assign ((size_t) N, 0.0f);
        outAccum.assign ((size_t) (2 * N), 0.0f);
        buf.assign ((size_t) N, {});
        cep.assign ((size_t) N, {});
        out.assign ((size_t) bins, {});
        mag.assign ((size_t) bins, 0.0f);
        phase.assign ((size_t) bins, 0.0f);
        lastPhase.assign ((size_t) bins, 0.0f);
        prevOutPhase.assign ((size_t) bins, 0.0f);
        trueFreq.assign ((size_t) bins, 0.0f);
        logEnv.assign ((size_t) bins, 0.0f);
        peaks.assign ((size_t) bins, 0);
        reset();
    }

    void reset()
    {
        std::fill (inFifo.begin(), inFifo.end(), 0.0f);
        std::fill (outFifo.begin(), outFifo.end(), 0.0f);
        std::fill (outAccum.begin(), outAccum.end(), 0.0f);
        std::fill (lastPhase.begin(), lastPhase.end(), 0.0f);
        std::fill (prevOutPhase.begin(), prevOutPhase.end(), 0.0f);
        rover = latency;
        levelGain = 1.0f;
    }

    int getLatency() const { return latency; }

    // ratio: во сколько раз изменить высоту (2 = +октава, 0.5 = -октава)
    // formantRatio: доп. сдвиг формант (1 = не трогать)
    // preserve: 0..1, насколько удерживать форманты на месте (1 = полностью естественно)
    void setParameters (float ratio, float formantRatio, float preserveAmount)
    {
        pitchRatio = std::clamp (ratio, 0.25f, 4.0f);
        formantShift = std::clamp (formantRatio, 0.5f, 2.0f);
        preserve = std::clamp (preserveAmount, 0.0f, 1.0f);
    }

    void process (float* data, int numSamples)
    {
        for (int i = 0; i < numSamples; ++i)
        {
            inFifo[(size_t) rover] = data[i];
            data[i] = outFifo[(size_t) (rover - latency)];
            if (++rover >= N)
            {
                rover = latency;
                processFrame();
            }
        }
    }

private:
    static float wrap (float p)
    {
        p = std::fmod (p + (float) kPi, 2.0f * (float) kPi);
        if (p < 0.0f) p += 2.0f * (float) kPi;
        return p - (float) kPi;
    }

    float envAt (float bin) const
    {
        bin = std::clamp (bin, 0.0f, (float) (bins - 1));
        const int i = std::min ((int) bin, bins - 2);
        const float f = bin - (float) i;
        return logEnv[(size_t) i] + f * (logEnv[(size_t) i + 1] - logEnv[(size_t) i]);
    }

    void processFrame()
    {
        const float twoPi = 2.0f * (float) kPi;
        const float expect = twoPi * (float) H / (float) N;

        // --- Анализ ---
        for (int i = 0; i < N; ++i)
            buf[(size_t) i] = { inFifo[(size_t) i] * window[(size_t) i], 0.0f };
        fft.transform (buf.data(), false);

        float maxMag = 0.0f;
        for (int k = 0; k < bins; ++k)
        {
            const auto c = buf[(size_t) k];
            mag[(size_t) k] = std::abs (c);
            phase[(size_t) k] = std::arg (c);
            const float dp = wrap (phase[(size_t) k] - lastPhase[(size_t) k] - expect * (float) k);
            lastPhase[(size_t) k] = phase[(size_t) k];
            trueFreq[(size_t) k] = (float) k + dp / expect;
            maxMag = std::max (maxMag, mag[(size_t) k]);
        }

        // --- Огибающая (кепстр) ---
        const float floorMag = std::max (maxMag * 1.0e-6f, 1.0e-9f);
        for (int k = 0; k < bins; ++k)
        {
            const float lm = std::log (mag[(size_t) k] + floorMag);
            cep[(size_t) k] = { lm, 0.0f };
            if (k > 0 && k < N / 2) cep[(size_t) (N - k)] = { lm, 0.0f };
        }
        fft.transform (cep.data(), true);
        for (int n = 0; n < N; ++n)
        {
            const int q = n <= N / 2 ? n : N - n;
            float w = 0.0f;
            if (q < lifter) w = 1.0f;
            else if (q < lifter + 4) w = 0.5f + 0.5f * std::cos ((float) kPi * (float) (q - lifter + 1) / 5.0f);
            cep[(size_t) n] = { cep[(size_t) n].real() * w, 0.0f };
        }
        fft.transform (cep.data(), false);
        for (int k = 0; k < bins; ++k)
            logEnv[(size_t) k] = cep[(size_t) k].real();

        // --- Поиск пиков ---
        int numPeaks = 0;
        const float peakThresh = std::max (maxMag * 1.0e-6f, 1.0e-9f);
        for (int k = 2; k < bins - 2; ++k)
        {
            const float m = mag[(size_t) k];
            if (m > peakThresh && m > mag[(size_t) k - 1] && m >= mag[(size_t) k + 1]
                && m > mag[(size_t) k - 2] && m >= mag[(size_t) k + 2])
                peaks[(size_t) numPeaks++] = k;
        }

        std::fill (out.begin(), out.end(), std::complex<float> {});

        const float r = pitchRatio;
        const float lnMaxBoost = std::log (100.0f);  // +40 dB максимум
        const float lnMaxCut = std::log (1.0e-3f);    // -60 dB

        int regionStart = 0;
        for (int pi = 0; pi < numPeaks; ++pi)
        {
            const int p = peaks[(size_t) pi];
            int regionEnd = bins; // не включительно
            if (pi + 1 < numPeaks)
            {
                const int next = peaks[(size_t) pi + 1];
                int lowest = p + 1;
                for (int k = p + 1; k < next; ++k)
                    if (mag[(size_t) k] < mag[(size_t) lowest]) lowest = k;
                regionEnd = lowest;
            }

            float fp = trueFreq[(size_t) p];
            if (std::abs (fp - (float) p) > 1.5f) fp = (float) p;
            const float fNew = fp * r;
            const int shift = (int) std::lround (fNew - fp);
            const int newPeak = p + shift;

            if (newPeak > 0 && newPeak < bins - 1)
            {
                const float newPhase = prevOutPhase[(size_t) newPeak] + expect * fNew;
                const float rot = newPhase - phase[(size_t) p];

                for (int k = regionStart; k < regionEnd; ++k)
                {
                    const int kk = k + shift;
                    if (kk < 0 || kk >= bins) continue;

                    // Целевая огибающая в новой точке:
                    //  preserve=1 -> огибающая оригинала (форманты на месте)
                    //  preserve=0 -> огибающая "уехала" вместе с высотой (классический эффект)
                    const float fk = (float) kk / formantShift;
                    const float target = preserve * envAt (fk) + (1.0f - preserve) * envAt (fk / r);
                    float lnGain = target - logEnv[(size_t) k];
                    lnGain = std::clamp (lnGain, lnMaxCut, lnMaxBoost);

                    out[(size_t) kk] += std::polar (mag[(size_t) k] * std::exp (lnGain), phase[(size_t) k] + rot);
                }
            }
            regionStart = regionEnd;
        }

        // При понижении сверху остаётся пустота — подмешиваем оригинальный "воздух".
        if (r < 0.999f)
        {
            const float nyqBin = (float) (bins - 1);
            const float a = r * nyqBin * 0.80f, b = r * nyqBin * 0.95f;
            for (int k = (int) a; k < bins; ++k)
            {
                const float w = std::clamp (((float) k - a) / (b - a), 0.0f, 1.0f);
                out[(size_t) k] = out[(size_t) k] * (1.0f - w) + buf[(size_t) k] * w;
            }
        }

        // Громкость: при сдвиге меняется число гармоник в каждой форманте, поэтому
        // выравниваем энергию кадра по оригиналу (плавно, чтобы не было "качания").
        double inE = 0.0, outE = 0.0;
        for (int k = 0; k < bins; ++k)
        {
            inE += (double) mag[(size_t) k] * mag[(size_t) k];
            outE += (double) std::norm (out[(size_t) k]);
        }
        if (inE > 1.0e-12 && outE > 1.0e-12)
        {
            const float g = std::clamp ((float) std::sqrt (inE / outE), 0.25f, 4.0f);
            levelGain += (g - levelGain) * 0.25f;
        }

        for (int k = 0; k < bins; ++k)
            prevOutPhase[(size_t) k] = std::abs (out[(size_t) k]) > 0.0f ? std::arg (out[(size_t) k])
                                                                         : wrap (prevOutPhase[(size_t) k] + expect * (float) k);

        // --- Синтез ---
        for (int k = 0; k < bins; ++k)
            buf[(size_t) k] = out[(size_t) k];
        buf[0] = { buf[0].real(), 0.0f };
        buf[(size_t) (N / 2)] = { buf[(size_t) (N / 2)].real(), 0.0f };
        for (int k = 1; k < N / 2; ++k)
            buf[(size_t) (N - k)] = std::conj (buf[(size_t) k]);
        fft.transform (buf.data(), true);

        for (int i = 0; i < N; ++i)
            outAccum[(size_t) i] += buf[(size_t) i].real() * window[(size_t) i] * olaScale * levelGain;

        for (int i = 0; i < H; ++i)
            outFifo[(size_t) i] = outAccum[(size_t) i];
        std::copy (outAccum.begin() + H, outAccum.end(), outAccum.begin());
        std::fill (outAccum.end() - H, outAccum.end(), 0.0f);
        std::copy (inFifo.begin() + H, inFifo.end(), inFifo.begin());
    }

    double sr = 44100.0;
    int N = 2048, H = 256, bins = 1025, latency = 1792, rover = 0, lifter = 70;
    float olaScale = 1.0f, levelGain = 1.0f;
    float pitchRatio = 1.0f, formantShift = 1.0f, preserve = 1.0f;

    FFT fft;
    std::vector<float> window, inFifo, outFifo, outAccum;
    std::vector<std::complex<float>> buf, cep, out;
    std::vector<float> mag, phase, lastPhase, prevOutPhase, trueFreq, logEnv;
    std::vector<int> peaks;
};

} // namespace pitchshift
