#pragma once

// Цели каждого режима при СИЛА = 100 %.
// При СИЛА = 0 % всё плавно сходится к нейтрали (сигнал проходит без изменений,
// кроме защитного лимитера на -0.3 dBFS).
// Это не копия чужих мастеров, а интерпретация звукового характера:
// «фонящий размашистый хип-хоп», где всё искажено, шипит и качается,
// плюс цифровая грязь в духе дигикора.

struct PrinceSettings
{
    const char* name;

    // EQ
    float hpfHz;
    float lowShelfHz, lowShelfDb;
    float mudHz, mudDb, mudQ;
    float midHz, midDb, midQ;          // «гнусавая» фонящая середина
    float highShelfHz, highShelfDb;

    // Качение (пампинг от бочки, как сайдчейн)
    float pumpDb;                      // максимальная глубина «ныряния» верха и середины
    float pumpReleaseMs;

    // Искажение (на 2x передискретизации, параллельно)
    float driveDb;
    float distAsym;                    // чётные гармоники, «перегруженная колонка»
    float distHard;                    // 0 = мягкий фузз, 1 = жёсткий клиппер
    float distMix;                     // доля искажённого сигнала

    // Цифра (дигикор): понижение разрядности и частоты
    float crushBits;
    float crushDownsample;
    float crushMix;

    // Склейка
    float compThreshDb, compRatio, compAttackMs, compReleaseMs;

    // Фон: шипение, гул, треск (идёт за громкостью трека)
    float noiseDb;                     // уровень шипения относительно 0 dBFS
    float humAmount;                   // доля гула 50 Гц
    float crackleAmount;               // доля треска

    // Пространство
    float width;
    float reverbSize, reverbDamp, reverbSend;

    float makeupDb;
};

// «ЗАВТРА» — фонящий, размашистый, тяжело перегруженный хип-хоп:
// толстый низ, гнусавая середина, пережатый фузз, много шипения и гула,
// огромный тёмный зал, широкое стерео, лёгкое качение.
static constexpr PrinceSettings kModeZavtra
{
    "ZAVTRA",
    28.0f,
    70.0f, 4.0f,
    320.0f, -2.5f, 1.0f,
    1600.0f, 2.0f, 0.8f,
    9000.0f, -1.5f,
    3.5f, 180.0f,
    20.0f, 0.30f, 0.35f, 0.60f,
    10.0f, 2.0f, 0.12f,
    -18.0f, 3.0f, 10.0f, 160.0f,
    -36.0f, 0.35f, 0.40f,
    1.30f,
    0.92f, 0.65f, 0.22f,
    6.0f
};

// «ONE MORE TIME» — дигикор / французский хаус:
// плотная бочка, сильное качение от сайдчейна, жёсткий цифровой клиппинг,
// заметная битовая грязь, яркий верх, ревер поменьше и посветлее.
static constexpr PrinceSettings kModeOneMoreTime
{
    "ONE MORE TIME",
    30.0f,
    60.0f, 3.5f,
    300.0f, -3.0f, 1.1f,
    3000.0f, 2.5f, 0.9f,
    10000.0f, 2.5f,
    8.0f, 120.0f,
    15.0f, 0.08f, 0.80f, 0.45f,
    8.0f, 4.0f, 0.22f,
    -20.0f, 4.0f, 5.0f, 100.0f,
    -42.0f, 0.10f, 0.15f,
    1.20f,
    0.75f, 0.40f, 0.14f,
    7.5f
};

static constexpr PrinceSettings kNeutral
{
    "NEUTRAL",
    20.0f,
    70.0f, 0.0f,
    320.0f, 0.0f, 1.0f,
    1600.0f, 0.0f, 0.8f,
    9000.0f, 0.0f,
    0.0f, 150.0f,
    0.0f, 0.0f, 0.0f, 0.0f,
    16.0f, 1.0f, 0.0f,
    0.0f, 1.0f, 10.0f, 100.0f,
    -120.0f, 0.0f, 0.0f,
    1.0f,
    0.5f, 0.5f, 0.0f,
    0.0f
};
