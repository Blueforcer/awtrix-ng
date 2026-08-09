#include <unity.h>

#include <string>

#include "core/AssetPaths.h"

using namespace awtrix;

namespace {

void test_public_dirs_allowed() {
  TEST_ASSERT_TRUE(assets::isWritable("/ICONS/1.gif"));
  TEST_ASSERT_TRUE(assets::isWritable("/MELODIES/alarm.txt"));
  TEST_ASSERT_TRUE(assets::isWritable("/PALETTES/fire.json"));
  TEST_ASSERT_TRUE(assets::isWritable("/MP3/ding.mp3"));
  TEST_ASSERT_TRUE(assets::isServable("/MP3/ding.mp3"));
  TEST_ASSERT_TRUE(assets::isServable("/ICONS/sub/2.jpg"));
}

void test_traversal_rejected() {
  TEST_ASSERT_FALSE(assets::isWritable("/ICONS/../apploop.json"));
  TEST_ASSERT_FALSE(assets::isWritable("/ICONS/../../secret"));
  TEST_ASSERT_FALSE(assets::isServable("/MELODIES/../../etc/passwd"));
  TEST_ASSERT_FALSE(assets::isWritable("/MP3/../device.json"));
}

void test_absolute_escape_rejected() {
  TEST_ASSERT_FALSE(assets::isWritable("/apploop.json"));
  TEST_ASSERT_FALSE(assets::isWritable("/CUSTOMAPPS/clock.json"));
  TEST_ASSERT_FALSE(assets::isServable("/CUSTOMAPPS/clock.json"));
  TEST_ASSERT_FALSE(assets::isWritable("/config"));
  TEST_ASSERT_FALSE(assets::isWritable("/"));
}

void test_scripts_dir_is_not_public() {
  TEST_ASSERT_FALSE(assets::isWritable("/SCRIPTS/weather.ax"));
  TEST_ASSERT_FALSE(assets::isServable("/SCRIPTS/weather.ax"));
  TEST_ASSERT_FALSE(assets::isWritable("/SCRIPTS/weather.store.json"));
  TEST_ASSERT_FALSE(assets::isServable("/SCRIPTS/weather.store.json"));
  TEST_ASSERT_FALSE(assets::isWritable("/SCRIPTS"));
  TEST_ASSERT_TRUE(assets::kindFor("/SCRIPTS/weather.ax") == assets::AssetKind::Unknown);
}

void test_no_traversal_into_scripts_dir() {
  TEST_ASSERT_FALSE(assets::isWritable("/ICONS/../SCRIPTS/evil.ax"));
  TEST_ASSERT_FALSE(assets::isWritable("/PALETTES/../SCRIPTS/evil.store.json"));
  TEST_ASSERT_FALSE(assets::isWritable("/MELODIES/../SCRIPTS/evil.ax"));
  TEST_ASSERT_FALSE(assets::isWritable("/SCRIPTSX/evil.ax"));
}

void test_lookalike_prefixes_rejected() {
  TEST_ASSERT_FALSE(assets::isWritable("/ICONSX/1.gif"));
  TEST_ASSERT_FALSE(assets::isWritable("/MELODIES_BACKUP/x"));
  TEST_ASSERT_FALSE(assets::isWritable("ICONS/1.gif"));
  TEST_ASSERT_FALSE(assets::isWritable("/MP3X/ding.mp3"));
  TEST_ASSERT_FALSE(assets::isWritable("SOUNDS/ding.mp3"));
}

void test_empty_rejected() {
  TEST_ASSERT_FALSE(assets::isWritable(""));
  TEST_ASSERT_FALSE(assets::isServable(""));
}


void test_backup_readable_covers_scripts_and_apploop() {
  TEST_ASSERT_TRUE(assets::isBackupReadable("/SCRIPTS/weather.ax"));
  TEST_ASSERT_TRUE(assets::isBackupReadable("/SCRIPTS/weather.store.json"));
  TEST_ASSERT_TRUE(assets::isBackupReadable("/apploop.json"));
  TEST_ASSERT_FALSE(assets::isBackupReadable("/ICONS/a.gif"));
  TEST_ASSERT_FALSE(assets::isBackupReadable("/SCRIPTS/../device.json"));
  TEST_ASSERT_FALSE(assets::isBackupReadable("/other.json"));
  TEST_ASSERT_FALSE(assets::isServable("/SCRIPTS/weather.ax"));
  TEST_ASSERT_FALSE(assets::isServable("/apploop.json"));
}

void test_backup_writable_adds_scripts_only() {
  TEST_ASSERT_TRUE(assets::isBackupWritable("/ICONS/a.gif"));
  TEST_ASSERT_TRUE(assets::isBackupWritable("/MELODIES/a.txt"));
  TEST_ASSERT_TRUE(assets::isBackupWritable("/PALETTES/a.txt"));
  TEST_ASSERT_TRUE(assets::isBackupWritable("/MP3/ding.mp3"));
  TEST_ASSERT_TRUE(assets::isBackupWritable("/SCRIPTS/weather.ax"));
  TEST_ASSERT_FALSE(assets::isBackupWritable("/ICONS/../../evil.txt"));
  TEST_ASSERT_FALSE(assets::isBackupWritable("/apploop.json"));
  TEST_ASSERT_FALSE(assets::isBackupWritable("/device.json"));
}

void test_kind_follows_the_folder() {
  TEST_ASSERT_TRUE(assets::kindFor("/ICONS/a.gif") == assets::AssetKind::Icon);
  TEST_ASSERT_TRUE(assets::kindFor("/MELODIES/a.txt") == assets::AssetKind::Melody);
  TEST_ASSERT_TRUE(assets::kindFor("/PALETTES/a.txt") == assets::AssetKind::Palette);
  TEST_ASSERT_TRUE(assets::kindFor("/MP3/a.mp3") == assets::AssetKind::Mp3);
  TEST_ASSERT_TRUE(assets::kindFor("/other/a.txt") == assets::AssetKind::Unknown);
}

void test_sounds_accept_mp3_and_reject_the_rest() {
  const unsigned char id3[] = {'I', 'D', '3', 0x04, 0x00};
  const unsigned char sync[] = {0xFF, 0xFB, 0x90, 0x00};
  const unsigned char syncMpeg2[] = {0xFF, 0xF3, 0x90, 0x00};
  const unsigned char gif[] = {'G', 'I', 'F', '8', '9', 'a'};
  const unsigned char jpeg[] = {0xFF, 0xD8, 0xFF, 0xE0};
  const unsigned char text[] = {'h', 'e', 'l', 'l', 'o'};
  const unsigned char shortId3[] = {'I', 'D'};
  TEST_ASSERT_TRUE(assets::contentLooksValid(assets::AssetKind::Mp3, id3, sizeof(id3)));
  TEST_ASSERT_TRUE(assets::contentLooksValid(assets::AssetKind::Mp3, sync, sizeof(sync)));
  TEST_ASSERT_TRUE(assets::contentLooksValid(assets::AssetKind::Mp3, syncMpeg2, sizeof(syncMpeg2)));
  TEST_ASSERT_FALSE(assets::contentLooksValid(assets::AssetKind::Mp3, gif, sizeof(gif)));
  TEST_ASSERT_FALSE(assets::contentLooksValid(assets::AssetKind::Mp3, jpeg, sizeof(jpeg)));
  TEST_ASSERT_FALSE(assets::contentLooksValid(assets::AssetKind::Mp3, text, sizeof(text)));
  TEST_ASSERT_FALSE(assets::contentLooksValid(assets::AssetKind::Mp3, shortId3, sizeof(shortId3)));
  TEST_ASSERT_FALSE(assets::contentLooksValid(assets::AssetKind::Mp3, id3, 0));
  TEST_ASSERT_EQUAL_STRING("MP3 (MPEG-1 Layer III)",
                           assets::acceptedFormats(assets::AssetKind::Mp3));
}

void test_icons_accept_gif_and_jpeg() {
  const unsigned char gif87[] = {'G', 'I', 'F', '8', '7', 'a'};
  const unsigned char gif89[] = {'G', 'I', 'F', '8', '9', 'a'};
  const unsigned char jpeg[] = {0xFF, 0xD8, 0xFF, 0xE0};
  TEST_ASSERT_TRUE(assets::contentLooksValid(assets::AssetKind::Icon, gif87, sizeof(gif87)));
  TEST_ASSERT_TRUE(assets::contentLooksValid(assets::AssetKind::Icon, gif89, sizeof(gif89)));
  TEST_ASSERT_TRUE(assets::contentLooksValid(assets::AssetKind::Icon, jpeg, sizeof(jpeg)));
}

void test_icons_reject_png_whatever_it_is_called() {
  const unsigned char png[] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
  TEST_ASSERT_FALSE(assets::contentLooksValid(assets::AssetKind::Icon, png, sizeof(png)));
}

void test_icons_reject_text_and_truncated_headers() {
  const unsigned char text[] = {'h', 'e', 'l', 'l', 'o'};
  const unsigned char shortGif[] = {'G', 'I'};
  TEST_ASSERT_FALSE(assets::contentLooksValid(assets::AssetKind::Icon, text, sizeof(text)));
  TEST_ASSERT_FALSE(assets::contentLooksValid(assets::AssetKind::Icon, shortGif, sizeof(shortGif)));
  TEST_ASSERT_FALSE(assets::contentLooksValid(assets::AssetKind::Icon, text, 0));
}

void test_text_folders_accept_text_and_reject_binary() {
  const unsigned char palette[] = {'F', 'F', '0', '0', '0', '0', 0x0D, 0x0A};
  const unsigned char gif[] = {'G', 'I', 'F', '8', '9', 'a'};
  const unsigned char withNul[] = {'a', 0x00, 'b'};
  TEST_ASSERT_TRUE(assets::contentLooksValid(assets::AssetKind::Palette, palette, sizeof(palette)));
  TEST_ASSERT_FALSE(assets::contentLooksValid(assets::AssetKind::Melody, gif, sizeof(gif)));
  TEST_ASSERT_FALSE(assets::contentLooksValid(assets::AssetKind::Palette, withNul, sizeof(withNul)));
}

void test_melody_upload_must_parse() {
  const unsigned char good[] = {'b', 'e', 'l', 'l', ':', 'd', '=', '4', ',',
                                'o', '=', '5', ',', 'b', '=', '1', '0', '0',
                                ':', 'e', ',', 'c', 0x0A};
  const unsigned char noTitle[] = {'d', '=', '4', ',', 'o', '=', '5', ':', 'c'};
  const unsigned char notANote[] = {'x', ':', 'd', '=', '4', ':', 'h'};
  TEST_ASSERT_TRUE(assets::contentLooksValid(assets::AssetKind::Melody, good, sizeof(good)));
  TEST_ASSERT_FALSE(assets::contentLooksValid(assets::AssetKind::Melody, noTitle, sizeof(noTitle)));
  TEST_ASSERT_FALSE(
      assets::contentLooksValid(assets::AssetKind::Melody, notANote, sizeof(notANote)));
  TEST_ASSERT_FALSE(assets::contentLooksValid(assets::AssetKind::Melody, good, 0));
}

}

