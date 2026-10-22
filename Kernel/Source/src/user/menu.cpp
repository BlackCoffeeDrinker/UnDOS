#include "user/user.hpp"

#include <kernel/kobject/KProcessObject.hpp>

#include "user/bitmap_font.hpp"

namespace kernel::user {
namespace {
constexpr uint32_t kMenuIndexMask = 0xFFFF;
constexpr uint32_t kMenuGenerationShift = 16;
constexpr int32_t kMenuCellWidth = 8;
constexpr int32_t kMenuItemHeight = 20;
constexpr int32_t kMenuPopupBorder = 2;

struct MenuEntry {
  kstd::static_string<kMenuTextLen> text;
  uint32_t flags{0};
  uint32_t item{0};
};

struct MenuRecord {
  bool used{false};
  bool popup{false};
  uint16_t generation{0};
  KProcessObject *owner{nullptr};
  uint32_t parent{0};
  uint32_t count{0};
  MenuEntry entries[kMaxMenuItems]{};
};

struct OpenMenu {
  bool visible{false};
  uint32_t ownerWindow{0};
  uint32_t menu{0};
  uint32_t parentMenu{0};
  uint32_t parentIndex{0};
  int32_t x{0};
  int32_t y{0};
  int32_t width{0};
  int32_t selected{-1};
};

kstd::array<MenuRecord, kMaxMenus> g_menus;
OpenMenu g_openMenu;
OpenMenu g_menuStack[kMaxMenus]{};
uint32_t g_menuDepth{0};
bool g_menuMouseCapture{false};

void destroy_menu_locked(uint32_t handle) noexcept;

MenuRecord *lookup_menu_locked(uint32_t handle) noexcept {
  if (!handle) return nullptr;
  const uint32_t index = (handle & kMenuIndexMask) - 1;
  if (index >= kMaxMenus) return nullptr;
  auto &menu = g_menus[index];
  if (!menu.used || menu.generation != (handle >> kMenuGenerationShift)) return nullptr;
  return &menu;
}

uint32_t menu_handle_locked(const MenuRecord &menu) noexcept {
  for (uint32_t index = 0; index < kMaxMenus; ++index)
    if (&g_menus[index] == &menu)
      return (static_cast<uint32_t>(menu.generation) << kMenuGenerationShift) | (index + 1);
  return 0;
}

int32_t menu_entry_width(const MenuEntry &entry) noexcept {
  if (entry.flags & kMenuItemSeparator) return 8;
  return static_cast<int32_t>(entry.text.size()) * kMenuCellWidth + 16;
}

bool menu_contains_locked(uint32_t root, uint32_t target, uint32_t depth = 0) noexcept {
  if (root == target) return true;
  if (depth >= kMaxMenus) return false;
  auto *menu = lookup_menu_locked(root);
  if (!menu) return false;
  for (uint32_t i = 0; i < menu->count; ++i) {
    const auto &entry = menu->entries[i];
    if ((entry.flags & kMenuItemPopup) &&
        menu_contains_locked(entry.item, target, depth + 1)) return true;
  }
  return false;
}

void close_menu_locked() noexcept {
  if (!g_openMenu.visible) return;
  URect oldMenus[kMaxMenus + 1]{};
  uint32_t count = 0;
  auto add_rect = [&](const OpenMenu &menu) {
    if (!menu.visible || count == kMaxMenus + 1) return;
    oldMenus[count++] = {menu.x, menu.y, menu.x + menu.width,
                         menu.y + kMenuPopupBorder * 2 +
                             static_cast<int32_t>(menu_item_count_locked(menu.menu)) * kMenuItemHeight};
  };
  add_rect(g_openMenu);
  for (uint32_t i = 0; i < g_menuDepth; ++i) add_rect(g_menuStack[i]);
  g_openMenu = {};
  g_menuDepth = 0;
  for (uint32_t i = 0; i < count; ++i) expose_locked(oldMenus[i]);
}

} // namespace

uint32_t menu_item_count_locked(uint32_t handle) noexcept {
  const auto *menu = lookup_menu_locked(handle);
  return menu ? menu->count : 0;
}

bool menu_item_locked(uint32_t handle, uint32_t index, MenuItemView &item) noexcept {
  const auto *menu = lookup_menu_locked(handle);
  if (!menu || index >= menu->count) return false;
  const auto &entry = menu->entries[index];
  item.text = kstd::string_view{entry.text.data(), entry.text.size()};
  item.flags = entry.flags;
  item.item = entry.item;
  return true;
}

namespace {
void menu_draw_text(int32_t x, int32_t y, const kstd::string_view &text,
                    uint32_t foreground, uint32_t background, const URect &limit) noexcept {
  gfx::DrawBitmapText(x, y, text.data(), text.size(), foreground, background, true,
                      [&](int64_t left, int64_t top, int64_t right, int64_t bottom, uint32_t color) {
                        fill_screen_locked(URect{static_cast<int32_t>(left), static_cast<int32_t>(top),
                                                static_cast<int32_t>(right), static_cast<int32_t>(bottom)}
                                               .Intersect(limit),
                                           color);
                      });
}

bool menu_item_enabled(const MenuEntry &entry) noexcept {
  return !(entry.flags & (kMenuItemDisabled | kMenuItemGrayed | kMenuItemSeparator));
}

int32_t first_enabled_item(const MenuRecord &menu) noexcept {
  for (uint32_t i = 0; i < menu.count; ++i)
    if (menu_item_enabled(menu.entries[i])) return static_cast<int32_t>(i);
  return -1;
}

void open_popup_locked(uint32_t ownerWindow, uint32_t menuHandle, int32_t x, int32_t y,
                       uint32_t parentMenu, uint32_t parentIndex) noexcept {
  const auto *menu = lookup_menu_locked(menuHandle);
  auto *owner = lookup_locked(ownerWindow);
  if (!menu || !owner || !menu->popup || menu->owner != owner->process) return;

  int32_t width = 48;
  for (uint32_t i = 0; i < menu->count; ++i) {
    const auto itemWidth = static_cast<int32_t>(menu->entries[i].text.size()) * kMenuCellWidth + 24;
    if (itemWidth > width) width = itemWidth;
  }
  const auto bounds = screen_rect();
  const int32_t height = static_cast<int32_t>(menu->count) * kMenuItemHeight + kMenuPopupBorder * 2;
  if (x + width > bounds.right) x = bounds.right - width;
  if (y + height > bounds.bottom) y = bounds.bottom - height;
  if (x < bounds.left) x = bounds.left;
  if (y < bounds.top) y = bounds.top;

  const OpenMenu previous = g_openMenu;
  if (previous.visible && previous.menu == parentMenu && g_menuDepth < kMaxMenus)
    g_menuStack[g_menuDepth++] = previous;
  else
    g_menuDepth = 0;
  g_openMenu = {.visible = true,
                .ownerWindow = ownerWindow,
                .menu = menuHandle,
                .parentMenu = parentMenu,
                .parentIndex = parentIndex,
                .x = x,
                .y = y,
                .width = width,
                .selected = first_enabled_item(*menu)};
  if (previous.visible) {
    expose_locked({previous.x, previous.y, previous.x + previous.width,
                   previous.y + kMenuPopupBorder * 2 +
                       static_cast<int32_t>(menu_item_count_locked(previous.menu)) * kMenuItemHeight});
  }
  expose_locked({x, y, x + width, y + height});
}

void close_popup_level_locked() noexcept {
  if (!g_openMenu.visible) return;
  const URect old{g_openMenu.x, g_openMenu.y, g_openMenu.x + g_openMenu.width,
                  g_openMenu.y + kMenuPopupBorder * 2 +
                      static_cast<int32_t>(menu_item_count_locked(g_openMenu.menu)) * kMenuItemHeight};
  g_openMenu = g_menuDepth ? g_menuStack[--g_menuDepth] : OpenMenu{};
  expose_locked(old);
}

void open_top_level_popup_locked(KWindowObject &window, uint32_t index) noexcept {
  const auto *menu = lookup_menu_locked(window.menu);
  if (!menu || menu->popup || index >= menu->count) return;
  const auto &entry = menu->entries[index];
  if (!(entry.flags & kMenuItemPopup) || !menu_item_enabled(entry)) return;
  const int32_t border = (window.style & kStyleBorder) ? kBorderWidth : 0;
  const int32_t caption = (window.style & kStyleCaption) ? kCaptionHeight : 0;
  int32_t x = window.rect.left + border;
  for (uint32_t i = 0; i < index; ++i) x += menu_entry_width(menu->entries[i]);
  open_popup_locked(window.hwnd, entry.item, x,
                    window.rect.top + border + caption + kMenuBarHeight,
                    window.menu, index);
}

void switch_top_level_popup_locked(int32_t direction) noexcept {
  if (!g_openMenu.visible || g_menuDepth || !g_openMenu.parentMenu) return;
  auto *root = lookup_menu_locked(g_openMenu.parentMenu);
  auto *window = lookup_locked(g_openMenu.ownerWindow);
  if (!root || root->popup || !window || !root->count) return;
  int32_t index = static_cast<int32_t>(g_openMenu.parentIndex);
  for (uint32_t attempt = 0; attempt < root->count; ++attempt) {
    index = (index + direction + static_cast<int32_t>(root->count)) %
            static_cast<int32_t>(root->count);
    const auto &entry = root->entries[index];
    if ((entry.flags & kMenuItemPopup) && menu_item_enabled(entry)) {
      open_top_level_popup_locked(*window, static_cast<uint32_t>(index));
      return;
    }
  }
}

bool menu_bar_hit_locked(KWindowObject &window, int32_t x, int32_t y,
                         uint32_t &index, int32_t &itemLeft, int32_t &barBottom) noexcept {
  const auto *menu = lookup_menu_locked(window.menu);
  if (!menu || menu->popup) return false;
  const int32_t border = (window.style & kStyleBorder) ? kBorderWidth : 0;
  const int32_t caption = (window.style & kStyleCaption) ? kCaptionHeight : 0;
  const int32_t left = window.rect.left + border;
  const int32_t top = window.rect.top + border + caption;
  if (y < top || y >= top + kMenuBarHeight) return false;
  int32_t cursor = left;
  for (uint32_t i = 0; i < menu->count; ++i) {
    const int32_t width = menu_entry_width(menu->entries[i]);
    if (x >= cursor && x < cursor + width) {
      index = i;
      itemLeft = cursor;
      barBottom = top + kMenuBarHeight;
      return true;
    }
    cursor += width;
  }
  return false;
}

void select_menu_item_locked(uint32_t menuHandle, uint32_t index) noexcept {
  auto *menu = lookup_menu_locked(menuHandle);
  if (!menu || index >= menu->count) return;
  const auto entry = menu->entries[index];
  if (entry.flags & kMenuItemPopup) {
    const auto parent = g_openMenu;
    if (parent.visible) {
      const int32_t rowY = parent.y + kMenuPopupBorder +
                           static_cast<int32_t>(index) * kMenuItemHeight;
      open_popup_locked(parent.ownerWindow, entry.item, parent.x + parent.width - 2, rowY,
                        menuHandle, index);
    }
    return;
  }
  if (!menu_item_enabled(entry)) return;
  if (auto *owner = lookup_locked(g_openMenu.ownerWindow); owner && owner->queue)
    (void)post_locked(*owner->queue, owner->hwnd, kMsgCommand, entry.item, 0);
  close_menu_locked();
}

void destroy_menu_locked(uint32_t handle) noexcept {
  auto *menu = lookup_menu_locked(handle);
  if (!menu) return;
  if (g_openMenu.visible &&
      (menu_contains_locked(handle, g_openMenu.menu) || menu_contains_locked(g_openMenu.menu, handle)))
    close_menu_locked();
  detach_menu_from_windows_locked(handle);
  uint32_t children[kMaxMenuItems]{};
  uint32_t childCount = 0;
  for (uint32_t i = 0; i < menu->count; ++i)
    if ((menu->entries[i].flags & kMenuItemPopup) && childCount < kMaxMenuItems)
      children[childCount++] = menu->entries[i].item;
  const uint16_t generation = menu->generation;
  *menu = {};
  menu->generation = generation;
  for (uint32_t i = 0; i < childCount; ++i) destroy_menu_locked(children[i]);
}
} // namespace

namespace {
URect popup_bounds_locked(const OpenMenu &popup) noexcept {
  return {popup.x, popup.y, popup.x + popup.width,
          popup.y + kMenuPopupBorder * 2 +
              static_cast<int32_t>(menu_item_count_locked(popup.menu)) * kMenuItemHeight};
}

void draw_popup_locked(const OpenMenu &popup, const URect &limit) noexcept {
  const auto *menu = lookup_menu_locked(popup.menu);
  if (!popup.visible || !menu) return;
  const int32_t height = static_cast<int32_t>(menu->count) * kMenuItemHeight + kMenuPopupBorder * 2;
  const URect bounds{popup.x, popup.y, popup.x + popup.width, popup.y + height};
  const URect clipped = bounds.Intersect(limit);
  if (clipped.Empty()) return;
  auto fill_popup = [&](const URect &rect, uint32_t color) {
    fill_screen_locked(rect.Intersect(clipped), color);
  };
  fill_popup(bounds, 0);
  fill_popup({bounds.left + 1, bounds.top + 1, bounds.right - 1, bounds.bottom - 1}, 15);

  for (uint32_t i = 0; i < menu->count; ++i) {
    const auto &entry = menu->entries[i];
    const int32_t top = popup.y + kMenuPopupBorder +
                        static_cast<int32_t>(i) * kMenuItemHeight;
    const int32_t bottom = top + kMenuItemHeight;
    const bool enabled = menu_item_enabled(entry);
    const bool selected = static_cast<int32_t>(i) == popup.selected && enabled;
    const uint32_t background = selected ? kActiveCaptionColor : 15;
    const uint32_t foreground = enabled ? (selected ? 15 : 0) : 8;
    fill_popup({popup.x + 1, top, popup.x + popup.width - 1, bottom}, background);
    if (entry.flags & kMenuItemSeparator) {
      fill_popup({popup.x + 7, top + kMenuItemHeight / 2,
                  popup.x + popup.width - 7, top + kMenuItemHeight / 2 + 1}, 8);
    } else {
      menu_draw_text(popup.x + 8, top + 2,
                     {entry.text.data(), entry.text.size()}, foreground, background,
                     clipped);
      if (entry.flags & kMenuItemPopup) {
        fill_popup({popup.x + popup.width - 8, top + 7,
                    popup.x + popup.width - 5, top + 10}, foreground);
      }
    }
  }
}
} // namespace

void draw_open_menu_locked(const URect &limit) noexcept {
  if (!g_openMenu.visible) return;
  for (uint32_t i = 0; i < g_menuDepth; ++i) draw_popup_locked(g_menuStack[i], limit);
  draw_popup_locked(g_openMenu, limit);
}

void subtract_open_menus_locked(URegion &region) noexcept {
  if (!g_openMenu.visible) return;
  for (uint32_t i = 0; i < g_menuDepth; ++i)
    region.Subtract(popup_bounds_locked(g_menuStack[i]));
  region.Subtract(popup_bounds_locked(g_openMenu));
}

bool menu_mouse_locked(int32_t x, int32_t y, bool down) noexcept {
  if (g_menuMouseCapture && !down) {
    g_menuMouseCapture = false;
    return true;
  }

  if (g_openMenu.visible) {
    for (int32_t level = static_cast<int32_t>(g_menuDepth); level >= 0; --level) {
      const OpenMenu candidate = level == static_cast<int32_t>(g_menuDepth)
                                     ? g_openMenu
                                     : g_menuStack[level];
      const auto *menu = lookup_menu_locked(candidate.menu);
      if (!menu) {
        close_menu_locked();
        return true;
      }
      const int32_t height = static_cast<int32_t>(menu->count) * kMenuItemHeight +
                             kMenuPopupBorder * 2;
      const bool insidePopup = x >= candidate.x && x < candidate.x + candidate.width &&
                               y >= candidate.y && y < candidate.y + height;
      if (!insidePopup) continue;

      if (level != static_cast<int32_t>(g_menuDepth)) {
        const OpenMenu old = g_openMenu;
        g_openMenu = candidate;
        g_menuDepth = static_cast<uint32_t>(level);
        const auto oldHeight = kMenuPopupBorder * 2 +
                               static_cast<int32_t>(menu_item_count_locked(old.menu)) *
                                   kMenuItemHeight;
        expose_locked({old.x, old.y, old.x + old.width, old.y + oldHeight});
      }
      const int32_t row = (y - g_openMenu.y - kMenuPopupBorder) / kMenuItemHeight;
      const bool validRow = row >= 0 && static_cast<uint32_t>(row) < menu->count;
      const int32_t selected = validRow && menu_item_enabled(menu->entries[row]) ? row : -1;
      if (g_openMenu.selected != selected) {
        g_openMenu.selected = selected;
        expose_locked({g_openMenu.x, g_openMenu.y, g_openMenu.x + g_openMenu.width,
                       g_openMenu.y + height});
      }
      if (down) {
        g_menuMouseCapture = true;
        if (selected >= 0)
          select_menu_item_locked(g_openMenu.menu, static_cast<uint32_t>(selected));
      }
      return true;
    }
  }

  auto *window = window_from_point_locked(x, y);
  if (window) {
    uint32_t index = 0;
    int32_t itemLeft = 0, barBottom = 0;
    if (menu_bar_hit_locked(*window, x, y, index, itemLeft, barBottom)) {
      const auto *menu = lookup_menu_locked(window->menu);
      if (!menu) return false;
      const auto &entry = menu->entries[index];
      if (down) {
        (void) set_focus_locked(window->hwnd);
        g_menuMouseCapture = true;
        if ((entry.flags & kMenuItemPopup) && menu_item_enabled(entry)) {
          if (g_openMenu.visible && g_openMenu.menu == entry.item &&
              g_openMenu.parentMenu == window->menu) {
            close_menu_locked();
          } else {
            open_popup_locked(window->hwnd, entry.item, itemLeft, barBottom, window->menu, index);
          }
        } else if (menu_item_enabled(entry) && window->queue) {
          (void)post_locked(*window->queue, window->hwnd, kMsgCommand, entry.item, 0);
          close_menu_locked();
        }
      } else if (g_openMenu.visible && (entry.flags & kMenuItemPopup)) {
        if (menu_item_enabled(entry) && g_openMenu.menu != entry.item)
          open_popup_locked(window->hwnd, entry.item, itemLeft, barBottom, window->menu, index);
      }
      return true;
    }
  }

  if (down && g_openMenu.visible) {
    close_menu_locked();
    return false;
  }
  return g_openMenu.visible && !down;
}

bool menu_key_locked(uint32_t virtualKey, bool alt) noexcept {
  if (!g_openMenu.visible) {
    if (virtualKey != 0x79 && !(alt && virtualKey == 40)) return false;
    auto *window = focused_window_locked();
    for (uint32_t i = 0; window && !window->menu && i < kMaxWindows; ++i)
      window = lookup_locked(window->parent);
    if (!window) {
      window = active_window_locked();
      for (uint32_t i = 0; window && !window->menu && i < kMaxWindows; ++i)
        window = lookup_locked(window->parent);
    }
    if (!window || !effectively_visible_locked(*window) || !window->menu) return false;
    const auto *menu = lookup_menu_locked(window->menu);
    if (!menu || menu->popup) return false;
    for (uint32_t i = 0; i < menu->count; ++i) {
      if ((menu->entries[i].flags & kMenuItemPopup) && menu_item_enabled(menu->entries[i])) {
        open_top_level_popup_locked(*window, i);
        return true;
      }
    }
    return false;
  }
  auto *menu = lookup_menu_locked(g_openMenu.menu);
  if (!menu) {
    g_openMenu = {};
    return false;
  }
  if (virtualKey == 27) {
    if (g_menuDepth) close_popup_level_locked();
    else close_menu_locked();
    return true;
  }
  if (virtualKey == 38 || virtualKey == 40) {
    const int32_t direction = virtualKey == 38 ? -1 : 1;
    int32_t index = g_openMenu.selected;
    for (uint32_t attempt = 0; attempt < menu->count; ++attempt) {
      index = (index + direction + static_cast<int32_t>(menu->count)) %
              static_cast<int32_t>(menu->count);
      if (menu_item_enabled(menu->entries[index])) {
        g_openMenu.selected = index;
        break;
      }
    }
    expose_locked({g_openMenu.x, g_openMenu.y, g_openMenu.x + g_openMenu.width,
                   g_openMenu.y + static_cast<int32_t>(menu->count) * kMenuItemHeight +
                       kMenuPopupBorder * 2});
    return true;
  }
  if (virtualKey == 13 || virtualKey == 32) {
    if (g_openMenu.selected >= 0)
      select_menu_item_locked(g_openMenu.menu, static_cast<uint32_t>(g_openMenu.selected));
    return true;
  }
  if (virtualKey == 39) {
    if (g_openMenu.selected >= 0 &&
        (menu->entries[g_openMenu.selected].flags & kMenuItemPopup))
      select_menu_item_locked(g_openMenu.menu, static_cast<uint32_t>(g_openMenu.selected));
    else
      switch_top_level_popup_locked(1);
    return true;
  }
  if (virtualKey == 37) {
    if (g_menuDepth) close_popup_level_locked();
    else switch_top_level_popup_locked(-1);
    return true;
  }
  return false;
}

kstd::expected<uint32_t, kstd::error_code> create_menu(KProcessObject &caller, bool popup) noexcept {
  Guard guard;
  for (uint32_t i = 0; i < kMaxMenus; ++i) {
    auto &menu = g_menus[i];
    if (menu.used) continue;
    if (++menu.generation == 0) menu.generation = 1;
    menu.used = true;
    menu.popup = popup;
    menu.owner = &caller;
    menu.parent = 0;
    menu.count = 0;
    return (static_cast<uint32_t>(menu.generation) << kMenuGenerationShift) | (i + 1);
  }
  return kstd::unexpected(KernelError::ResourceLimitExceeded);
}

kstd::error_code destroy_menu(KProcessObject &caller, uint32_t handle) noexcept {
  Guard guard;
  auto *menu = lookup_menu_locked(handle);
  if (!menu) return KernelError::InvalidHandle;
  if (menu->owner != &caller) return KernelError::AccessDenied;
  if (menu->parent) return KernelError::InvalidState;
  destroy_menu_locked(handle);
  return {};
}

kstd::error_code append_menu(KProcessObject &caller, uint32_t handle, uint32_t flags, uint32_t item,
                             const kstd::string_view &text) noexcept {
  const uint32_t supportedFlags = kMenuItemGrayed | kMenuItemDisabled |
                                  kMenuItemPopup | kMenuItemSeparator;
  if (flags & ~supportedFlags) return KernelError::InvalidArgument;
  const bool separator = (flags & kMenuItemSeparator) != 0;
  const bool popup = (flags & kMenuItemPopup) != 0;
  if (separator && (popup || (flags & (kMenuItemGrayed | kMenuItemDisabled))))
    return KernelError::InvalidArgument;
  if (text.size() >= kMenuTextLen) return KernelError::InvalidSize;
  if (!separator && text.empty()) return KernelError::InvalidArgument;

  Guard guard;
  auto *menu = lookup_menu_locked(handle);
  if (!menu) return KernelError::InvalidHandle;
  if (menu->owner != &caller) return KernelError::AccessDenied;
  if (menu->count == kMaxMenuItems) return KernelError::ResourceLimitExceeded;
  if (popup) {
    auto *submenu = lookup_menu_locked(item);
    if (!submenu) return KernelError::InvalidHandle;
    if (submenu->owner != &caller) return KernelError::AccessDenied;
    if (!submenu->popup || submenu->parent || menu_contains_locked(item, handle))
      return KernelError::InvalidArgument;
    submenu->parent = handle;
  }

  auto &entry = menu->entries[menu->count++];
  entry.flags = flags;
  entry.item = item;
  entry.text = separator ? kstd::string_view{} : text;
  refresh_attached_menu_locked(handle);
  if (g_openMenu.visible &&
      (g_openMenu.menu == handle || menu_contains_locked(g_openMenu.menu, handle)))
    expose_locked({g_openMenu.x, g_openMenu.y, g_openMenu.x + g_openMenu.width,
                   g_openMenu.y + kMenuPopupBorder * 2 +
                       static_cast<int32_t>(menu_item_count_locked(g_openMenu.menu)) * kMenuItemHeight});
  return {};
}

void close_menu_for_window_locked(uint32_t hwnd) noexcept {
  if (!g_openMenu.visible) return;
  const auto *owner = lookup_locked(g_openMenu.ownerWindow);
  const auto *window = lookup_locked(hwnd);
  if (owner && window && (owner == window || descendant_locked(*owner, *window)))
    close_menu_locked();
}

void destroy_process_menus(KProcessObject &process) noexcept {
  for (uint32_t i = 0; i < kMaxMenus; ++i) {
    if (g_menus[i].used && g_menus[i].owner == &process)
      destroy_menu_locked(menu_handle_locked(g_menus[i]));
  }
}

kstd::error_code validate_menu_attachment_locked(const KProcessObject &caller,
                                                 uint32_t handle) noexcept {
  if (!handle) return {};
  const auto *menu = lookup_menu_locked(handle);
  if (!menu) return KernelError::InvalidHandle;
  if (menu->owner != &caller) return KernelError::AccessDenied;
  if (menu->popup) return KernelError::InvalidArgument;
  return {};
}

} // namespace kernel::user
