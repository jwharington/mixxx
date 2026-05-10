#include "library/dao/playlistdao.h"

#include <gtest/gtest.h>

#include <QSignalSpy>
#include <QTest>

#include "control/controlobject.h"
#include "control/controlpotmeter.h"
#include "control/controlpushbutton.h"
#include "library/playlisttablemodel.h"
#include "library/trackset/crate/crate.h"
#include "mixer/playerinfo.h"
#include "test/librarytest.h"
#include "track/track.h"

class PlaylistDaoTest : public LibraryTest {
};

namespace {

class ScopedPlayerInfo {
  public:
    ScopedPlayerInfo() {
        PlayerInfo::create();
    }

    ~ScopedPlayerInfo() {
        PlayerInfo::destroy();
    }
};

class ScopedPlaylistTableModelControls {
  public:
    ScopedPlaylistTableModelControls()
            : m_numDecks(ConfigKey("[App]", "num_decks")),
              m_numSamplers(ConfigKey("[App]", "num_samplers")),
              m_numPreviewDecks(ConfigKey("[App]", "num_preview_decks")),
              m_crossfader(ConfigKey("[Master]", "crossfader"), -1.0, 1.0),
              m_crossfaderReverse(ConfigKey("[Mixer Profile]", "xFaderReverse")) {
        m_numDecks.set(2.0);
        m_numSamplers.set(0.0);
        m_numPreviewDecks.set(0.0);
        m_crossfaderReverse.setButtonMode(mixxx::control::ButtonMode::Toggle);
    }

  private:
    ControlObject m_numDecks;
    ControlObject m_numSamplers;
    ControlObject m_numPreviewDecks;
    ControlPotmeter m_crossfader;
    ControlPushButton m_crossfaderReverse;
};

} // namespace

TEST_F(PlaylistDaoTest, CreateSmartPlaylistAndReadProperties) {
    PlaylistDAO& playlistDao = internalCollection()->getPlaylistDAO();

    const int playlistId = playlistDao.createSmartPlaylist(
            "Smart Playlist A",
            PlaylistDAO::SmartPlaylistMatchMode::MatchAny,
            false);
    ASSERT_NE(kInvalidPlaylistId, playlistId);

    EXPECT_TRUE(playlistDao.playlistExists(playlistId));
    EXPECT_TRUE(playlistDao.isSmartPlaylist(playlistId));

    PlaylistDAO::SmartPlaylistMatchMode matchMode =
            PlaylistDAO::SmartPlaylistMatchMode::MatchAll;
    bool autoRefresh = true;
    ASSERT_TRUE(playlistDao.readSmartPlaylistProperties(
            playlistId,
            &matchMode,
            &autoRefresh));
    EXPECT_EQ(PlaylistDAO::SmartPlaylistMatchMode::MatchAny, matchMode);
    EXPECT_FALSE(autoRefresh);
}

TEST_F(PlaylistDaoTest, ReplaceAndReadSmartPlaylistRules) {
    PlaylistDAO& playlistDao = internalCollection()->getPlaylistDAO();

    const int playlistId = playlistDao.createSmartPlaylist(
            "Smart Playlist B",
            PlaylistDAO::SmartPlaylistMatchMode::MatchAll,
            true);
    ASSERT_NE(kInvalidPlaylistId, playlistId);

    QList<PlaylistDAO::SmartPlaylistRule> rules;

    PlaylistDAO::SmartPlaylistRule firstRule;
    firstRule.position = 1;
    firstRule.field = "genre";
    firstRule.op = "contains";
    firstRule.value = "house";
    rules.append(firstRule);

    PlaylistDAO::SmartPlaylistRule secondRule;
    secondRule.position = 2;
    secondRule.field = "bpm";
    secondRule.op = "between";
    secondRule.value = "120";
    secondRule.secondValue = "128";
    rules.append(secondRule);

    ASSERT_TRUE(playlistDao.replaceSmartPlaylistRules(playlistId, rules));

    const QList<PlaylistDAO::SmartPlaylistRule> persistedRules =
            playlistDao.readSmartPlaylistRules(playlistId);
    ASSERT_EQ(2, persistedRules.size());

    EXPECT_EQ(1, persistedRules[0].position);
    EXPECT_EQ(QString("genre"), persistedRules[0].field);
    EXPECT_EQ(QString("contains"), persistedRules[0].op);
    EXPECT_EQ(QString("house"), persistedRules[0].value);
    EXPECT_FALSE(persistedRules[0].negate);

    EXPECT_EQ(2, persistedRules[1].position);
    EXPECT_EQ(QString("bpm"), persistedRules[1].field);
    EXPECT_EQ(QString("between"), persistedRules[1].op);
    EXPECT_EQ(QString("120"), persistedRules[1].value);
    EXPECT_EQ(QString("128"), persistedRules[1].secondValue);
}

