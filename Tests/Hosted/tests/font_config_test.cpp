#include "../../../Apps/Fontloader/font_load_result.hpp"
#include <catch2/catch_test_macros.hpp>
#include <process_startup.hpp>

#include "../../../Apps/Init/font_config.hpp"

#include <cstring>
#include <string>
#include <vector>

namespace {
constexpr char DefaultConfig[] =
    "[Fonts]\n"
    "Backend=truetype\n"
    "Regular=/System/Fonts/SSR.TTF\n"
    "Bold=/System/Fonts/SSB.TTF\n"
    "SizePt=11\n";

bool parse(const std::string &text, init::FontConfiguration &config) {
  return init::parse_font_config(text.data(), text.size(), config);
}

std::string config_with(const std::string &regular, const std::string &bold,
                        const std::string &size = "11") {
  return "[Fonts]\nBackend=truetype\nRegular=" + regular + "\nBold=" + bold +
         "\nSizePt=" + size + "\n";
}

bool same(const init::FontConfiguration &left, const init::FontConfiguration &right) {
  for (size_t i = 0; i < sizeof(left.regular); ++i)
    if (left.regular[i] != right.regular[i]) return false;
  for (size_t i = 0; i < sizeof(left.bold); ++i)
    if (left.bold[i] != right.bold[i]) return false;
  return left.backend == right.backend && left.sizePt == right.sizePt &&
         left.ppem == right.ppem;
}

bool rejected_without_mutation(const std::string &text) {
  init::FontConfiguration output{};
  output.regular[0] = '!';
  output.bold[0] = '?';
  output.sizePt = 73;
  output.ppem = 99;
  const auto original = output;
  return !parse(text, output) && same(output, original);
}
}

TEST_CASE("Font configuration parses the shipped defaults and integer conversion", "[font-config]") {
  init::FontConfiguration config{};
  REQUIRE(parse(DefaultConfig, config));
  REQUIRE(std::string(config.regular) == "/System/Fonts/SSR.TTF");
  REQUIRE(std::string(config.bold) == "/System/Fonts/SSB.TTF");
  REQUIRE(config.sizePt == 11);
  REQUIRE(config.ppem == 15);
  REQUIRE(config.backend == init::FontBackend::TrueType);

  uint32_t ppem = 0;
  REQUIRE(init::point_size_to_ppem(9, ppem));
  REQUIRE(ppem == 12);
  REQUIRE(init::point_size_to_ppem(192, ppem));
  REQUIRE(ppem == 256);
  REQUIRE_FALSE(init::point_size_to_ppem(0, ppem));
  REQUIRE_FALSE(init::point_size_to_ppem(193, ppem));
}

TEST_CASE("Font configuration accepts bounded comments, whitespace and quoted paths", "[font-config]") {
  const std::string text =
      " \t; packaged fonts\r\n"
      "[ Fonts ]\r\n"
      " Backend \t = \t truetype \r\n"
      " Regular \t = \t\"/System/Font Files/SSR.TTF\"  \r\n"
      "# bold face\r\n"
      "Bold=\"/System/Font Files/SSB.TTF\"\r\n"
      "SizePt \t= 12 \r\n";
  init::FontConfiguration config{};
  REQUIRE(parse(text, config));
  REQUIRE(std::string(config.regular) == "/System/Font Files/SSR.TTF");
  REQUIRE(std::string(config.bold) == "/System/Font Files/SSB.TTF");
  REQUIRE(config.sizePt == 12);
  REQUIRE(config.ppem == 16);
}

