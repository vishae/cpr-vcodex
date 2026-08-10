#pragma once
#include <I18n.h>

#include <functional>
#include <string>
#include <vector>

#include "CrossPointSettings.h"
#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

struct Rect;

enum class SettingType { TOGGLE, ENUM, ACTION, VALUE, STRING, SECTION };

enum class SettingAction {
  None,
  RemapFrontButtons,
  CustomiseStatusBar,
  KOReaderSync,
  OPDSBrowser,
  Network,
  ClearCache,
  CheckForUpdates,
  SdFirmwareUpdate,
  Language,
  SyncDay,
  ClockSync,
  TimeZone,
  ReadingStats,
  ResetReadingStats,
  ExportReadingStats,
  ImportReadingStats,
  ClearReadingStatsBackups,
  ReadingHeatmap,
  ReadingProfile,
  Achievements,
  ResetAchievements,
  SyncAchievementsFromStats,
  ShortcutLocation,
  ShortcutVisibility,
  OrderHomeShortcuts,
  OrderAppsShortcuts,
  Bookmarks,
  Favorites,
  Flashcards,
  ScreenClean,
  SleepApp,
  IfFound,
  DownloadFonts,
  LibraryFolder,
  GenerateMosaicMetadata,
  DeleteMosaicCovers,
  // Open an app's own settings page (CGV-016). One action per page rather than a
  // single action plus a payload field — SettingInfo has nowhere to carry the
  // payload, and the switch already dispatches by action.
  AppPageSyncDay,
  AppPageCoverGrid,
  AppPageReadingStats,
  AppPageAchievements,
  AppPageShortcuts,
};

// Which app's settings page a SettingsActivity instance is showing. `None` is the
// ordinary tabbed settings screen; anything else is a single app's page, opened
// from the Apps tab (CGV-016).
enum class AppSettingsPage { None, SyncDay, CoverGrid, ReadingStats, Achievements, Shortcuts };

struct SettingInfo {
  StrId nameId;
  SettingType type;
  uint8_t CrossPointSettings::* valuePtr = nullptr;
  std::vector<StrId> enumValues;
  SettingAction action = SettingAction::None;

  struct ValueRange {
    uint8_t min;
    uint8_t max;
    uint8_t step;
  };
  ValueRange valueRange = {};

  const char* key = nullptr;             // JSON API key (nullptr for ACTION types)
  StrId category = StrId::STR_NONE_OPT;  // Category for web UI grouping
  bool obfuscated = false;               // Save/load via base64 obfuscation (passwords)

  // Direct char[] string fields (for settings stored in CrossPointSettings)
  size_t stringOffset = 0;
  size_t stringMaxLen = 0;

  // Dynamic accessors (for settings stored outside CrossPointSettings, e.g. KOReaderCredentialStore)
  std::function<uint8_t()> valueGetter;
  std::function<void(uint8_t)> valueSetter;
  std::function<std::string()> stringGetter;
  std::function<void(const std::string&)> stringSetter;

  SettingInfo& withObfuscated() {
    obfuscated = true;
    return *this;
  }

  static SettingInfo Toggle(StrId nameId, uint8_t CrossPointSettings::* ptr, const char* key = nullptr,
                            StrId category = StrId::STR_NONE_OPT) {
    SettingInfo s;
    s.nameId = nameId;
    s.type = SettingType::TOGGLE;
    s.valuePtr = ptr;
    s.key = key;
    s.category = category;
    return s;
  }

  static SettingInfo Enum(StrId nameId, uint8_t CrossPointSettings::* ptr, std::vector<StrId> values,
                          const char* key = nullptr, StrId category = StrId::STR_NONE_OPT) {
    SettingInfo s;
    s.nameId = nameId;
    s.type = SettingType::ENUM;
    s.valuePtr = ptr;
    s.enumValues = std::move(values);
    s.key = key;
    s.category = category;
    return s;
  }

  static SettingInfo Action(StrId nameId, SettingAction action) {
    SettingInfo s;
    s.nameId = nameId;
    s.type = SettingType::ACTION;
    s.action = action;
    return s;
  }

  static SettingInfo Section(StrId nameId) {
    SettingInfo s;
    s.nameId = nameId;
    s.type = SettingType::SECTION;
    return s;
  }