TEST_F(PlaylistDaoTest, UpdateSmartPlaylistProperties) {
    PlaylistDAO& playlistDao = internalCollection()->getPlaylistDAO();

    const int playlistId = playlistDao.createSmartPlaylist(
            "Smart Playlist Props",
            PlaylistDAO::SmartPlaylistMatchMode::MatchAll,
            true);
    ASSERT_NE(kInvalidPlaylistId, playlistId);

    ASSERT_TRUE(playlistDao.updateSmartPlaylistProperties(
            playlistId,
            PlaylistDAO::SmartPlaylistMatchMode::MatchAny,
            false));

    PlaylistDAO::SmartPlaylistMatchMode matchMode =
            PlaylistDAO::SmartPlaylistMatchMode::MatchAll;
    bool autoRefresh = true;
    ASSERT_TRUE(playlistDao.readSmartPlaylistProperties(
            playlistId,
            &matchMode,
            &autoRefresh));
    EXPECT_EQ(PlaylistDAO::SmartPlaylistMatchMode::MatchAny, matchMode);
    EXPECT_FALSE(autoRefresh);
}

TEST_F(PlaylistDaoTest, UpdateSmartPlaylistPropertiesFailsForStaticPlaylist) {
    PlaylistDAO& playlistDao = internalCollection()->getPlaylistDAO();

    const int staticPlaylistId = playlistDao.createPlaylist("Static Playlist Props");
    ASSERT_NE(kInvalidPlaylistId, staticPlaylistId);

    EXPECT_FALSE(playlistDao.updateSmartPlaylistProperties(
            staticPlaylistId,
            PlaylistDAO::SmartPlaylistMatchMode::MatchAny,
            false));
}

TEST_F(PlaylistDaoTest, ReplaceRulesFailsForStaticPlaylist) {
    PlaylistDAO& playlistDao = internalCollection()->getPlaylistDAO();

    const int staticPlaylistId = playlistDao.createPlaylist("Static Playlist A");
    ASSERT_NE(kInvalidPlaylistId, staticPlaylistId);
    ASSERT_FALSE(playlistDao.isSmartPlaylist(staticPlaylistId));

    PlaylistDAO::SmartPlaylistRule rule;
    rule.position = 1;
    rule.field = "rating";
    rule.op = "gte";
    rule.value = "4";

    EXPECT_FALSE(playlistDao.replaceSmartPlaylistRules(staticPlaylistId, {rule}));
}

TEST_F(PlaylistDaoTest, ReplaceRulesRejectsInvalidFieldOrOperator) {
    PlaylistDAO& playlistDao = internalCollection()->getPlaylistDAO();

    const int playlistId = playlistDao.createSmartPlaylist(
            "Smart Playlist C",
            PlaylistDAO::SmartPlaylistMatchMode::MatchAll,
            true);
    ASSERT_NE(kInvalidPlaylistId, playlistId);

    PlaylistDAO::SmartPlaylistRule invalidFieldRule;
    invalidFieldRule.field = "not_a_field";
    invalidFieldRule.op = "contains";
    invalidFieldRule.value = "x";
    EXPECT_FALSE(playlistDao.replaceSmartPlaylistRules(playlistId, {invalidFieldRule}));

    PlaylistDAO::SmartPlaylistRule invalidOpRule;
    invalidOpRule.field = "genre";
    invalidOpRule.op = "between";
    invalidOpRule.value = "a";
    invalidOpRule.secondValue = "b";
    EXPECT_FALSE(playlistDao.replaceSmartPlaylistRules(playlistId, {invalidOpRule}));

    PlaylistDAO::SmartPlaylistRule missingBetweenValueRule;
    missingBetweenValueRule.field = "bpm";
    missingBetweenValueRule.op = "between";
    missingBetweenValueRule.value = "120";
    EXPECT_FALSE(playlistDao.replaceSmartPlaylistRules(playlistId, {missingBetweenValueRule}));
}