TEST_CASE("Font configuration requires one exact section and each case-sensitive key", "[font-config]") {
  const std::string valid = DefaultConfig;
  const std::string values =
      "Regular=/System/Fonts/SSR.TTF\n"
      "Bold=/System/Fonts/SSB.TTF\n"
      "SizePt=11\n";
  for (const auto &invalid : std::vector<std::string>{
           valid + "[Fonts]\n",
           std::string("[Other]\n") + valid,
           valid + "[Other]\n",
           std::string("Regular=/System/Fonts/SSR.TTF\n") + valid,
           std::string("[Fonts]\n[Fonts]\n") + valid.substr(valid.find('\n') + 1),
           std::string("[Fonts]\nregular=/System/Fonts/SSR.TTF\n") +
               valid.substr(valid.find("Bold=")),
           std::string("[Fonts]\nBackend=truetype\nbackend=bitmap\n") +
               valid.substr(valid.find("Regular=")),
           std::string("[Fonts]\nBackend=other\n") + valid.substr(valid.find("Regular=")),
           std::string("[Fonts]\nUnknown=x\n") + valid.substr(valid.find("Regular=")),
           std::string("[Fonts]\nRegular=/System/Fonts/SSR.TTF\n") + valid.substr(valid.find("Regular=")),
           std::string("[Fonts]\n") + values + "Bold=/System/Fonts/SSB.TTF\n",
           std::string("[Fonts]\n") + values + "SizePt=11\n",
           std::string("[Fonts]\nBackend=truetype\nRegular=/System/Fonts/SSR.TTF\nBold=/System/Fonts/SSB.TTF\n"),
           std::string("[Fonts]\nBackend=bitmap\nRegular=/System/Fonts/SSR.TTF\n"),
           std::string("[Fonts]\nBackend=bitmap\nSizePt=11\n"),
           std::string("[Fonts]\nRegular=/System/Fonts/SSR.TTF\nBold=/System/Fonts/SSB.TTF\nSizePt=11\n"),
           std::string("[Fonts]\nRegular=/System/Fonts/SSR.TTF\nBold=/System/Fonts/SSB.TTF\nSizePt=11 ; inline\n")}) {
    INFO(invalid);
    REQUIRE(rejected_without_mutation(invalid));
  }
}

TEST_CASE("Font configuration enforces ASCII and the 1023-byte file limit", "[font-config]") {
  init::FontConfiguration sentinel{};
  sentinel.regular[0] = '!';
  sentinel.bold[0] = '?';
  sentinel.sizePt = 73;
  sentinel.ppem = 99;
  const auto original = sentinel;
  REQUIRE_FALSE(init::parse_font_config(nullptr, 0, sentinel));
  REQUIRE(same(sentinel, original));
  REQUIRE_FALSE(init::parse_font_config(nullptr, 1, sentinel));
  REQUIRE(same(sentinel, original));
  REQUIRE_FALSE(init::parse_font_config("", 0, sentinel));
  REQUIRE(same(sentinel, original));

  std::string maximum = DefaultConfig;
  maximum += '#';
  maximum.append(1023 - maximum.size(), 'x');
  REQUIRE(maximum.size() == 1023);
  init::FontConfiguration config{};
  REQUIRE(parse(maximum, config));

  maximum += 'x';
  REQUIRE(rejected_without_mutation(maximum));
  std::string nul = DefaultConfig;
  nul.insert(nul.find("SSR"), 1, '\0');
  REQUIRE(rejected_without_mutation(nul));
  std::string bom = DefaultConfig;
  bom.insert(0, "\xef\xbb\xbf");
  REQUIRE(rejected_without_mutation(bom));
  std::string nonAscii = DefaultConfig;
  nonAscii.insert(nonAscii.find("SSR"), 1, static_cast<char>(0x80));
  REQUIRE(rejected_without_mutation(nonAscii));
  std::string bareCarriageReturn = DefaultConfig;
  bareCarriageReturn.insert(bareCarriageReturn.find("Regular"), 1, '\r');
  REQUIRE(rejected_without_mutation(bareCarriageReturn));
}

