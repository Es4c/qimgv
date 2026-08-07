#pragma once

#include <QRunnable>
#include <QString>
#include <atomic>
#include <cstdint>
#include <memory>

class Loader;
class Image;

// 普通 QRunnable，不继承 QObject：省去每任务 QObject 元对象与信号机制的分配开销。
// 结果经 QMetaObject::invokeMethod(排队连接) 送回主线程。
// 生命周期由 shared_ptr 管理：self 引用保证排队/运行期间对象存活，
// run() 结束时释放（被取消的任务随之自毁），正常任务由结果事件持有的引用
// 维持到事件处理完，自动释放，避免与 run() 并发的直接 delete 造成 UAF。
class LoaderRunnable : public QRunnable {
public:
    explicit LoaderRunnable(Loader *loader, const QString &path);
    void run() override;

    // 位0=started（run() 开始时置位，用于区分排队中/运行中），位1=cancelled（clearTasks 取消排队中任务）
    // 打包进单个原子变量：避免多个 1 字节原子成员引入结构填充（-Wpadded 告警）
    void markStarted() noexcept { state.fetch_or(std::uint64_t{1}, std::memory_order_release); }
    bool started() const noexcept { return (state.load(std::memory_order_acquire) & std::uint64_t{1}) != 0; }
    void markCancelled() noexcept { state.fetch_or(std::uint64_t{2}, std::memory_order_release); }
    bool cancelled() const noexcept { return (state.load(std::memory_order_acquire) & std::uint64_t{2}) != 0; }

private:
    Loader *loader;
    QString path;

public:
    std::shared_ptr<LoaderRunnable> self; // 由 doLoadAsync 赋值，run() 结束时释放
    std::atomic<std::uint64_t> state{0};
};