TEST_F(PlaylistDaoTest, ReplaceRulesCanonicalizesRuleFieldsOperatorsAndPositions) {
    PlaylistDAO& playlistDao = internalCollection()->getPlaylistDAO();

    const int playlistId = playlistDao.createSmartPlaylist(
            "Smart Playlist D",
            PlaylistDAO::SmartPlaylistMatchMode::MatchAll,
            true);
    ASSERT_NE(kInvalidPlaylistId, playlistId);

    PlaylistDAO::SmartPlaylistRule firstRule;
    firstRule.position = 42;
    firstRule.field = "  GeNrE ";
    firstRule.op = "  CoNtAiNs ";
    firstRule.value = " house ";
    firstRule.secondValue = "unused";

    PlaylistDAO::SmartPlaylistRule secondRule;
    secondRule.position = 7;
    secondRule.field = "rating";
    secondRule.op = "is_empty";
    secondRule.value = "4";
    secondRule.secondValue = "8";

    ASSERT_TRUE(playlistDao.replaceSmartPlaylistRules(playlistId, {firstRule, secondRule}));

    const QList<PlaylistDAO::SmartPlaylistRule> persistedRules =
            playlistDao.readSmartPlaylistRules(playlistId);
    ASSERT_EQ(2, persistedRules.size());

    EXPECT_EQ(1, persistedRules[0].position);
    EXPECT_EQ(QString("genre"), persistedRules[0].field);
    EXPECT_EQ(QString("contains"), persistedRules[0].op);
    EXPECT_EQ(QString("house"), persistedRules[0].value);
    EXPECT_TRUE(persistedRules[0].secondValue.isEmpty());

    EXPECT_EQ(2, persistedRules[1].position);
    EXPECT_EQ(QString("rating"), persistedRules[1].field);
    EXPECT_EQ(QString("is_empty"), persistedRules[1].op);
    EXPECT_TRUE(persistedRules[1].value.isEmpty());
    EXPECT_TRUE(persistedRules[1].secondValue.isEmpty());
}

TEST_F(PlaylistDaoTest, BuildSmartPlaylistSearchQueryMatchAllAndAny) {
    PlaylistDAO& playlistDao = internalCollection()->getPlaylistDAO();

    QList<PlaylistDAO::SmartPlaylistRule> rules;

    PlaylistDAO::SmartPlaylistRule genreRule;
    genreRule.field = "genre";
    genreRule.op = "contains";
    genreRule.value = "deep house";
    rules.append(genreRule);

    PlaylistDAO::SmartPlaylistRule bpmRule;
    bpmRule.field = "bpm";
    bpmRule.op = "between";
    bpmRule.value = "120";
    bpmRule.secondValue = "128";
    rules.append(bpmRule);

    PlaylistDAO::SmartPlaylistRule ratingRule;
    ratingRule.field = "rating";
    ratingRule.op = "gte";
    ratingRule.value = "4";
    ratingRule.negate = true;
    rules.append(ratingRule);

    const QString matchAllQuery = playlistDao.buildSmartPlaylistSearchQuery(
            rules,
            PlaylistDAO::SmartPlaylistMatchMode::MatchAll);
    EXPECT_EQ(QString("genre:\"deep house\" bpm:120-128 -rating:>=4"),
            matchAllQuery);

    const QString matchAnyQuery = playlistDao.buildSmartPlaylistSearchQuery(
            rules,
            PlaylistDAO::SmartPlaylistMatchMode::MatchAny);
    EXPECT_EQ(QString("genre:\"deep house\" OR bpm:120-128 OR -rating:>=4"),
            matchAnyQuery);
}

