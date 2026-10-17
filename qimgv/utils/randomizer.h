#pragma once

#include <algorithm>
#include <chrono>
#include <random>
#include <vector>

class Randomizer {
public:
    Randomizer();
    Randomizer(size_t _count);

    void setCount(size_t _count);
    size_t next();
    size_t prev();

    void shuffle();
    void setCurrent(size_t _current);
    // sigma[旧下标] = 新下标：把置换重映射到新的下标空间，保持每个位置对应的“项”不变
    //（游标即位置，因此无需改动）。长度不匹配或 sigma 不是一一映射时返回 false，
    // 此时原状态不变，调用方应改用 setCount() + shuffle() 重建。
    bool remap(const std::vector<size_t> &sigma);
private:
    size_t currentIndex;
    // 当前圈剩余步数：以当前项为锚，一圈恰好把其余每张各走一次，走完才重洗
    size_t remainingInRound;
    std::vector<size_t> vec;
    std::vector<size_t> indexMap;  // indexMap[value] = position in vec
    std::mt19937 rng;
    void fill();
    void startNewRound();
    size_t indexOf(size_t item);
};