TEST_CASE("Font configuration validates absolute paths and quoted argument construction", "[font-config]") {
  const std::string path255 = "/" + std::string(254, 'x');
  const auto maximumPath = config_with(path255, "/System/Fonts/SSB.TTF");
  init::FontConfiguration config{};
  REQUIRE(parse(maximumPath, config));
  REQUIRE(std::string(config.regular).size() == 255);
  char arguments[1024]{};
  REQUIRE(init::make_font_loader_arguments(config, arguments, sizeof(arguments)));
  REQUIRE(arguments[0] == '"');
  REQUIRE(std::string(arguments).find("\" \"/System/Fonts/SSB.TTF\" 15") != std::string::npos);

  const std::string quotedPath255 = "\"" + std::string("/") + std::string(253, 'x') + " " + "\"";
  REQUIRE(quotedPath255.size() == 257);
  REQUIRE(parse(config_with(quotedPath255, "/System/Fonts/SSB.TTF"), config));
  REQUIRE(std::string(config.regular).size() == 255);
  REQUIRE(std::string(config.regular).back() == ' ');

  REQUIRE(parse(config_with("/", "/System/Fonts/SSB.TTF"), config));
  REQUIRE(std::string(config.regular) == "/");

  const auto tooLong = config_with("/" + std::string(255, 'x'), "/System/Fonts/SSB.TTF");
  REQUIRE(rejected_without_mutation(tooLong));
  for (const auto &regular : {"", "relative.ttf", "/has space.ttf", "/has\tab.ttf",
                              "/has\"quote.ttf", "/has\nline.ttf"}) {
    const auto invalid = config_with(regular, "/System/Fonts/SSB.TTF");
    INFO(regular);
    REQUIRE(rejected_without_mutation(invalid));
  }
  REQUIRE(rejected_without_mutation(config_with("\"/unclosed", "/System/Fonts/SSB.TTF")));
  REQUIRE(rejected_without_mutation(config_with("\"/has\"quote\"", "/System/Fonts/SSB.TTF")));
  REQUIRE(rejected_without_mutation(config_with(std::string("/has") + char(1) + "control",
                                                "/System/Fonts/SSB.TTF")));
  REQUIRE(rejected_without_mutation(config_with("\"/has\tcontrol\"", "/System/Fonts/SSB.TTF")));
  REQUIRE(rejected_without_mutation(config_with(std::string("\"/has") + char(0x7f) +
                                                "control\"", "/System/Fonts/SSB.TTF")));

  const auto spaced = config_with("\"/System/Font Files/SSR.TTF\"",
                                  "\"/System/Font Files/SSB.TTF\"", "12");
  REQUIRE(parse(spaced, config));
  char spacedArguments[1024]{};
  REQUIRE(init::make_font_loader_arguments(config, spacedArguments, sizeof(spacedArguments)));
  REQUIRE(std::string(spacedArguments) ==
          "\"/System/Font Files/SSR.TTF\" \"/System/Font Files/SSB.TTF\" 16");
  kernel::process_startup::Arguments parsedArguments{};
  constexpr char loaderPath[] = "/System/fontload.elf";
  REQUIRE_FALSE(kernel::process_startup::parse(
      loaderPath, sizeof(loaderPath) - 1, spacedArguments,
      std::strlen(spacedArguments), parsedArguments));
  REQUIRE(parsedArguments.argc == 4);
  REQUIRE(std::string(parsedArguments.text + parsedArguments.offsets[1]) ==
          "/System/Font Files/SSR.TTF");
  REQUIRE(std::string(parsedArguments.text + parsedArguments.offsets[2]) ==
          "/System/Font Files/SSB.TTF");
  REQUIRE(std::string(parsedArguments.text + parsedArguments.offsets[3]) == "16");
  char diagnostic[640]{};
  REQUIRE(init::make_font_config_diagnostic(config, diagnostic, sizeof(diagnostic)));
  REQUIRE(std::string(diagnostic) ==
          "init: default font config backend=truetype regular=\"/System/Font Files/SSR.TTF\" "
          "bold=\"/System/Font Files/SSB.TTF\" size=12pt ppem=16");
  char tooSmall[8]{};
  REQUIRE_FALSE(init::make_font_loader_arguments(config, tooSmall, sizeof(tooSmall)));
}

TEST_CASE("Font configuration selects the built-in bitmap system font", "[font-config]") {
  init::FontConfiguration config{};
  REQUIRE(parse("[Fonts]\nBackend=bitmap\n", config));
  REQUIRE(config.backend == init::FontBackend::BuiltinBitmap);
  REQUIRE(config.regular[0] == '\0');
  REQUIRE(config.bold[0] == '\0');
  REQUIRE(config.sizePt == 0);
  REQUIRE(config.ppem == 0);
  char arguments[32] = "unchanged";
  REQUIRE_FALSE(init::make_font_loader_arguments(config, arguments, sizeof(arguments)));
  REQUIRE(arguments[0] == '\0');
  char diagnostic[80]{};
  REQUIRE(init::make_font_config_diagnostic(config, diagnostic, sizeof(diagnostic)));
  REQUIRE(std::string(diagnostic) == "init: default font config backend=bitmap");
  char tooSmall[8]{};
  REQUIRE_FALSE(init::make_font_config_diagnostic(config, tooSmall, sizeof(tooSmall)));
}