TEST_F(PlaylistDaoTest, GetSmartPlaylistSearchQueryFromPersistedRules) {
    PlaylistDAO& playlistDao = internalCollection()->getPlaylistDAO();

    const int playlistId = playlistDao.createSmartPlaylist(
            "Smart Playlist E",
            PlaylistDAO::SmartPlaylistMatchMode::MatchAny,
            true);
    ASSERT_NE(kInvalidPlaylistId, playlistId);

    QList<PlaylistDAO::SmartPlaylistRule> rules;

    PlaylistDAO::SmartPlaylistRule addedRule;
    addedRule.field = "datetime_added";
    addedRule.op = "gte";
    addedRule.value = "2025-01-01";
    rules.append(addedRule);

    PlaylistDAO::SmartPlaylistRule crateRule;
    crateRule.field = "crate";
    crateRule.op = "equals";
    crateRule.value = "Warmup";
    rules.append(crateRule);

    ASSERT_TRUE(playlistDao.replaceSmartPlaylistRules(playlistId, rules));

    EXPECT_EQ(QString("datetime_added:>=2025-01-01 OR crate:=Warmup"),
            playlistDao.getSmartPlaylistSearchQuery(playlistId));
}

TEST_F(PlaylistDaoTest, SmartPlaylistAutoRefreshEnabledRefreshesOnCrateChanges) {
    PlaylistDAO& playlistDao = internalCollection()->getPlaylistDAO();

    ScopedPlaylistTableModelControls scopedControls;
    ScopedPlayerInfo scopedPlayerInfo;

    const int playlistId = playlistDao.createSmartPlaylist(
            "Smart Playlist Auto On",
            PlaylistDAO::SmartPlaylistMatchMode::MatchAll,
            true);
    ASSERT_NE(kInvalidPlaylistId, playlistId);

    PlaylistDAO::SmartPlaylistRule crateRule;
    crateRule.field = "crate";
    crateRule.op = "equals";
    crateRule.value = "AutoRefreshCrate";
    ASSERT_TRUE(playlistDao.replaceSmartPlaylistRules(playlistId, {crateRule}));

    PlaylistTableModel model(nullptr, trackCollectionManager(), "testSmartPlaylistAutoOn");
    model.selectPlaylist(playlistId);
    EXPECT_TRUE(playlistDao.getTrackIdsInPlaylistOrder(playlistId).isEmpty());

    Crate crate;
    crate.setName("AutoRefreshCrate");
    CrateId crateId;
    ASSERT_TRUE(internalCollection()->insertCrate(crate, &crateId));
    ASSERT_TRUE(crateId.isValid());

    const auto pTrack = getOrAddTrackByLocation(
            getTestDir().filePath(QStringLiteral("id3-test-data/cover-test-png.mp3")));
    ASSERT_TRUE(pTrack);
    const TrackId trackId = pTrack->getId();
    ASSERT_TRUE(trackId.isValid());

    ASSERT_TRUE(internalCollection()->addCrateTracks(crateId, {trackId}));

    QTest::qWait(180);

    const QList<TrackId> refreshedTrackIds = playlistDao.getTrackIdsInPlaylistOrder(playlistId);
    ASSERT_EQ(1, refreshedTrackIds.size());
    EXPECT_EQ(trackId, refreshedTrackIds.first());
}

TEST_F(PlaylistDaoTest, SmartPlaylistAutoRefreshDisabledSkipsEventDrivenRefresh) {
    PlaylistDAO& playlistDao = internalCollection()->getPlaylistDAO();

    ScopedPlaylistTableModelControls scopedControls;
    ScopedPlayerInfo scopedPlayerInfo;

    const int playlistId = playlistDao.createSmartPlaylist(
            "Smart Playlist Auto Off",
            PlaylistDAO::SmartPlaylistMatchMode::MatchAll,
            false);
    ASSERT_NE(kInvalidPlaylistId, playlistId);

    PlaylistDAO::SmartPlaylistRule crateRule;
    crateRule.field = "crate";
    crateRule.op = "equals";
    crateRule.value = "NoAutoRefreshCrate";
    ASSERT_TRUE(playlistDao.replaceSmartPlaylistRules(playlistId, {crateRule}));

    PlaylistTableModel model(nullptr, trackCollectionManager(), "testSmartPlaylistAutoOff");
    model.selectPlaylist(playlistId);
    EXPECT_TRUE(playlistDao.getTrackIdsInPlaylistOrder(playlistId).isEmpty());

    Crate crate;
    crate.setName("NoAutoRefreshCrate");
    CrateId crateId;
    ASSERT_TRUE(internalCollection()->insertCrate(crate, &crateId));
    ASSERT_TRUE(crateId.isValid());

    const auto pTrack = getOrAddTrackByLocation(
            getTestDir().filePath(QStringLiteral("id3-test-data/cover-test-png.mp3")));
    ASSERT_TRUE(pTrack);
    const TrackId trackId = pTrack->getId();
    ASSERT_TRUE(trackId.isValid());

    ASSERT_TRUE(internalCollection()->addCrateTracks(crateId, {trackId}));

    QTest::qWait(180);

    EXPECT_TRUE(playlistDao.getTrackIdsInPlaylistOrder(playlistId).isEmpty());
}