void test_sound_upload_name_must_be_playable() {
  TEST_ASSERT_TRUE(assets::uploadNameOk("/MP3/ding.mp3"));
  TEST_ASSERT_TRUE(assets::uploadNameOk("/MP3/Alarm_2-long.mp3"));
  // The player builds "/MP3/<name>.mp3" from the bare name, so anything it cannot spell is
  // a file that would upload and then never be found.
  TEST_ASSERT_FALSE(assets::uploadNameOk("/MP3/two words.mp3"));
  TEST_ASSERT_FALSE(assets::uploadNameOk("/MP3/I'm (500).mp3"));
  TEST_ASSERT_FALSE(assets::uploadNameOk("/MP3/tune.MP3"));
  TEST_ASSERT_FALSE(assets::uploadNameOk("/MP3/no-extension"));
  TEST_ASSERT_FALSE(assets::uploadNameOk("/MP3/.mp3"));
  // 32 is the limit, so this pair is the boundary either side of it.
  TEST_ASSERT_TRUE(assets::uploadNameOk("/MP3/exactly-thirty-two-characters-ab.mp3"));
  TEST_ASSERT_FALSE(assets::uploadNameOk("/MP3/one-over-the-thirty-two-char-limit.mp3"));
  // Other folders keep their own rules; this one is about MP3 names only.
  TEST_ASSERT_TRUE(assets::uploadNameOk("/ICONS/two words.gif"));
  TEST_ASSERT_TRUE(assets::uploadNameOk("/MELODIES/two words.txt"));
}

void setUp() {}
void tearDown() {}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_public_dirs_allowed);
  RUN_TEST(test_traversal_rejected);
  RUN_TEST(test_absolute_escape_rejected);
  RUN_TEST(test_scripts_dir_is_not_public);
  RUN_TEST(test_no_traversal_into_scripts_dir);
  RUN_TEST(test_lookalike_prefixes_rejected);
  RUN_TEST(test_empty_rejected);
  RUN_TEST(test_backup_readable_covers_scripts_and_apploop);
  RUN_TEST(test_backup_writable_adds_scripts_only);
  RUN_TEST(test_kind_follows_the_folder);
  RUN_TEST(test_sounds_accept_mp3_and_reject_the_rest);
  RUN_TEST(test_icons_accept_gif_and_jpeg);
  RUN_TEST(test_icons_reject_png_whatever_it_is_called);
  RUN_TEST(test_icons_reject_text_and_truncated_headers);
  RUN_TEST(test_text_folders_accept_text_and_reject_binary);
  RUN_TEST(test_melody_upload_must_parse);
  RUN_TEST(test_sound_upload_name_must_be_playable);
  UNITY_END();
  return 0;
}