TEST_CASE("Font configuration accepts only positive integer point sizes in range", "[font-config]") {
  for (const auto *size : {"0", "193", "+11", "-1", "1.5", "11pt", "", "1 1"}) {
    const auto text = config_with("/System/Fonts/SSR.TTF", "/System/Fonts/SSB.TTF", size);
    INFO(size);
    REQUIRE(rejected_without_mutation(text));
  }
}

TEST_CASE("Font fallback classifies only file absence and kernel upload OOM", "[font-config][fallback]") {
  using namespace fontloader;
  REQUIRE(open_result(SyscallReturnCode_NOT_FOUND) == LoadResult::ConfiguredFontNotFound);
  REQUIRE(open_result(SyscallReturnCode_OUT_OF_MEMORY) == LoadResult::Fatal);
  REQUIRE(open_result(SyscallReturnCode_UNKNOWN) == LoadResult::Fatal);
  REQUIRE(open_result(SyscallReturnCode_INVALID_ARGUMENT) == LoadResult::Fatal);
  REQUIRE(upload_result(SyscallReturnCode_OUT_OF_MEMORY) == LoadResult::UploadOutOfMemory);
  REQUIRE(upload_result(SyscallReturnCode_NOT_FOUND) == LoadResult::Fatal);
  REQUIRE(upload_result(SyscallReturnCode_INVALID_ARGUMENT) == LoadResult::Fatal);
  REQUIRE(upload_result(SyscallReturnCode_UNKNOWN) == LoadResult::Fatal);
  REQUIRE_FALSE(fallback_eligible(uint32_t(LoadResult::Fatal)));
  REQUIRE_FALSE(fallback_eligible(99));
}

TEST_CASE("Font pair fallback closes regular ownership before bitmap publication", "[font-config][fallback]") {
  using namespace fontloader;
  for (auto failure : {LoadResult::ConfiguredFontNotFound, LoadResult::UploadOutOfMemory, LoadResult::Fatal}) {
    uint32_t regular = 0, bold = 0;
    std::vector<std::string> events;
    auto result = load_pair("regular", "bold", regular, bold,
        [&](const char *path, uint32_t &handle) {
          events.emplace_back(path);
          if (std::strcmp(path, "regular") == 0) { handle = 7; return LoadResult::Success; }
          return failure;
        }, [&](uint32_t handle) { REQUIRE(handle == 7); events.emplace_back("close regular"); return true; });
    REQUIRE(result == failure);
    REQUIRE(events == std::vector<std::string>{"regular", "bold", "close regular"});
    bool published = false;
    const bool ready = finish_startup(uint32_t(result), [&] {
      published = true;
      events.emplace_back("publish bitmap");
      return true;
    }, [&](const char *message) { events.emplace_back(message); });
    REQUIRE(ready == (failure != LoadResult::Fatal));
    REQUIRE(published == ready);
    if (ready) REQUIRE(events.back() == "init: bitmap fallback selected");
  }
  uint32_t regular = 0, bold = 0;
  const auto rollbackFailed = load_pair("regular", "bold", regular, bold,
      [&](const char *path, uint32_t &handle) {
        if (std::strcmp(path, "regular") == 0) { handle = 7; return LoadResult::Success; }
        return LoadResult::ConfiguredFontNotFound;
      }, [](uint32_t) { return false; });
  REQUIRE(rollbackFailed == LoadResult::Fatal);
}

TEST_CASE("Bitmap fallback publication failure blocks startup and never logs success", "[font-config][fallback]") {
  using namespace fontloader;
  for (auto result : {LoadResult::ConfiguredFontNotFound, LoadResult::UploadOutOfMemory}) {
    std::vector<std::string> log;
    REQUIRE_FALSE(finish_startup(uint32_t(result), [] { return false; },
        [&](const char *message) { log.emplace_back(message); }));
    REQUIRE(log.size() == 2);
    REQUIRE(log.back() == "init: cannot publish fallback bitmap font; desktop not started");
  }
  bool published = false;
  REQUIRE(finish_startup(uint32_t(LoadResult::Success), [&] { published = true; return false; },
      [](const char *) { FAIL("Successful scalable startup must not log fallback"); }));
  REQUIRE_FALSE(published);
}

