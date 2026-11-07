#pragma once
#include "third_party/qrcodegen.hpp"
#include <QImage>
#include <QByteArray>

inline QImage pairingQrImage(const QByteArray &payload) {
    const auto qr = qrcodegen::QrCode::encodeText(payload.constData(), qrcodegen::QrCode::Ecc::MEDIUM);
    constexpr int quiet = 4, scale = 6;
    const int side = (qr.getSize() + quiet * 2) * scale;
    QImage image(side, side, QImage::Format_RGB32);
    image.fill(Qt::white);
    for (int y = 0; y < qr.getSize(); ++y)
        for (int x = 0; x < qr.getSize(); ++x)
            if (qr.getModule(x, y))
                for (int dy = 0; dy < scale; ++dy)
                    for (int dx = 0; dx < scale; ++dx)
                        image.setPixel((x + quiet) * scale + dx, (y + quiet) * scale + dy, qRgb(0, 0, 0));
    return image;
}