  static SettingInfo Value(StrId nameId, uint8_t CrossPointSettings::* ptr, const ValueRange valueRange,
                           const char* key = nullptr, StrId category = StrId::STR_NONE_OPT) {
    SettingInfo s;
    s.nameId = nameId;
    s.type = SettingType::VALUE;
    s.valuePtr = ptr;
    s.valueRange = valueRange;
    s.key = key;
    s.category = category;
    return s;
  }

  static SettingInfo String(StrId nameId, char* ptr, size_t maxLen, const char* key = nullptr,
                            StrId category = StrId::STR_NONE_OPT) {
    SettingInfo s;
    s.nameId = nameId;
    s.type = SettingType::STRING;
    s.stringOffset = (size_t)ptr - (size_t)&SETTINGS;
    s.stringMaxLen = maxLen;
    s.key = key;
    s.category = category;
    return s;
  }

  static SettingInfo DynamicEnum(StrId nameId, std::vector<StrId> values, std::function<uint8_t()> getter,
                                 std::function<void(uint8_t)> setter, const char* key = nullptr,
                                 StrId category = StrId::STR_NONE_OPT) {
    SettingInfo s;
    s.nameId = nameId;
    s.type = SettingType::ENUM;
    s.enumValues = std::move(values);
    s.valueGetter = std::move(getter);
    s.valueSetter = std::move(setter);
    s.key = key;
    s.category = category;
    return s;
  }

  static SettingInfo DynamicString(StrId nameId, std::function<std::string()> getter,
                                   std::function<void(const std::string&)> setter, const char* key = nullptr,
                                   StrId category = StrId::STR_NONE_OPT) {
    SettingInfo s;
    s.nameId = nameId;
    s.type = SettingType::STRING;
    s.stringGetter = std::move(getter);
    s.stringSetter = std::move(setter);
    s.key = key;
    s.category = category;
    return s;
  }
};

class SettingsActivity final : public Activity {
  using SettingRef = const SettingInfo*;

  ButtonNavigator buttonNavigator;

  int selectedCategoryIndex = 0;  // Currently selected category
  int selectedSettingIndex = 0;
  int settingsCount = 0;

  // Per-category settings derived from shared list + device-only actions
  std::vector<SettingRef> displaySettings;
  std::vector<SettingRef> readerSettings;
  std::vector<SettingRef> controlsSettings;
  std::vector<SettingRef> systemSettings;
  std::vector<SettingRef> appSettings;
  std::vector<SettingInfo> appSettingOverrides;
  std::vector<SettingRef> pageSettings;
  const std::vector<SettingRef>* currentSettings = nullptr;
  bool settingsListsBuilt = false;

  // Page mode (CGV-016): when set, this instance shows one app's settings instead
  // of the tabbed screen. Row 0 stays the header row in both modes so the
  // selection arithmetic below is shared; only what row 0 *does* differs.
  AppSettingsPage appPage = AppSettingsPage::None;
  bool isAppPage() const { return appPage != AppSettingsPage::None; }
  StrId appPageTitleId() const;
  const std::vector<SettingInfo>& appPageSource() const;
  void buildPageSettingsList();

  static constexpr int categoryCount = 5;
  static const StrId categoryNames[categoryCount];

  // One-shot guard for long-press-Back-at-tab-row -> previous tab (avoids
  // repeat-firing every tick while the button stays held past the threshold).
  bool prevTabLongPressHandled = false;

  void enterCategory(int categoryIndex);
  bool isSelectableSetting(int settingIndex) const;
  int firstSelectableSettingIndex() const;
  int stepSettingSelection(int direction) const;
  bool prewarmSettingsRenderText(const char* settingsTitle, const char* selectedCategoryLabel,
                                 const char* firmwareVersion, const char* confirmLabel) const;
  void showTransientPopup(const char* message, int progress = -1, unsigned long delayMs = 0);
  void toggleCurrentSetting();
  void buildSettingsLists();
  void rebuildAppSettingsList();

 public:
  explicit SettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("Settings", renderer, mappedInput) {}
  // Page-mode constructor: one app's settings, no tab bar (CGV-016).
  SettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, AppSettingsPage page)
      : Activity("Settings", renderer, mappedInput), appPage(page) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  uint8_t getUiTransitionRefreshWeight() const override { return UI_TRANSITION_REFRESH_WEIGHT_DENSE; }
};
