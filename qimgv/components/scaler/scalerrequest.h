#ifndef SCALERREQUEST_H
#define SCALERREQUEST_H

#include <QSize>
#include <QString>
#include <memory>
#include "sourcecontainers/image.h"
#include "settings.h"

class ScalerRequest {
public:
    ScalerRequest() noexcept
        : m_filter(QI_FILTER_BILINEAR)
    {}

    ScalerRequest(std::shared_ptr<Image> image,
                  QSize size,
                  QString path,
                  ScalingFilter filter) noexcept
        : m_image(std::move(image))
        , m_size(size)
        , m_path(std::move(path))
        , m_filter(filter)
    {}

    // ✅ 保持默认语义（Qt6 已优化）
    ScalerRequest(const ScalerRequest&) = default;
    ScalerRequest(ScalerRequest&&) noexcept = default;
    ScalerRequest& operator=(const ScalerRequest&) = default;
    ScalerRequest& operator=(ScalerRequest&&) noexcept = default;

    // 🚀 代数：requestScaled 时递增打戳，用于排队中任务启动前的陈旧性自检
    [[nodiscard]] quint64 generation() const noexcept { return m_generation; }
    void setGeneration(quint64 g) noexcept { m_generation = g; }

    // 🚀 高频路径：全部返回引用
    [[nodiscard]] const std::shared_ptr<Image>& imageRef() const noexcept {
        return m_image;
    }

    [[nodiscard]] const QSize& sizeRef() const noexcept {
        return m_size;
    }

    [[nodiscard]] const QString& pathRef() const noexcept {
        return m_path;
    }

    [[nodiscard]] ScalingFilter filter() const noexcept {
        return m_filter;
    }

    // 🚀 指针比较避免字符串比较（非常关键）；代数不参与相等比较
    bool operator==(const ScalerRequest& other) const noexcept {
        return m_image.get() == other.m_image.get() &&
               m_size == other.m_size &&
               m_filter == other.m_filter;
    }

    bool operator!=(const ScalerRequest& other) const noexcept {
        return !(*this == other);
    }

private:
    std::shared_ptr<Image> m_image;
    QSize m_size;
    QString m_path;
    ScalingFilter m_filter;
    quint64 m_generation = 0;
};

// Qt 元类型（仍然安全）
Q_DECLARE_METATYPE(ScalerRequest)

#endif // SCALERREQUEST_H