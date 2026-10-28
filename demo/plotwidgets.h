#pragma once
#include <QWidget>
#include <QImage>
#include <QPointF>
#include "paleo_data/service.h"

class QToolButton;

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
    // 一个像素对应一个返回的振幅；不再把原始矩阵缩减到 1800 像素。
    QSize nativeImageSize() const { return image_.size(); }
    const QImage& nativeImage() const { return image_; }
    bool exportImage(const QString& path, QString& error) const;
    // 墙是带坐标的投影图；精确的矩阵仍通过 exportImage 导出。
    bool exportWallImage(const QString& path, QString& error, QSize size=QSize(4096,3072)) const;
    void fitToWindow();
    void actualPixels();
    void setWallMode(bool enabled);
    bool wallMode() const { return wallMode_; }
    bool hasWallGeometry() const { return wallPoints_.size()>=2; }
protected:
    void paintEvent(QPaintEvent*) override;
    void resizeEvent(QResizeEvent*) override;
    void wheelEvent(QWheelEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void mouseDoubleClickEvent(QMouseEvent*) override;
    void contextMenuEvent(QContextMenuEvent*) override;
private:
    QRectF plotArea() const;
    QRectF imageTarget(const QRectF& area) const;
    void zoomAt(double factor, const QPointF& position);
    void drawWall(QPainter& painter, const QRectF& area, double zoom=1, QPointF pan={}) const;
    void chooseExport(bool wall);
    void updateControls();
    QImage image_;
    QString caption_;
    bool sectionMode_ = false; // 道方向与时间方向单位不同，剖面填充绘图区而非按像素等比例缩小。
    bool fit_=true, wallMode_=false, dragging_=false;
    double zoom_=1, wallAngle_=30;
    QPointF pan_, lastMouse_;
    std::vector<QPointF> wallPoints_;
    std::vector<int> wallColumns_;
    std::vector<QPointF> wallSamples_; // 每个返回列的请求坐标；保留非均匀间距和折点。
    double firstTime_=0, lastTime_=0;
    QString timeUnit_;
    QWidget* toolbar_=nullptr;
    QToolButton* wallButton_=nullptr;
    QToolButton* rotateButton_=nullptr;
    QToolButton* exportButton_=nullptr;
};
