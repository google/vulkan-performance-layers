#include <cstdio>
#include <filesystem>
#include <utility>

#include "gtest/gtest.h"
#include "log_scanner.h"

namespace {

using namespace performancelayers;
namespace fs = std::filesystem;

static std::pair<FILE*, fs::path> make_tmp_file(const char* filename) {
  fs::path tmp_dir = fs::temp_directory_path();
  fs::path tmp_filename = tmp_dir / filename;
  return {fopen(tmp_filename.c_str(), "w"), tmp_filename};
}

TEST(LogScanner, FileNotFound) {
  fs::path non_existent = "/definitely/nothing/here/asdf";
  ASSERT_FALSE(fs::exists(non_existent));
  std::optional<LogScanner> scanner = LogScanner::FromFilename(non_existent);
  EXPECT_EQ(scanner, std::nullopt);
}

TEST(LogScanner, MakeEmptyFile) {
  FILE* empty;
  fs::path path;
  std::tie(empty, path) = make_tmp_file("empty.log");
  ASSERT_TRUE(empty != nullptr);
  fclose(empty);

  EXPECT_TRUE(fs::exists(path));
  EXPECT_EQ(fs::file_size(path), 0);
  fs::remove(path);
  EXPECT_FALSE(fs::exists(path));
}

TEST(LogScanner, OneLineNoMatch) {
  FILE* file;
  fs::path path;
  std::tie(file, path) = make_tmp_file("one_line_no_match.log");
  ASSERT_TRUE(file != nullptr);

  auto scanner = LogScanner::FromFilename(path);
  EXPECT_TRUE(scanner.has_value());
  scanner->RegisterWatchedPattern("ddd");
  EXPECT_FALSE(scanner->ConsumeNewLines());

  fprintf(file, "rrr tttt\n");
  fflush(file);
  EXPECT_FALSE(scanner->ConsumeNewLines());
  EXPECT_EQ(scanner->GetSeenPatterns().size(), 0u);
  fclose(file);
  fs::remove(path);
  scanner.reset();
  ASSERT_EQ(scanner, std::nullopt);
}

TEST(LogScanner, OneLineMatch) {
  FILE* file;
  fs::path path;
  std::tie(file, path) = make_tmp_file("one_line_match.log");
  ASSERT_TRUE(file != nullptr);

  auto scanner = LogScanner::FromFilename(path);
  EXPECT_TRUE(scanner.has_value());

  const char* pattern = "ddd";
  scanner->RegisterWatchedPattern(pattern);
  EXPECT_FALSE(scanner->ConsumeNewLines());

  fprintf(file, "rrr ddd\n");
  fflush(file);
  EXPECT_TRUE(scanner->ConsumeNewLines());
  EXPECT_EQ(scanner->GetFirstOccurrenceLineNum(pattern), 1u);

  auto seen_patterns = scanner->GetSeenPatterns();
  EXPECT_EQ(seen_patterns.size(), 1u);
  EXPECT_EQ(seen_patterns.front().first, std::string(pattern));
  EXPECT_EQ(seen_patterns.front().second, 1u);

  fclose(file);
  fs::remove(path);
}

TEST(LogScanner, SecondLineMatch) {
  FILE* file;
  fs::path path;
  std::tie(file, path) = make_tmp_file("second_line_match.log");
  ASSERT_TRUE(file != nullptr);

  auto scanner = LogScanner::FromFilename(path);
  EXPECT_TRUE(scanner.has_value());

  const char* pattern = "ddd";
  scanner->RegisterWatchedPattern(pattern);
  EXPECT_FALSE(scanner->ConsumeNewLines());

  fprintf(file, "rrr kkkk\n");
  fflush(file);
  EXPECT_FALSE(scanner->ConsumeNewLines());
  EXPECT_EQ(scanner->GetFirstOccurrenceLineNum(pattern), 0u);

  fprintf(file, "fff dddd\n");
  fflush(file);
  EXPECT_TRUE(scanner->ConsumeNewLines());
  EXPECT_EQ(scanner->GetFirstOccurrenceLineNum(pattern), 2u);

  auto seen_patterns = scanner->GetSeenPatterns();
  EXPECT_EQ(seen_patterns.size(), 1u);

  fclose(file);
  fs::remove(path);
}

TEST(LogScanner, ThreeLinesTwoMatches) {
  FILE* file;
  fs::path path;
  std::tie(file, path) = make_tmp_file("three_lines_two_matches.log");
  ASSERT_TRUE(file != nullptr);

  auto scanner = LogScanner::FromFilename(path);
  EXPECT_TRUE(scanner.has_value());

  const char* pattern1 = "ddd";
  const char* pattern2 = "eeee";
  const char* pattern3 = "pp pp";
  scanner->RegisterWatchedPattern(pattern1);
  scanner->RegisterWatchedPattern(pattern2);
  scanner->RegisterWatchedPattern(pattern3);
  EXPECT_FALSE(scanner->ConsumeNewLines());

  fprintf(file, "rrr ddd\n");
  fflush(file);
  EXPECT_TRUE(scanner->ConsumeNewLines());
  EXPECT_EQ(scanner->GetFirstOccurrenceLineNum(pattern1), 1u);

  fprintf(file, "ccccc vvvvv\n");
  fflush(file);
  EXPECT_FALSE(scanner->ConsumeNewLines());

  fprintf(file, "pp pp\n");
  fflush(file);
  EXPECT_TRUE(scanner->ConsumeNewLines());
  EXPECT_EQ(scanner->GetFirstOccurrenceLineNum(pattern1), 1u);
  EXPECT_EQ(scanner->GetFirstOccurrenceLineNum(pattern3), 3u);

  auto seen_patterns = scanner->GetSeenPatterns();
  EXPECT_EQ(seen_patterns.size(), 2u);

  fclose(file);
  fs::remove(path);
}

TEST(LogScanner, NoNewline) {
  FILE* file;
  fs::path path;
  std::tie(file, path) = make_tmp_file("no_newline.log");
  ASSERT_TRUE(file != nullptr);

  auto scanner = LogScanner::FromFilename(path);
  EXPECT_TRUE(scanner.has_value());

  const char* pattern1 = "ddd";
  const char* pattern2 = "xxx";
  scanner->RegisterWatchedPattern(pattern1);
  scanner->RegisterWatchedPattern(pattern2);
  EXPECT_FALSE(scanner->ConsumeNewLines());

  fprintf(file, "rrr ddd xxx");
  fflush(file);
  EXPECT_TRUE(scanner->ConsumeNewLines());
  EXPECT_EQ(scanner->GetFirstOccurrenceLineNum(pattern1), 1u);
  EXPECT_EQ(scanner->GetFirstOccurrenceLineNum(pattern2), 1u);

  fclose(file);
  fs::remove(path);
}

}  // namespace
