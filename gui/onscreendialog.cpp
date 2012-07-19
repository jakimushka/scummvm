#include "onscreendialog.h"
#include "gui/gui-manager.h"
#include "common/rect.h"
#include "common/system.h"

namespace GUI {

bool OnScreenDialog::isVisible() const {
	return true;
}

enum {
	kPlayCmd = 'PLAY'
};

void OnScreenDialog::reflowLayout() {
	GuiObject::reflowLayout();
}

void OnScreenDialog::releaseFocus() {
}

OnScreenDialog::OnScreenDialog(int x, int y, int w, int h) : Dialog(x, y, w, h) {
	GUI::ButtonWidget *btn = new GUI::ButtonWidget(this, "", "|>", 0, kPlayCmd);
	btn->resize(0,0,30,30);
	text = new GUI::StaticTextWidget(this, "", "00:00:00");
	text->resize(40,5,50,30);
	uint32 lastTime = 0;
}

void OnScreenDialog::handleCommand(CommandSender *sender, uint32 cmd, uint32 data) {
	switch (cmd) {
	case kPlayCmd:
		g_eventRec.togglePause();
		break;
	}
}

void OnScreenDialog::setReplayedTime(uint32 newTime) {
	uint32 delay = newTime - lastTime;
	if (newTime - lastTime > 1000) {
		uint32 seconds = newTime / 1000;
		text->setLabel(Common::String::format("%.2d:%.2d:%.2d", seconds / 3600 % 24, seconds / 60 % 60, seconds % 60));
		lastTime = newTime;
	}
}



}
