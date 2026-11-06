#pragma once

// Ядро звука TalkBox. Без JUCE, чтобы можно было проверять отдельно.
//
// Как работает толкбокс в железе: синтезатор играет в трубку, трубка во рту,
// рот формирует гласные, микрофон снимает результат. Здесь то же самое в цифре:
//   1) из голоса каждые ~2.5 мс вычисляется форма «рта» (LPC, линейное предсказание),
//   2) синтезатор (или внешний синт через сайдчейн) пропускается через этот фильтр.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace tb
{
constexpr double kPi = 3.14159265358979323846;
constexpr int kMaxOrder = 48;
constexpr float kPreEmph = 0.97f;

//==============================================================================
// Формантный фильтр: анализ голоса (LPC) + решётчатый всеполюсный фильтр для несущей.
class FormantVocoder
{
public:
    void prepare (double sampleRate)
    {
        sr = sampleRate;
        order = std::clamp ((int) std::lround (sr / 2000.0), 12, kMaxOrder);
        winLen = (int) (sr * 0.032);
        hop = std::max (16, (int) (sr / 400.0));

        ring.assign ((size_t) winLen, 0.0f);
        frame.assign ((size_t) winLen, 0.0);
        window.resize ((size_t) winLen);
        winEnergy = 0.0;
        for (int i = 0; i < winLen; ++i)
        {
            window[(size_t) i] = 0.5 - 0.5 * std::cos (2.0 * kPi * (i + 0.5) / winLen);
            winEnergy += window[(size_t) i] * window[(size_t) i];
        }

        // Лаг-окно: расширяет полосы формант, чтобы фильтр не «звенел»
        const double bw = 50.0;
        for (int i = 0; i <= kMaxOrder; ++i)
        {
            const double a = 2.0 * kPi * bw * i / sr;
            lagWin[(size_t) i] = std::exp (-0.5 * a * a);
        }
        reset();
    }

    void reset()
    {
        std::fill (ring.begin(), ring.end(), 0.0f);
        writePos = 0;
        hopCount = 0;
        k.fill (0.0); kTarget.fill (0.0); kStep.fill (0.0); b.fill (0.0);
        gain = gainTarget = gainStep = 0.0;
        preVoice = 0.0f;
        deEmph = 0.0;
    }

    // Задержка, которую надо скомпенсировать у несущей и сухого сигнала
    int getLatency() const { return winLen / 2 + hop / 2; }

    // voice: голос (моно), carrier: уже «выбеленная» несущая (≈ единичная мощность).
    float process (float voice, float carrier)
    {
        const float pe = voice - kPreEmph * preVoice;
        preVoice = voice;
        ring[(size_t) writePos] = pe;
        if (++writePos >= winLen) writePos = 0;

        if (++hopCount >= hop)
        {
            hopCount = 0;
            analyse();
            for (int i = 1; i <= order; ++i)
                kStep[(size_t) i] = (kTarget[(size_t) i] - k[(size_t) i]) / hop;
            gainStep = (gainTarget - gain) / hop;
        }

        for (int i = 1; i <= order; ++i) k[(size_t) i] += kStep[(size_t) i];
        gain += gainStep;

        // Решётчатый синтез-фильтр 1/A(z): устойчив, пока |k| < 1, и его можно
        // плавно менять от отсчёта к отсчёту без щелчков.
        double f = carrier * gain;
        for (int i = order; i >= 1; --i)
        {
            f -= k[(size_t) i] * b[(size_t) i - 1];
            b[(size_t) i] = b[(size_t) i - 1] + k[(size_t) i] * f;
        }
        b[0] = f;
        if (! std::isfinite (f)) { b.fill (0.0); f = 0.0; }

        // Де-эмфазис возвращает естественный баланс низа и верха голоса
        deEmph = f + kPreEmph * deEmph;
        return (float) deEmph;
    }

    int getOrder() const { return order; }

private:
    void analyse()
    {
        // Окно по последним winLen отсчётам (от старых к новым)
        int idx = writePos;
        for (int i = 0; i < winLen; ++i)
        {
            frame[(size_t) i] = ring[(size_t) idx] * window[(size_t) i];
            if (++idx >= winLen) idx = 0;
        }

        std::array<double, kMaxOrder + 1> r {};
        for (int lag = 0; lag <= order; ++lag)
        {
            double acc = 0.0;
            for (int i = lag; i < winLen; ++i)
                acc += frame[(size_t) i] * frame[(size_t) (i - lag)];
            r[(size_t) lag] = acc * lagWin[(size_t) lag];
        }

        if (r[0] < 1.0e-10 * winEnergy)
        {
            kTarget.fill (0.0);
            gainTarget = 0.0;
            return;
        }
        r[0] *= 1.0 + 1.0e-4; // белый шум для численной устойчивости

        // Левинсон-Дурбин
        std::array<double, kMaxOrder + 1> a {}, tmp {};
        a[0] = 1.0;
        double err = r[0];
        for (int i = 1; i <= order; ++i)
        {
            double acc = r[(size_t) i];
            for (int j = 1; j < i; ++j) acc += a[(size_t) j] * r[(size_t) (i - j)];
            double ki = -acc / err;
            ki = std::clamp (ki, -0.999, 0.999);
            tmp = a;
            for (int j = 1; j < i; ++j) a[(size_t) j] = tmp[(size_t) j] + ki * tmp[(size_t) (i - j)];
            a[(size_t) i] = ki;
            kTarget[(size_t) i] = ki;
            err *= (1.0 - ki * ki);
        }
        gainTarget = std::sqrt (std::max (0.0, err) / winEnergy);
    }

    double sr = 44100.0;
    int order = 22, winLen = 1411, hop = 110;
    std::vector<float> ring;
    std::vector<double> frame, window;
    double winEnergy = 1.0;
    std::array<double, kMaxOrder + 1> lagWin {};
    int writePos = 0, hopCount = 0;

    std::array<double, kMaxOrder + 1> k {}, kTarget {}, kStep {}, b {};
    double gain = 0.0, gainTarget = 0.0, gainStep = 0.0, deEmph = 0.0;
    float preVoice = 0.0f;
};

//==============================================================================
// Определение высоты голоса (YIN) на прореженном сигнале ~11 кГц.
class PitchTracker
{
public:
    void prepare (double sampleRate)
    {
        decim = std::max (1, (int) std::lround (sampleRate / 11025.0));
        dsr = sampleRate / decim;
        W = (int) (dsr * 0.035);
        maxLag = (int) (dsr / 60.0);
        minLag = std::max (2, (int) (dsr / 1100.0));
        bufLen = W + maxLag;
        buf.assign ((size_t) bufLen, 0.0f);
        diff.assign ((size_t) maxLag + 2, 0.0f);
        hopD = std::max (8, W / 4);
        lpCoef = (float) std::exp (-2.0 * kPi * (dsr * 0.4) / sampleRate);
        reset();
    }

    void reset()
    {
        std::fill (buf.begin(), buf.end(), 0.0f);
        pos = 0; decCount = 0; hopCount = 0; acc = 0.0f; lp1 = lp2 = 0.0f;
        voiced = false; midiNote = 57.0f; level = 0.0f;
    }

    void push (float x)
    {
        lp1 = x + lpCoef * (lp1 - x);
        lp2 = lp1 + lpCoef * (lp2 - lp1);
        acc += lp2;
        if (++decCount < decim) return;
        const float y = acc / (float) decim;
        decCount = 0; acc = 0.0f;
        buf[(size_t) pos] = y;
        if (++pos >= bufLen) pos = 0;
        if (++hopCount >= hopD) { hopCount = 0; detect(); }
    }

    bool isVoiced() const { return voiced; }
    float getMidiNote() const { return midiNote; } // последняя найденная высота (держится в паузах)

private:
    float sample (int i) const // i = 0 — самый старый отсчёт окна
    {
        int j = pos + i;
        if (j >= bufLen) j -= bufLen;
        return buf[(size_t) j];
    }

    void detect()
    {
        double energy = 0.0;
        for (int i = 0; i < W; ++i) { const float s = sample (maxLag + i); energy += s * s; }
        level = (float) std::sqrt (energy / W);
        if (level < 1.0e-3f) { voiced = false; return; }

        // Разностная функция; «текущее» окно в конце буфера, сравниваем с прошлым
        diff[0] = 1.0f;
        double running = 0.0;
        for (int tau = 1; tau <= maxLag; ++tau)
        {
            double d = 0.0;
            for (int i = 0; i < W; ++i)
            {
                const float delta = sample (maxLag + i) - sample (maxLag + i - tau);
                d += delta * delta;
            }
            running += d;
            diff[(size_t) tau] = running > 0.0 ? (float) (d * tau / running) : 1.0f;
        }

        int best = -1;
        for (int tau = minLag; tau < maxLag; ++tau)
        {
            if (diff[(size_t) tau] < 0.15f)
            {
                while (tau + 1 < maxLag && diff[(size_t) tau + 1] < diff[(size_t) tau]) ++tau;
                best = tau;
                break;
            }
        }
        if (best < 0) { voiced = false; return; }

        // Параболическая интерполяция для точности
        double t = best;
        const double a = diff[(size_t) best - 1], c0 = diff[(size_t) best], c = diff[(size_t) best + 1];
        const double den = a - 2.0 * c0 + c;
        if (std::abs (den) > 1.0e-9) t += 0.5 * (a - c) / den;

        const double hz = dsr / t;
        midiNote = (float) (69.0 + 12.0 * std::log2 (hz / 440.0));
        voiced = true;
    }

    int decim = 4, W = 385, maxLag = 183, minLag = 10, bufLen = 568, hopD = 96;
    double dsr = 11025.0;
    std::vector<float> buf, diff;
    int pos = 0, decCount = 0, hopCount = 0;
    float acc = 0.0f, lp1 = 0.0f, lp2 = 0.0f, lpCoef = 0.5f;
    bool voiced = false;
    float midiNote = 57.0f, level = 0.0f;
};

//==============================================================================
// Гаммы для режима «Авто»
inline int nearestScaleNote (float note, int key, int scale)
{
    static const std::array<std::array<int, 12>, 6> masks { {
        { 1,1,1,1,1,1,1,1,1,1,1,1 }, // хроматика
        { 1,0,1,0,1,1,0,1,0,1,0,1 }, // мажор
        { 1,0,1,1,0,1,0,1,1,0,1,0 }, // минор
        { 1,0,0,1,0,1,0,1,0,0,1,0 }, // минорная пентатоника
        { 1,0,1,1,0,1,0,1,0,1,1,0 }, // дорийский
        { 1,0,0,1,0,1,1,1,0,0,1,0 }, // блюз
    } };
    const auto& m = masks[(size_t) std::clamp (scale, 0, 5)];
    const int base = (int) std::lround (note);
    int best = base;
    float bestDist = 1.0e9f;
    for (int n = base - 6; n <= base + 6; ++n)
    {
        const int pc = ((n - key) % 12 + 12) % 12;
        if (! m[(size_t) pc]) continue;
        const float d = std::abs ((float) n - note);
        if (d < bestDist) { bestDist = d; best = n; }
    }
    return best;
}

//==============================================================================
// Голос синтезатора: две пилы с расстройкой + меандр, сглаженные PolyBLEP.
// Выход уже «выбелен» (пре-эмфазис + нормировка мощности), чтобы громкость
// результата повторяла громкость голоса и не зависела от высоты ноты.
class SynthVoice
{
public:
    void prepare (double sampleRate)
    {
        sr = sampleRate;
        powCoef = (float) std::exp (-1.0 / (0.03 * sr));
        envAtt = (float) (1.0 - std::exp (-1.0 / (0.004 * sr)));
        envRel = (float) (1.0 - std::exp (-1.0 / (0.08 * sr)));
        active = false; env = 0.0f;
    }

    // legato = true: если голос уже звучит, плавно доехать до новой ноты (глайд)
    void noteOn (int note, float velocity, bool legato, uint32_t seed)
    {
        targetNote = (float) note;
        if (! active || ! legato)
        {
            if (! active) { p1 = 0.0; p2 = (seed % 1000) / 1000.0; p3 = 0.0; prev = 0.0f; }
            curNote = targetNote;
            powEst = estimatePower (noteToHz (curNote));
        }
        midiNote = note;
        vel = 0.4f + 0.6f * velocity;
        gate = true;
        active = true;
    }

    void noteOff() { gate = false; }
    void setTarget (float note) { targetNote = note; }
    void kill() { active = false; gate = false; env = 0.0f; }

    bool isActive() const { return active; }
    bool isHeld() const { return gate; }
    int getNote() const { return midiNote; }

    // glideCoef: 0 = мгновенно; wave: 0 пила … 1 меандр; detune в центах
    float render (float glideCoef, float wave, float detuneCents)
    {
        if (! active) return 0.0f;

        curNote = targetNote + glideCoef * (curNote - targetNote);
        const double hz = noteToHz (curNote);
        const double det = std::pow (2.0, detuneCents / 1200.0);
        const double dt1 = hz / sr, dt2 = hz * det / sr, dt3 = hz / det / sr;

        const float saw1 = (float) (2.0 * p1 - 1.0 - polyBlep (p1, dt1));
        const float saw2 = (float) (2.0 * p2 - 1.0 - polyBlep (p2, dt2));
        double p3b = p3 + 0.5; if (p3b >= 1.0) p3b -= 1.0;
        const float sq = (float) ((p3 < 0.5 ? 1.0 : -1.0) + polyBlep (p3, dt3) - polyBlep (p3b, dt3));

        p1 += dt1; if (p1 >= 1.0) p1 -= 1.0;
        p2 += dt2; if (p2 >= 1.0) p2 -= 1.0;
        p3 += dt3; if (p3 >= 1.0) p3 -= 1.0;

        const float saws = detuneCents > 0.01f ? 0.5f * (saw1 + saw2) : saw1;
        const float raw = (1.0f - wave) * saws + wave * sq;

        // Пре-эмфазис как у анализа голоса + нормировка мощности
        const float pe = raw - kPreEmph * prev;
        prev = raw;
        powEst = pe * pe + powCoef * (powEst - pe * pe);
        const float norm = pe / std::sqrt (powEst + 1.0e-12f);

        env += (gate ? 1.0f - env : -env) * (gate ? envAtt : envRel);
        if (! gate && env < 1.0e-4f) { active = false; env = 0.0f; }
        return std::clamp (norm, -8.0f, 8.0f) * env * vel;
    }

private:
    static double noteToHz (float n) { return 440.0 * std::pow (2.0, (n - 69.0) / 12.0); }

    static double polyBlep (double t, double dt)
    {
        if (t < dt) { t /= dt; return t + t - t * t - 1.0; }
        if (t > 1.0 - dt) { t = (t - 1.0) / dt; return t * t + t + t + 1.0; }
        return 0.0;
    }

    // Мощность пилы после пре-эмфазиса (чтобы нормировка не давала выброса в начале ноты)
    float estimatePower (double hz) const
    {
        const int nh = std::max (1, (int) (sr * 0.5 / hz));
        double p = 0.0;
        for (int n = 1; n <= std::min (nh, 2000); ++n)
        {
            const double w = 2.0 * kPi * hz * n / sr;
            const double h2 = 1.0 + kPreEmph * kPreEmph - 2.0 * kPreEmph * std::cos (w);
            const double an = 2.0 / (kPi * n);
            p += 0.5 * an * an * h2;
        }
        return (float) p;
    }

    double sr = 44100.0, p1 = 0.0, p2 = 0.0, p3 = 0.0;
    float curNote = 60.0f, targetNote = 60.0f, prev = 0.0f, powEst = 1.0f, powCoef = 0.999f;
    float env = 0.0f, envAtt = 0.01f, envRel = 0.001f, vel = 1.0f;
    int midiNote = 60;
    bool active = false, gate = false;
};

} // namespace tb