TEST_F(PlaylistDaoTest, SmartPlaylistAutoRefreshCoalescesRapidCrateChanges) {
    PlaylistDAO& playlistDao = internalCollection()->getPlaylistDAO();

    ScopedPlaylistTableModelControls scopedControls;
    ScopedPlayerInfo scopedPlayerInfo;

    const int playlistId = playlistDao.createSmartPlaylist(
            "Smart Playlist Coalesced Refresh",
            PlaylistDAO::SmartPlaylistMatchMode::MatchAll,
            true);
    ASSERT_NE(kInvalidPlaylistId, playlistId);

    PlaylistDAO::SmartPlaylistRule crateRule;
    crateRule.field = "crate";
    crateRule.op = "equals";
    crateRule.value = "CoalescedRefreshCrate";
    ASSERT_TRUE(playlistDao.replaceSmartPlaylistRules(playlistId, {crateRule}));

    PlaylistTableModel model(nullptr, trackCollectionManager(), "testSmartPlaylistCoalesced");
    model.selectPlaylist(playlistId);
    EXPECT_TRUE(playlistDao.getTrackIdsInPlaylistOrder(playlistId).isEmpty());

    Crate crate;
    crate.setName("CoalescedRefreshCrate");
    CrateId crateId;
    ASSERT_TRUE(internalCollection()->insertCrate(crate, &crateId));
    ASSERT_TRUE(crateId.isValid());

    const auto pTrackA = getOrAddTrackByLocation(
            getTestDir().filePath(QStringLiteral("id3-test-data/cover-test-png.mp3")));
    const auto pTrackB = getOrAddTrackByLocation(
            getTestDir().filePath(QStringLiteral("id3-test-data/cover-test-jpg.mp3")));
    ASSERT_TRUE(pTrackA);
    ASSERT_TRUE(pTrackB);
    const TrackId trackIdA = pTrackA->getId();
    const TrackId trackIdB = pTrackB->getId();
    ASSERT_TRUE(trackIdA.isValid());
    ASSERT_TRUE(trackIdB.isValid());

    QSignalSpy tracksAddedSpy(
            &playlistDao,
            &PlaylistDAO::tracksAdded);

    ASSERT_TRUE(internalCollection()->addCrateTracks(crateId, {trackIdA}));
    ASSERT_TRUE(internalCollection()->addCrateTracks(crateId, {trackIdB}));

    // Before the debounce timeout elapses, no refresh should have happened yet.
    QTest::qWait(50);
    EXPECT_TRUE(playlistDao.getTrackIdsInPlaylistOrder(playlistId).isEmpty());

    // After the timeout, one refresh should include both changes.
    QTest::qWait(180);

    const QList<TrackId> refreshedTrackIds = playlistDao.getTrackIdsInPlaylistOrder(playlistId);
    EXPECT_EQ(2, refreshedTrackIds.size());
    EXPECT_TRUE(refreshedTrackIds.contains(trackIdA));
    EXPECT_TRUE(refreshedTrackIds.contains(trackIdB));

    int additionsForTargetPlaylist = 0;
    for (const auto& signalArgs : tracksAddedSpy) {
        const QSet<int> changedPlaylistIds = signalArgs.at(0).value<QSet<int>>();
        if (changedPlaylistIds.contains(playlistId)) {
            ++additionsForTargetPlaylist;
        }
    }
    EXPECT_EQ(1, additionsForTargetPlaylist);
}