TEST_CASE("Upload failure cleanup preserves eligible outcomes only after successful rollback", "[font-config][fallback]") {
  using namespace fontloader;
  uint8_t bytes[5000]{};
  for (unsigned failedStage = 0; failedStage < 3; ++failedStage) {
    for (const auto failure : {SyscallReturnCode_OUT_OF_MEMORY, SyscallReturnCode_UNKNOWN,
                               SyscallReturnCode_INVALID_ARGUMENT}) {
      uint32_t face = 0;
      unsigned closes = 0, appended = 0;
      const auto result = upload_resource(bytes, sizeof(bytes), face,
          [&](uint32_t size, uint32_t &upload) {
            REQUIRE(size == sizeof(bytes)); upload = 5;
            return failedStage == 0 ? failure : SyscallReturnCode_OKAY;
          },
          [&](uint32_t handle, uint32_t offset, const uint8_t *data, uint32_t size) {
            REQUIRE(handle == 5); REQUIRE(data == bytes + offset);
            REQUIRE(size <= 4096); ++appended;
            return failedStage == 1 ? failure : SyscallReturnCode_OKAY;
          },
          [&](uint32_t handle, uint32_t &, uint32_t &) {
            REQUIRE(handle == 5); REQUIRE(appended == 2);
            return failure;
          },
          [&](uint32_t handle) { REQUIRE(handle == 5); ++closes; return SyscallReturnCode_OKAY; },
          [](const char *, SyscallReturnCode) {});
      REQUIRE(result == (failure == SyscallReturnCode_OUT_OF_MEMORY ? LoadResult::UploadOutOfMemory : LoadResult::Fatal));
      REQUIRE(closes == (failedStage == 0 ? 0 : 1));
    }
  }
  for (bool commitFailure : {false, true}) {
    uint32_t face = 0;
    const auto result = upload_resource(bytes, sizeof(bytes), face,
        [](uint32_t, uint32_t &upload) { upload = 5; return SyscallReturnCode_OKAY; },
        [&](uint32_t, uint32_t, const uint8_t *, uint32_t) {
          return commitFailure ? SyscallReturnCode_OKAY : SyscallReturnCode_OUT_OF_MEMORY;
        },
        [](uint32_t, uint32_t &, uint32_t &) { return SyscallReturnCode_OUT_OF_MEMORY; },
        [](uint32_t) { return SyscallReturnCode_UNKNOWN; },
        [](const char *, SyscallReturnCode) {});
    REQUIRE(result == LoadResult::Fatal);
  }
}

TEST_CASE("Upload success transfers its capability and failed commit may already have released it", "[font-config][fallback]") {
  using namespace fontloader;
  uint8_t bytes[8]{};
  for (bool failedCommit : {false, true}) {
    uint32_t face = 0;
    unsigned closes = 0;
    const auto result = upload_resource(bytes, sizeof(bytes), face,
        [](uint32_t, uint32_t &upload) { upload = 5; return SyscallReturnCode_OKAY; },
        [](uint32_t, uint32_t, const uint8_t *, uint32_t) { return SyscallReturnCode_OKAY; },
        [&](uint32_t, uint32_t &output, uint32_t &) {
          if (failedCommit) return SyscallReturnCode_OUT_OF_MEMORY;
          output = 6; return SyscallReturnCode_OKAY;
        },
        [&](uint32_t handle) { REQUIRE(handle == 5); ++closes; return SyscallReturnCode_INVALID_ARGUMENT; },
        [](const char *, uint32_t) {});
    REQUIRE(result == (failedCommit ? LoadResult::UploadOutOfMemory : LoadResult::Success));
    REQUIRE(closes == (failedCommit ? 1 : 0));
    REQUIRE(face == (failedCommit ? 0 : 6));
  }
  uint32_t regular = 0, bold = 0;
  unsigned loads = 0;
  const auto absent = load_pair("regular", "bold", regular, bold,
      [&](const char *, uint32_t &) { ++loads; return LoadResult::ConfiguredFontNotFound; },
      [](uint32_t) { FAIL("Missing first font has no registered face to close"); return false; });
  REQUIRE(absent == LoadResult::ConfiguredFontNotFound);
  REQUIRE(loads == 1);
}
