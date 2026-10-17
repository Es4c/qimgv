#include "randomizer.h"

Randomizer::Randomizer() : currentIndex(0), rng(std::chrono::steady_clock::now().time_since_epoch().count()) {
    setCount(0);
}

Randomizer::Randomizer(size_t _count) : currentIndex(0), rng(std::chrono::steady_clock::now().time_since_epoch().count()) {
    setCount(_count);
}

void Randomizer::setCount(size_t _count) {
    vec.resize(_count);
    indexMap.resize(_count);
    fill();
}

void Randomizer::shuffle() {
    std::shuffle(vec.begin(), vec.end(), rng);
    for(size_t i = 0; i < vec.size(); ++i)
        indexMap[vec[i]] = i;
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
    // re-shuffle when needed
    // because vector gets rearranged this will break prev()
    if(currentIndex >= vec.size() - 1) {
        const size_t currentItem = vec[currentIndex];
        shuffle();
        setCurrent(currentItem);   // 新一圈：游标重新落在当前项上
        // 从当前项的下一位继续；当前项恰好落在末尾时绕回起点（原先 currentIndex++ 会读越界）
        currentIndex = (currentIndex + 1) % vec.size();
    } else {
        ++currentIndex;
    }
    return vec[currentIndex];
}

size_t Randomizer::prev() {
    if(vec.empty()) return 0;
    if(currentIndex == 0) {
        const size_t currentItem = vec[currentIndex];
        shuffle();
        setCurrent(currentItem);   // 新一圈：游标重新落在当前项上
        // 当前项落在起点时，前一项是末尾（原先 currentIndex-- 会下溢成 size_t(-1)）
        currentIndex = (currentIndex + vec.size() - 1) % vec.size();
    } else {
        --currentIndex;
    }
    return vec[currentIndex];
}
