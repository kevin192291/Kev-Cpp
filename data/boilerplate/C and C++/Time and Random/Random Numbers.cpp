---
order: 20
description: randomInt(1, 6), randomReal(0, 1) and randomChance(0.25) on one seeded std::mt19937 per thread
includes: <random>
---
// One generator, seeded once and reused (seeding on every call gives poor, repeating numbers); one per
// thread, so threads don't share it. randomInt(1, 6) includes both ends; randomReal(a, b) never returns b.
std::mt19937& randomEngine()
{
    thread_local std::mt19937 engine{std::random_device{}()};
    return engine;
}

int randomInt(int low, int high)
{
    return std::uniform_int_distribution<int>(low, high)(randomEngine());
}

double randomReal(double low, double high)
{
    return std::uniform_real_distribution<double>(low, high)(randomEngine());
}

// True with the given probability: randomChance(0.25) is true about one time in four.
bool randomChance(double probability)
{
    return std::bernoulli_distribution(probability)(randomEngine());
}
