#pragma once

// Цели каждого стиля при Amount = 100 %.
// При Amount = 0 % всё плавно сходится к «нейтрали» (плоская АЧХ, без компрессии и эффектов).
// Это не копии чужих миксов, а интерпретация звукового характера.

struct StyleSettings
{
    const char* name;

    // EQ
    float hpfHz;
    float lowShelfHz, lowShelfDb;
    float midHz, midDb, midQ;
    float presHz, presDb, presQ;
    float highShelfHz, highShelfDb;
    float lpfHz;            // 20000 = не режем верх

    // Компрессор (склейка)
    float compThreshDb, compRatio, compAttackMs, compReleaseMs;

    // Сатурация
    float driveDb;          // усиление на входе сатуратора
    float satAsym;          // асимметрия (чётные гармоники, «ламповость/плёнка»)
    float satHard;          // 0 = мягкий tanh, 1 = жёсткий клиппер

    // Стерео и пространство
    float width;            // 1 = как есть, <1 уже, >1 шире
    float reverbSize, reverbDamp, reverbWet;
    float delayBeats, delayFeedback, delayWet, delayToneHz;

    float makeupDb;         // компенсация громкости
};

// «BULLY» — тёплый, грязный, «винтажный» соул-сэмпл звук:
// плотный низ, тёплая середина, приглушённый верх как с плёнки,
// плёночная сатурация с асимметрией, тугая склейка, короткая комната, чуть уже стерео.
static constexpr StyleSettings kStyleBully
{
    "BULLY",
    30.0f,
    90.0f, 3.5f,
    280.0f, 1.5f, 0.8f,
    3200.0f, -1.5f, 1.0f,
    10000.0f, -3.0f,
    13500.0f,
    -20.0f, 4.0f, 12.0f, 140.0f,
    9.0f, 0.35f, 0.15f,
    0.88f,
    0.35f, 0.75f, 0.10f,
    0.0f, 0.0f, 0.0f, 3000.0f,
    5.0f
};

// «MILITANTUM» — тёмный, агрессивный, «боевой» звук:
// тяжёлый саб, вычищенная грязь в низкой середине, жёсткая атака и яркий верх,
// жёсткий клиппинг, плотная агрессивная компрессия, широкое стерео,
// большой тёмный ревер и пинг-понг дилей в темп.
static constexpr StyleSettings kStyleMilitantum
{
    "MILITANTUM",
    25.0f,
    55.0f, 4.5f,
    420.0f, -3.5f, 1.1f,
    2600.0f, 2.5f, 0.9f,
    9000.0f, 2.0f,
    20000.0f,
    -24.0f, 6.0f, 3.0f, 70.0f,
    12.0f, 0.05f, 0.75f,
    1.35f,
    0.85f, 0.45f, 0.13f,
    0.75f, 0.35f, 0.09f, 2500.0f,
    7.0f
};

static constexpr StyleSettings kNeutral
{
    "NEUTRAL",
    20.0f,
    90.0f, 0.0f,
    300.0f, 0.0f, 0.8f,
    3000.0f, 0.0f, 1.0f,
    10000.0f, 0.0f,
    20000.0f,
    0.0f, 1.0f, 10.0f, 100.0f,
    0.0f, 0.0f, 0.0f,
    1.0f,
    0.5f, 0.5f, 0.0f,
    0.75f, 0.0f, 0.0f, 3000.0f,
    0.0f
};
