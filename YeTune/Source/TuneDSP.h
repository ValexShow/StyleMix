#pragma once

// YeTune DSP: определение высоты (YIN) + сдвиг высоты PSOLA.
// Без зависимостей от JUCE, чтобы можно было тестировать отдельно.
//
// Почему PSOLA: голос режется на кусочки длиной в два периода основного тона
// и складывается обратно с другим шагом. Форманты (тембр, "размер" рта)
// остаются на месте, меняется только высота. Поэтому нет ни "бурундука",
// ни "демона", как у простого ресемплинга.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace yt
{

constexpr double kPi = 3.14159265358979323846;

// ---------------------------------------------------------------- Biquad
struct Biquad
{
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;

    void setLowpass (double sr, double fc, double q = 0.7071)
    {
        const double w = 2.0 * kPi * fc / sr, c = std::cos (w), al = std::sin (w) / (2.0 * q), a0 = 1.0 + al;
        b0 = (float) ((1.0 - c) * 0.5 / a0); b1 = (float) ((1.0 - c) / a0); b2 = b0;
        a1 = (float) (-2.0 * c / a0); a2 = (float) ((1.0 - al) / a0);
    }
    void setHighpass (double sr, double fc, double q = 0.7071)
    {
        const double w = 2.0 * kPi * fc / sr, c = std::cos (w), al = std::sin (w) / (2.0 * q), a0 = 1.0 + al;
        b0 = (float) ((1.0 + c) * 0.5 / a0); b1 = (float) (-(1.0 + c) / a0); b2 = b0;
        a1 = (float) (-2.0 * c / a0); a2 = (float) ((1.0 - al) / a0);
    }
    void reset() { z1 = z2 = 0; }
    float process (float x)
    {
        const float y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
};

// ---------------------------------------------------------------- Лады
enum Scale { scaleChromatic = 0, scaleMajor, scaleMinor, scaleHarmMinor, scaleMinorPenta, scaleMajorPenta, scaleBlues, numScales };

inline uint16_t scaleMask (int key, int scale)
{
    static const std::array<std::vector<int>, numScales> steps {{
        { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 },
        { 0, 2, 4, 5, 7, 9, 11 },
        { 0, 2, 3, 5, 7, 8, 10 },
        { 0, 2, 3, 5, 7, 8, 11 },
        { 0, 3, 5, 7, 10 },
        { 0, 2, 4, 7, 9 },
        { 0, 3, 5, 6, 7, 10 },
    }};
    uint16_t m = 0;
    for (int s : steps[(size_t) std::clamp (scale, 0, numScales - 1)])
        m |= (uint16_t) (1u << ((key + s) % 12));
    return m;
}

// Ближайшая нота (MIDI) из маски лада
inline int nearestAllowed (float midi, uint16_t mask)
{
    if (mask == 0) mask = 0xfff;
    const int base = (int) std::floor (midi);
    int best = base;
    float bestD = 1e9f;
    for (int n = base - 6; n <= base + 7; ++n)
        if (mask & (1u << (((n % 12) + 12) % 12)))
        {
            const float d = std::abs (midi - (float) n);
            if (d < bestD) { bestD = d; best = n; }
        }
    return best;
}

// ---------------------------------------------------------------- Тюнер
class Tuner
{
public:
    static constexpr int maxChannels = 2;

    // Параметры (меняются из процессора перед каждым блоком)
    uint16_t mask = 0xfff;
    float retuneMs = 0.0f;   // 0 = мгновенно (жёсткий "роботский" эффект)
    float amount = 1.0f;     // сила коррекции 0..1
    float humanize = 0.0f;   // 0..1: длинные ноты держатся жёстко, короткие переходы мягче

    // Для интерфейса
    float detectedMidi = -1.0f, targetMidi = -1.0f;

    void prepare (double sampleRate)
    {
        sr = sampleRate;
        tMax = (int) std::ceil (sr / 65.0);          // самый низкий голос ~65 Гц
        unvoicedHalf = std::max (32, (int) std::round (sr * 0.005));
        latency = 3 * tMax + 2;

        size_t n = 1;
        while (n < (size_t) (sr * 0.25) + (size_t) (8 * tMax)) n <<= 1;
        bufMask = (int64_t) n - 1;
        for (int c = 0; c < maxChannels; ++c)
        {
            in[c].assign (n, 0.0f);
            acc[c].assign (n, 0.0f);
        }
        wacc.assign (n, 0.0f);

        decim = std::max (1, (int) std::lround (sr / 22050.0));
        detSr = sr / decim;
        detW = (int) std::round (detSr * 0.025);
        detTauMax = (int) std::ceil (detSr / 65.0);
        detTauMin = std::max (2, (int) std::floor (detSr / 1100.0));
        detHop = std::max (16, (int) std::round (detSr * 0.004));
        detLen = detW + detTauMax + 2;
        detBuf.assign ((size_t) detLen * 2, 0.0f);
        diff.assign ((size_t) detTauMax + 2, 0.0f);
        lp1.setLowpass (sr, 1500.0); lp2.setLowpass (sr, 1500.0);
        hp.setHighpass (sr, 60.0);

        reset();
    }

    void reset()
    {
        for (int c = 0; c < maxChannels; ++c)
        {
            std::fill (in[c].begin(), in[c].end(), 0.0f);
            std::fill (acc[c].begin(), acc[c].end(), 0.0f);
        }
        std::fill (wacc.begin(), wacc.end(), 0.0f);
        std::fill (detBuf.begin(), detBuf.end(), 0.0f);
        lp1.reset(); lp2.reset(); hp.reset();
        now = 0;
        detWrite = 0; detCount = 0; decimPhase = 0;
        voiced = false; curPeriod = (float) tMax * 0.5f; ratio = 1.0f;
        corr = 0.0f; curTarget = -1; voicedTime = 0.0f;
        periodHist[0] = periodHist[1] = periodHist[2] = 0.0f;
        markHead = markCount = 0;
        lastMark = (double) -tMax;
        nextSynth = (double) -tMax;
        detectedMidi = targetMidi = -1.0f;
    }

    int getLatency() const { return latency; }

    // Один сэмпл для numCh каналов (1 или 2). in/out могут совпадать.
    void process (const float* x, float* y, int numCh)
    {
        const int64_t n = now;
        float mono = 0.0f;
        for (int c = 0; c < numCh; ++c)
        {
            in[c][(size_t) (n & bufMask)] = x[c];
            mono += x[c];
        }
        mono /= (float) numCh;

        // --- анализ высоты на прореженном сигнале
        const float f = lp2.process (lp1.process (hp.process (mono)));
        if (++decimPhase >= decim)
        {
            decimPhase = 0;
            detBuf[(size_t) detWrite] = f;
            detBuf[(size_t) (detWrite + detLen)] = f; // зеркальная копия для линейного чтения
            if (++detWrite >= detLen) detWrite = 0;
            if (++detCount >= detHop)
            {
                detCount = 0;
                analyse();
            }
        }

        // --- метки анализа (по периодам основного тона)
        for (;;)
        {
            const double step = voiced ? (double) curPeriod : (double) unvoicedHalf;
            const double next = lastMark + step;
            if (next > (double) (n - tMax)) break;
            Mark m;
            m.pos = (int64_t) std::llround (next);
            m.half = voiced ? (int) std::lround (curPeriod) : 0;
            m.ratio = ratio;
            if (markCount < (int) markRing.size())
            {
                markRing[(size_t) ((markHead + markCount) % (int) markRing.size())] = m;
                ++markCount;
            }
            lastMark = next;
        }

        // --- синтез (наложение кусочков с новым шагом)
        while (nextSynth <= (double) (n - 2 * tMax))
        {
            const int64_t s = (int64_t) std::floor (nextSynth);
            while (markCount > 1 && markAt (1).pos <= s) { markHead = (markHead + 1) % (int) markRing.size(); --markCount; }
            const Mark* best = nullptr;
            if (markCount > 0)
            {
                best = &markAt (0);
                if (markCount > 1 && std::llabs (markAt (1).pos - s) < std::llabs (markAt (0).pos - s))
                    best = &markAt (1);
            }

            int half;
            int64_t src;
            double step;
            if (best == nullptr || best->half == 0)
            {
                half = unvoicedHalf; src = s; step = unvoicedHalf;
            }
            else
            {
                half = best->half; src = best->pos;
                step = (double) best->half / (double) best->ratio;
            }

            const float inv = (float) kPi / (float) half;
            for (int k = -half; k < half; ++k)
            {
                const float w = 0.5f - 0.5f * std::cos (inv * (float) (k + half));
                const size_t di = (size_t) ((s + k) & bufMask), si = (size_t) ((src + k) & bufMask);
                for (int c = 0; c < numCh; ++c)
                    acc[c][di] += w * in[c][si];
                wacc[di] += w;
            }
            nextSynth += step;
        }

        // --- выход
        const size_t o = (size_t) ((n - latency) & bufMask);
        const float norm = 1.0f / std::max (wacc[o], 0.35f);
        for (int c = 0; c < numCh; ++c)
        {
            y[c] = acc[c][o] * norm;
            acc[c][o] = 0.0f;
        }
        wacc[o] = 0.0f;
        ++now;
    }

private:
    struct Mark { int64_t pos = 0; int half = 0; float ratio = 1.0f; };
    const Mark& markAt (int i) const { return markRing[(size_t) ((markHead + i) % (int) markRing.size())]; }

    void analyse()
    {
        // Окно: последние detW + detTauMax отсчётов (линейно благодаря зеркалу)
        const float* x = detBuf.data() + detWrite; // самый старый отсчёт
        const int W = detW;

        float energy = 0.0f;
        for (int j = 0; j < W + detTauMax; ++j) energy += x[j] * x[j];
        const float rms = std::sqrt (energy / (float) (W + detTauMax));

        // YIN: разностная функция + кумулятивная нормировка
        diff[0] = 1.0f;
        float running = 0.0f;
        int tauFound = -1;
        for (int tau = 1; tau <= detTauMax; ++tau)
        {
            float d = 0.0f;
            for (int j = 0; j < W; ++j)
            {
                const float e = x[j] - x[j + tau];
                d += e * e;
            }
            running += d;
            diff[(size_t) tau] = running > 0.0f ? d * (float) tau / running : 1.0f;
        }
        const float thr = voiced ? 0.25f : 0.15f;
        for (int tau = detTauMin; tau < detTauMax; ++tau)
            if (diff[(size_t) tau] < thr)
            {
                while (tau + 1 < detTauMax && diff[(size_t) tau + 1] < diff[(size_t) tau]) ++tau;
                tauFound = tau;
                break;
            }

        const float hopSec = (float) (detHop / detSr);
        if (tauFound < 0 || rms < 0.0015f)
        {
            if (voiced) { voiced = false; voicedTime = 0.0f; }
            detectedMidi = -1.0f;
            targetMidi = -1.0f;
            ratio = 1.0f;
            return;
        }

        // параболическая интерполяция
        float t = (float) tauFound;
        if (tauFound > 1 && tauFound < detTauMax)
        {
            const float a = diff[(size_t) tauFound - 1], b = diff[(size_t) tauFound], c = diff[(size_t) tauFound + 1];
            const float den = a - 2.0f * b + c;
            if (std::abs (den) > 1e-9f) t += 0.5f * (a - c) / den;
        }
        float period = t * (float) decim;

        // медиана из трёх: убирает одиночные срывы на октаву
        periodHist[2] = periodHist[1]; periodHist[1] = periodHist[0]; periodHist[0] = period;
        if (voiced && periodHist[2] > 0.0f)
        {
            float a = periodHist[0], b = periodHist[1], c = periodHist[2];
            period = std::max (std::min (a, b), std::min (std::max (a, b), c));
        }
        period = std::clamp (period, 8.0f, (float) tMax);

        if (! voiced)
        {
            voiced = true;
            corr = 0.0f;
            curTarget = -1;
            voicedTime = 0.0f;
        }
        curPeriod = period;
        voicedTime += hopSec;

        const float midi = 69.0f + 12.0f * std::log2 ((float) sr / period / 440.0f);
        detectedMidi = midi;

        // выбор целевой ноты с небольшим гистерезисом (без дребезга между соседними нотами)
        const int cand = nearestAllowed (midi, mask);
        const bool curOk = curTarget >= 0 && (mask & (1u << (curTarget % 12))) != 0;
        if (! curOk || (cand != curTarget && std::abs (midi - (float) cand) + 0.2f < std::abs (midi - (float) curTarget)))
            curTarget = cand;
        targetMidi = (float) curTarget;

        const float desired = ((float) curTarget - midi) * amount;

        // скорость подстройки; humanize смягчает самое начало ноты
        float speed = retuneMs;
        if (humanize > 0.0f && voicedTime < 0.25f)
            speed += humanize * 120.0f * (1.0f - voicedTime / 0.25f);

        if (speed <= 0.5f)
            corr = desired;
        else
            corr += (desired - corr) * (1.0f - std::exp (-hopSec * 1000.0f / speed));

        ratio = std::clamp (std::pow (2.0f, corr / 12.0f), 0.5f, 2.0f);
    }

    double sr = 44100.0;
    int tMax = 700, unvoicedHalf = 220, latency = 2102;
    int64_t bufMask = 0, now = 0;
    std::vector<float> in[maxChannels], acc[maxChannels], wacc;

    // анализ
    int decim = 2, detW = 551, detTauMax = 340, detTauMin = 20, detHop = 88, detLen = 900;
    double detSr = 22050.0;
    std::vector<float> detBuf, diff;
    int detWrite = 0, detCount = 0, decimPhase = 0;
    Biquad lp1, lp2, hp;

    bool voiced = false;
    float curPeriod = 300.0f, ratio = 1.0f, corr = 0.0f, voicedTime = 0.0f;
    float periodHist[3] {};
    int curTarget = -1;

    std::array<Mark, 512> markRing {};
    int markHead = 0, markCount = 0;
    double lastMark = 0.0, nextSynth = 0.0;
};

} // namespace yt