TEST_F(PlaylistDaoTest, SmartPlaylistAutoRefreshEnabledRefreshesOnTrackMetadataChanges) {
    PlaylistDAO& playlistDao = internalCollection()->getPlaylistDAO();

    ScopedPlaylistTableModelControls scopedControls;
    ScopedPlayerInfo scopedPlayerInfo;

    const int playlistId = playlistDao.createSmartPlaylist(
            "Smart Playlist Metadata Refresh",
            PlaylistDAO::SmartPlaylistMatchMode::MatchAll,
            true);
    ASSERT_NE(kInvalidPlaylistId, playlistId);

    PlaylistDAO::SmartPlaylistRule artistRule;
    artistRule.field = "artist";
    artistRule.op = "equals";
    artistRule.value = "Refreshed Artist";
    ASSERT_TRUE(playlistDao.replaceSmartPlaylistRules(playlistId, {artistRule}));

    PlaylistTableModel model(nullptr, trackCollectionManager(), "testSmartPlaylistMetadataRefresh");
    model.selectPlaylist(playlistId);
    EXPECT_TRUE(playlistDao.getTrackIdsInPlaylistOrder(playlistId).isEmpty());

    const auto pTrack = getOrAddTrackByLocation(
            getTestDir().filePath(QStringLiteral("id3-test-data/cover-test-png.mp3")));
    ASSERT_TRUE(pTrack);
    const TrackId trackId = pTrack->getId();
    ASSERT_TRUE(trackId.isValid());

    pTrack->setArtist(QStringLiteral("Refreshed Artist"));
    internalCollection()->getTrackDAO().slotDatabaseTracksChanged(QSet<TrackId>{trackId});

    QTest::qWait(180);

    const QList<TrackId> refreshedTrackIds = playlistDao.getTrackIdsInPlaylistOrder(playlistId);
    ASSERT_EQ(1, refreshedTrackIds.size());
    EXPECT_EQ(trackId, refreshedTrackIds.first());
}

TEST_F(PlaylistDaoTest, SmartPlaylistAutoRefreshDisabledSkipsTrackMetadataChanges) {
    PlaylistDAO& playlistDao = internalCollection()->getPlaylistDAO();

    ScopedPlaylistTableModelControls scopedControls;
    ScopedPlayerInfo scopedPlayerInfo;

    const int playlistId = playlistDao.createSmartPlaylist(
            "Smart Playlist Metadata Refresh Disabled",
            PlaylistDAO::SmartPlaylistMatchMode::MatchAll,
            false);
    ASSERT_NE(kInvalidPlaylistId, playlistId);

    PlaylistDAO::SmartPlaylistRule artistRule;
    artistRule.field = "artist";
    artistRule.op = "equals";
    artistRule.value = "Refreshed Artist Disabled";
    ASSERT_TRUE(playlistDao.replaceSmartPlaylistRules(playlistId, {artistRule}));

    PlaylistTableModel model(nullptr, trackCollectionManager(), "testSmartPlaylistMetadataRefreshOff");
    model.selectPlaylist(playlistId);
    EXPECT_TRUE(playlistDao.getTrackIdsInPlaylistOrder(playlistId).isEmpty());

    const auto pTrack = getOrAddTrackByLocation(
            getTestDir().filePath(QStringLiteral("id3-test-data/cover-test-png.mp3")));
    ASSERT_TRUE(pTrack);
    const TrackId trackId = pTrack->getId();
    ASSERT_TRUE(trackId.isValid());

    pTrack->setArtist(QStringLiteral("Refreshed Artist Disabled"));
    internalCollection()->getTrackDAO().slotDatabaseTracksChanged(QSet<TrackId>{trackId});

    QTest::qWait(180);

    EXPECT_TRUE(playlistDao.getTrackIdsInPlaylistOrder(playlistId).isEmpty());
}

