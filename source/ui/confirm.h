// S22 (U27): the one confirmation / error modal every "are you sure" and every save error uses
// (C# NGenericPopup / NVerticalPopup / NAbandonRunConfirmPopup / NErrorPopup).
//
// Layout (RGDSplus U27: the whole danger confirmation on the bottom screen): the top screen is
// dimmed, the bottom screen shows the popup plate with a gold title, the body and two buttons,
// 取消 (btn_cancel_s) left and 确认 (btn_ok_s) right, or one button for a notice. Focus starts on
// the safe choice (取消; the only button of a notice). D-pad left/right move it, A presses it,
// B cancels (closes a notice), a tap presses a button (released inside it). While open it owns
// the input: App::updateConfirm runs before every screen and the kit widgets are suspended.
//
// Call sites are one line: confirm::ask(title, body, [this] { ...on confirm... });
// Dialogs opened while one is up wait their turn (a queue); save errors reported through
// core/save_errors.h are turned into notices by App::updateConfirm.
#pragma once
#include <functional>
#include <string>

#include "res.h"

namespace ui::confirm {

struct Spec {
  std::string title, body;
  std::string ok = tr("确认", "Confirm");
  std::string cancel = tr("取消", "Cancel");      // "" = a notice with one button
  std::function<void()> onOk;        // may be empty
  std::function<void()> onCancel;    // may be empty
};

void open(Spec s);
// A yes / no question; `onOk` runs only when the player confirms.
void ask(const std::string& title, const std::string& body, std::function<void()> onOk,
         const std::string& ok = tr("确认", "Confirm"), const std::string& cancel = tr("取消", "Cancel"));
// A one-button notice (errors).
void notice(const std::string& title, const std::string& body, const std::string& ok = tr("了解了", "OK"),
            std::function<void()> onClose = {});

bool isOpen();
// True on a frame whose input the modal consumed (including the frame it closed): screens that
// read gfx::input() themselves in draw (settings) must ignore that frame's input.
bool inputTaken();
void clear();

}  // namespace ui::confirm
