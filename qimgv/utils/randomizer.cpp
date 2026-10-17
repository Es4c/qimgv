#include "randomizer.h"

Randomizer::Randomizer() : currentIndex(0), remainingInRound(0), rng(std::chrono::steady_clock::now().time_since_epoch().count()) {
    setCount(0);
}

Randomizer::Randomizer(size_t _count) : currentIndex(0), remainingInRound(0), rng(std::chrono::steady_clock::now().time_since_epoch().count()) {
    setCount(_count);
}

void Randomizer::setCount(size_t _count) {
    vec.resize(_count);
    indexMap.resize(_count);
    fill();
    // ⭐ 列表缩小时旧游标可能越出新范围（如目录重载时当前文件已被删除，
    // setCurrent() 的保位逻辑会保留旧游标）：就地钳到合法区间，
    // 否则 next()/prev() 会用越界游标读 vec
    if(currentIndex >= _count)
        currentIndex = _count > 0 ? _count - 1 : 0;
    remainingInRound = _count > 0 ? _count - 1 : 0;
}

void Randomizer::shuffle() {
    std::shuffle(vec.begin(), vec.end(), rng);
    for(size_t i = 0; i < vec.size(); ++i)
        indexMap[vec[i]] = i;
    remainingInRound = vec.size() > 0 ? vec.size() - 1 : 0;
}

void Randomizer::setCurrent(size_t _current) {
    // ⭐ 下标无效时（如 indexOfFile() 返回 -1 被转换、条目已被删除）保持当前位置：
    // 旧实现会存入 size_t(-1)，随后 next()/prev() 用该下标访问 vec，属越界读
    const size_t pos = indexOf(_current);
    if(pos != size_t(-1))
        currentIndex = pos;
}

size_t Randomizer::indexOf(size_t item) {
    if(item >= indexMap.size())
        return size_t(-1);
    return indexMap[item];
}

bool Randomizer::remap(const std::vector<size_t> &sigma) {
    const size_t count = vec.size();
    if(sigma.size() != count)
        return false;

    // 先校验并写入临时置换：失败时不留下半更新的状态
    std::vector<size_t> remapped(count);
    std::vector<char> seen(count, 0);
    for(size_t i = 0; i < count; ++i) {
        const size_t oldIndex = vec[i];
        if(oldIndex >= count)
            return false;
        const size_t newIndex = sigma[oldIndex];
        if(newIndex >= count || seen[newIndex] != 0)
            return false;
        seen[newIndex] = 1;
        remapped[i] = newIndex;
    }

    vec.swap(remapped);
    for(size_t i = 0; i < count; ++i)
        indexMap[vec[i]] = i;
    return true;
}

void Randomizer::fill() {
    for(size_t i = 0; i < vec.size(); ++i) {
        vec[i] = i;
        indexMap[i] = i;
    }
}

size_t Randomizer::next() {
    if(vec.empty()) return 0;
    // ⭐ 每圈以「当前项」为锚恰好走 size-1 步（其余每张各出现一次），走完才重洗：
    // 重洗后锚点可能落在置换中部，裸递增走到末尾会跳过锚点之前的项（提前重洗）、
    // 而越过末尾则越界，故圈内也用取模绕行，由 remainingInRound 决定何时开新圈
    if(remainingInRound == 0)
        startNewRound();
    if(remainingInRound > 0)
        --remainingInRound;
    currentIndex = (currentIndex + 1) % vec.size();
    return vec[currentIndex];
}

size_t Randomizer::prev() {
    if(vec.empty()) return 0;
    if(remainingInRound == 0)
        startNewRound();
    if(remainingInRound > 0)
        --remainingInRound;
    currentIndex = (currentIndex + vec.size() - 1) % vec.size();
    return vec[currentIndex];
}

// 开新一圈：重洗后把游标锚回当前项，本圈从它的下一位（或前一位）开始
void Randomizer::startNewRound() {
    const size_t currentItem = vec[currentIndex];
    shuffle();
    setCurrent(currentItem);
}