TEST_F(PlaylistDaoTest, SmartPlaylistAutoRefreshToggleEnablesEventDrivenRefresh) {
    PlaylistDAO& playlistDao = internalCollection()->getPlaylistDAO();

    ScopedPlaylistTableModelControls scopedControls;
    ScopedPlayerInfo scopedPlayerInfo;

    const int playlistId = playlistDao.createSmartPlaylist(
            "Smart Playlist Toggle Auto Refresh",
            PlaylistDAO::SmartPlaylistMatchMode::MatchAll,
            false);
    ASSERT_NE(kInvalidPlaylistId, playlistId);

    PlaylistDAO::SmartPlaylistRule crateRule;
    crateRule.field = "crate";
    crateRule.op = "equals";
    crateRule.value = "ToggleRefreshCrate";
    ASSERT_TRUE(playlistDao.replaceSmartPlaylistRules(playlistId, {crateRule}));

    PlaylistTableModel model(nullptr, trackCollectionManager(), "testSmartPlaylistToggleAutoRefresh");
    model.selectPlaylist(playlistId);
    EXPECT_TRUE(playlistDao.getTrackIdsInPlaylistOrder(playlistId).isEmpty());

    Crate crate;
    crate.setName("ToggleRefreshCrate");
    CrateId crateId;
    ASSERT_TRUE(internalCollection()->insertCrate(crate, &crateId));
    ASSERT_TRUE(crateId.isValid());

    const auto pTrackA = getOrAddTrackByLocation(
            getTestDir().filePath(QStringLiteral("id3-test-data/cover-test-png.mp3")));
    ASSERT_TRUE(pTrackA);
    const TrackId trackIdA = pTrackA->getId();
    ASSERT_TRUE(trackIdA.isValid());

    ASSERT_TRUE(internalCollection()->addCrateTracks(crateId, {trackIdA}));
    QTest::qWait(180);
    EXPECT_TRUE(playlistDao.getTrackIdsInPlaylistOrder(playlistId).isEmpty());

    ASSERT_TRUE(playlistDao.updateSmartPlaylistProperties(
            playlistId,
            PlaylistDAO::SmartPlaylistMatchMode::MatchAll,
            true));

    const auto pTrackB = getOrAddTrackByLocation(
            getTestDir().filePath(QStringLiteral("id3-test-data/cover-test-jpg.mp3")));
    ASSERT_TRUE(pTrackB);
    const TrackId trackIdB = pTrackB->getId();
    ASSERT_TRUE(trackIdB.isValid());

    ASSERT_TRUE(internalCollection()->addCrateTracks(crateId, {trackIdB}));
    QTest::qWait(180);

    const QList<TrackId> refreshedTrackIds = playlistDao.getTrackIdsInPlaylistOrder(playlistId);
    EXPECT_EQ(2, refreshedTrackIds.size());
    EXPECT_TRUE(refreshedTrackIds.contains(trackIdA));
    EXPECT_TRUE(refreshedTrackIds.contains(trackIdB));
}

TEST_F(PlaylistDaoTest, SmartPlaylistAutoRefreshToggleDisablesEventDrivenRefresh) {
    PlaylistDAO& playlistDao = internalCollection()->getPlaylistDAO();

    ScopedPlaylistTableModelControls scopedControls;
    ScopedPlayerInfo scopedPlayerInfo;

    const int playlistId = playlistDao.createSmartPlaylist(
            "Smart Playlist Toggle Auto Refresh Off",
            PlaylistDAO::SmartPlaylistMatchMode::MatchAll,
            true);
    ASSERT_NE(kInvalidPlaylistId, playlistId);

    PlaylistDAO::SmartPlaylistRule crateRule;
    crateRule.field = "crate";
    crateRule.op = "equals";
    crateRule.value = "ToggleRefreshOffCrate";
    ASSERT_TRUE(playlistDao.replaceSmartPlaylistRules(playlistId, {crateRule}));

    PlaylistTableModel model(nullptr, trackCollectionManager(), "testSmartPlaylistToggleAutoRefreshOff");
    model.selectPlaylist(playlistId);
    EXPECT_TRUE(playlistDao.getTrackIdsInPlaylistOrder(playlistId).isEmpty());

    Crate crate;
    crate.setName("ToggleRefreshOffCrate");
    CrateId crateId;
    ASSERT_TRUE(internalCollection()->insertCrate(crate, &crateId));
    ASSERT_TRUE(crateId.isValid());

    const auto pTrackA = getOrAddTrackByLocation(
            getTestDir().filePath(QStringLiteral("id3-test-data/cover-test-png.mp3")));
    ASSERT_TRUE(pTrackA);
    const TrackId trackIdA = pTrackA->getId();
    ASSERT_TRUE(trackIdA.isValid());

    ASSERT_TRUE(internalCollection()->addCrateTracks(crateId, {trackIdA}));
    QTest::qWait(180);

    const QList<TrackId> initialRefreshedTrackIds = playlistDao.getTrackIdsInPlaylistOrder(playlistId);
    ASSERT_EQ(1, initialRefreshedTrackIds.size());
    EXPECT_EQ(trackIdA, initialRefreshedTrackIds.first());

    ASSERT_TRUE(playlistDao.updateSmartPlaylistProperties(
            playlistId,
            PlaylistDAO::SmartPlaylistMatchMode::MatchAll,
            false));

    const auto pTrackB = getOrAddTrackByLocation(
            getTestDir().filePath(QStringLiteral("id3-test-data/cover-test-jpg.mp3")));
    ASSERT_TRUE(pTrackB);
    const TrackId trackIdB = pTrackB->getId();
    ASSERT_TRUE(trackIdB.isValid());

    ASSERT_TRUE(internalCollection()->addCrateTracks(crateId, {trackIdB}));
    QTest::qWait(180);

    const QList<TrackId> refreshedTrackIds = playlistDao.getTrackIdsInPlaylistOrder(playlistId);
    ASSERT_EQ(1, refreshedTrackIds.size());
    EXPECT_EQ(trackIdA, refreshedTrackIds.first());
}

