#include <gtest/gtest.h>
#include "ecu/TuneValidation.hpp"

#include <QString>

using ecu::ValidationResult;
using ecu::ValidationStatus;
using ecu::SessionRecorder;
using ecu::StatusHysteresis;
using ecu::EmaFilter;
using ecu::TuneValidator;

TEST(TuneValidation, ClassifyDeltaMbar) {
    EXPECT_EQ(TuneValidator::classifyDelta(10.0, 50.0, "mbar"), ValidationStatus::Ok);
    EXPECT_EQ(TuneValidator::classifyDelta(75.0, 50.0, "mbar"), ValidationStatus::Warn);
    EXPECT_EQ(TuneValidator::classifyDelta(120.0, 50.0, "mbar"), ValidationStatus::Fail);
}

TEST(TuneValidation, DefaultMeasureForCategory) {
    EXPECT_EQ(TuneValidator::defaultMeasureForCategory("boost"),
              ecu::MeasureKind::MapAbsMbar);
    EXPECT_EQ(TuneValidator::defaultMeasurePid("air"), 0x10);
    EXPECT_EQ(TuneValidator::defaultMeasurePid("fuel"), 0x23);
    EXPECT_EQ(TuneValidator::defaultMeasurePid("smoke"), 0x24);
}

TEST(TuneValidation, MapAbsKpaToMbarNoDoubleBaro) {
    // Ralenti typique : MAP ≈ 100 kPa abs → 1000 mbar (pas 2010 avec +baro).
    EXPECT_DOUBLE_EQ(TuneValidator::mapAbsKpaToMbar(100.0), 1000.0);
    EXPECT_DOUBLE_EQ(TuneValidator::mapAbsKpaToMbar(250.0), 2500.0);
}

TEST(StatusHysteresis, FailThenRecover) {
    StatusHysteresis h;
    h.setFailThreshold(3);
    h.setOkThreshold(3);

    EXPECT_EQ(h.update(ValidationStatus::Fail), ValidationStatus::Warn);
    EXPECT_EQ(h.update(ValidationStatus::Fail), ValidationStatus::Warn);
    EXPECT_EQ(h.update(ValidationStatus::Fail), ValidationStatus::Fail);
    EXPECT_EQ(h.failStreak(), 3);

    EXPECT_EQ(h.update(ValidationStatus::Ok), ValidationStatus::Fail);
    EXPECT_EQ(h.update(ValidationStatus::Ok), ValidationStatus::Fail);
    EXPECT_EQ(h.update(ValidationStatus::Ok), ValidationStatus::Ok);
}

TEST(EmaFilter, Smooths) {
    EmaFilter e;
    e.setAlpha(0.5);
    EXPECT_DOUBLE_EQ(e.push(100.0), 100.0);
    EXPECT_NEAR(e.push(0.0), 50.0, 1e-9);
}

TEST(SessionRecorder, AggregatesAndHotspots) {
    SessionRecorder rec;
    rec.start(QStringLiteral("edc16c34"), QStringLiteral("deadbeef"));

    ValidationResult ok;
    ok.mapName = QStringLiteral("pAirBas");
    ok.category = QStringLiteral("boost");
    ok.status = ValidationStatus::Ok;
    ok.delta = 10.0;
    ok.ix0 = 3;
    ok.iy0 = 5;

    ValidationResult fail = ok;
    fail.status = ValidationStatus::Fail;
    fail.delta = -200.0;
    fail.ix0 = 3;
    fail.iy0 = 5;

    rec.ingest({ok});
    rec.ingest({fail});
    rec.ingest({fail});
    rec.setCsvPath(QStringLiteral("/tmp/drive_test.csv"));

    const auto sum = rec.finish();
    EXPECT_EQ(sum.ticks, 3);
    EXPECT_EQ(sum.ok, 1);
    EXPECT_EQ(sum.fail, 2);
    EXPECT_GT(sum.peakAbsDelta, 100.0);
    EXPECT_FALSE(sum.hotspots.empty());
    EXPECT_EQ(sum.hotspots.front().gx, 3);
    EXPECT_EQ(sum.hotspots.front().gy, 5);
    EXPECT_EQ(sum.hotspots.front().count, 3);
    EXPECT_NEAR(sum.okRatio(), 100.0 * 1.0 / 3.0, 0.01);
    EXPECT_EQ(sum.csvPath, QStringLiteral("/tmp/drive_test.csv"));
}

// Replay CSV minimal — parseur de statut (comme ObdPanel::replayValidationCsv).
TEST(SessionReplayCsv, CountStatuses) {
    const QString csv =
        "time,map,measured,expected,delta,unit,status,rpm,load\n"
        "t,pAir,100,100,0,mbar,OK,2000,40\n"
        "t,pAir,80,100,-20,mbar,Attention,2100,50\n"
        "t,pAir,50,100,-50,mbar,Écart,2200,60\n";

    int ok = 0, warn = 0, fail = 0, total = 0;
    const QStringList lines = csv.split(QLatin1Char('\n'));
    for (int i = 1; i < lines.size(); ++i) {
        if (lines[i].trimmed().isEmpty()) continue;
        const QStringList cols = lines[i].split(QLatin1Char(','));
        if (cols.size() < 7) continue;
        ++total;
        const QString st = cols[6].trimmed();
        if (st == QStringLiteral("OK")) ++ok;
        else if (st == QStringLiteral("Attention")) ++warn;
        else if (st == QStringLiteral("Écart")) ++fail;
    }
    EXPECT_EQ(total, 3);
    EXPECT_EQ(ok, 1);
    EXPECT_EQ(warn, 1);
    EXPECT_EQ(fail, 1);
}
