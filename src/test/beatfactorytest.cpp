#include <gtest/gtest.h>

#include <QHash>

#include "track/beatfactory.h"

namespace {

TEST(BeatFactoryTest, ParseSubVersionRoundTrip) {
    QHash<QString, QString> info;
    info.insert("vamp_plugin_id", "mixxxlarocheswingbeats:0");
    info.insert("fast_analysis", "1");
    info.insert("swing_pct", "12.50");

    const QString encoded = BeatFactory::getPreferredSubVersion(info);
    const auto decoded = BeatFactory::parseSubVersion(encoded);

    EXPECT_EQ(decoded.value("vamp_plugin_id"), QString("mixxxlarocheswingbeats:0"));
    EXPECT_EQ(decoded.value("fast_analysis"), QString("1"));
    EXPECT_EQ(decoded.value("swing_pct"), QString("12.50"));
    // getPreferredSubVersion appends a rounding marker.
    EXPECT_EQ(decoded.value("rounding"), QString("V4"));
}

TEST(BeatFactoryTest, ParseSubVersionIgnoresMalformedFragments) {
    const auto decoded = BeatFactory::parseSubVersion(
            QStringLiteral("foo=bar|malformed|=emptykey|swing_pct=18.0"));

    EXPECT_EQ(decoded.value("foo"), QString("bar"));
    EXPECT_EQ(decoded.value("swing_pct"), QString("18.0"));
    EXPECT_FALSE(decoded.contains("malformed"));
    EXPECT_FALSE(decoded.contains(QString()));
}

} // namespace