TEST_F(PlaylistDaoTest, SmartPlaylistMatchModeUpdateAffectsNextEventRefresh) {
    PlaylistDAO& playlistDao = internalCollection()->getPlaylistDAO();

    ScopedPlaylistTableModelControls scopedControls;
    ScopedPlayerInfo scopedPlayerInfo;

    const int playlistId = playlistDao.createSmartPlaylist(
            "Smart Playlist Match Mode Toggle",
            PlaylistDAO::SmartPlaylistMatchMode::MatchAll,
            true);
    ASSERT_NE(kInvalidPlaylistId, playlistId);

    PlaylistDAO::SmartPlaylistRule artistRule;
    artistRule.field = "artist";
    artistRule.op = "equals";
    artistRule.value = "ModeTestArtist";

    PlaylistDAO::SmartPlaylistRule titleRule;
    titleRule.field = "title";
    titleRule.op = "equals";
    titleRule.value = "ModeTestTitle";

    ASSERT_TRUE(playlistDao.replaceSmartPlaylistRules(playlistId, {artistRule, titleRule}));

    PlaylistTableModel model(nullptr, trackCollectionManager(), "testSmartPlaylistMatchModeToggle");
    model.selectPlaylist(playlistId);
    EXPECT_TRUE(playlistDao.getTrackIdsInPlaylistOrder(playlistId).isEmpty());

    const auto pTrackA = getOrAddTrackByLocation(
            getTestDir().filePath(QStringLiteral("id3-test-data/cover-test-png.mp3")));
    const auto pTrackB = getOrAddTrackByLocation(
            getTestDir().filePath(QStringLiteral("id3-test-data/cover-test-jpg.mp3")));
    ASSERT_TRUE(pTrackA);
    ASSERT_TRUE(pTrackB);
    const TrackId trackIdA = pTrackA->getId();
    const TrackId trackIdB = pTrackB->getId();
    ASSERT_TRUE(trackIdA.isValid());
    ASSERT_TRUE(trackIdB.isValid());

    pTrackA->setArtist(QStringLiteral("ModeTestArtist"));
    pTrackA->setTitle(QStringLiteral("NotModeTestTitle"));
    pTrackB->setArtist(QStringLiteral("NotModeTestArtist"));
    pTrackB->setTitle(QStringLiteral("ModeTestTitle"));

    internalCollection()->getTrackDAO().slotDatabaseTracksChanged(QSet<TrackId>{trackIdA, trackIdB});
    QTest::qWait(180);
    EXPECT_TRUE(playlistDao.getTrackIdsInPlaylistOrder(playlistId).isEmpty());

    ASSERT_TRUE(playlistDao.updateSmartPlaylistProperties(
            playlistId,
            PlaylistDAO::SmartPlaylistMatchMode::MatchAny,
            true));

    internalCollection()->getTrackDAO().slotDatabaseTracksChanged(QSet<TrackId>{trackIdA, trackIdB});
    QTest::qWait(180);

    const QList<TrackId> refreshedTrackIds = playlistDao.getTrackIdsInPlaylistOrder(playlistId);
    EXPECT_EQ(2, refreshedTrackIds.size());
    EXPECT_TRUE(refreshedTrackIds.contains(trackIdA));
    EXPECT_TRUE(refreshedTrackIds.contains(trackIdB));
}
