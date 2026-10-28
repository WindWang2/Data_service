#pragma once
#include <QWidget>
#include <QImage>
#include "paleo_data/service.h"

class CurvePlot final : public QWidget {
public:
    explicit CurvePlot(QWidget* parent=nullptr);
    void setCurve(const paleo::data::CurveSlice& curve);
    void clear();
protected:
    void paintEvent(QPaintEvent*) override;
private:
    paleo::data::CurveSlice curve_;
};

class ImagePreview final : public QWidget {
public:
    explicit ImagePreview(QWidget* parent=nullptr);
    bool load(const QString& path, QString& error);
    void clear();
    void setSection(const paleo::data::SeismicSectionResult& section);
    void setTimeSlice(const paleo::data::SeismicTimeSliceResult& slice);
protected:
    void paintEvent(QPaintEvent*) override;
private:
    QImage image_;
    QString caption_;
    bool sectionMode_ = false; // 道方向与时间方向单位不同，剖面填充绘图区而非按像素等比例缩小。
};
