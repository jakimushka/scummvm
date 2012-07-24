#include "onscreendialog.h"
#include "gui/gui-manager.h"
#include "common/rect.h"
#include "common/system.h"
#include "gui/editrecorddialog.h"

namespace GUI {

bool OnScreenDialog::isVisible() const {
	return true;
}

enum {
	kStopCmd = 'STOP',
	kEditCmd = 'Edit'
};

void OnScreenDialog::reflowLayout() {
	GuiObject::reflowLayout();
}

void OnScreenDialog::releaseFocus() {
}

OnScreenDialog::OnScreenDialog(int x, int y, int w, int h) : Dialog(x, y, w, h) {
	GUI::PicButtonWidget *btn = new PicButtonWidget(this, "", "|>", kStopCmd);
	btn->useThemeTransparency(true);
	btn->resize(0,0,32,32);
	btn->setGfx(g_gui.theme()->getImageSurface(ThemeEngine::kImageStopbtn));
	btn = new PicButtonWidget(this, "", "|>", kEditCmd);
	btn->useThemeTransparency(true);
	btn->resize(40,0,32,32);
	btn->setGfx(g_gui.theme()->getImageSurface(ThemeEngine::kImageEditbtn));
	text = new GUI::StaticTextWidget(this, "", "00:00:00");
	text->resize(80,5,50,30);
}

void OnScreenDialog::handleCommand(CommandSender *sender, uint32 cmd, uint32 data) {
	Common::Event eventRTL;
	EditRecordDialog dlg(g_eventRec.getAuthor(), g_eventRec.getName(), g_eventRec.getNotes());
	switch (cmd) {
	case kStopCmd:
		eventRTL.type = Common::EVENT_RTL;
		g_system->getEventManager()->pushEvent(eventRTL);
		close();
		break;
	case kEditCmd:
		close();
		g_gui.theme()->disable();
		dlg.runModal();
		g_eventRec.setAuthor(dlg.getAuthor());
		g_eventRec.setName(dlg.getName());
		g_eventRec.setNotes(dlg.getNotes());
		open();
		break;
	}
}

void OnScreenDialog::setReplayedTime(uint32 newTime) {
	if (newTime - lastTime > 1000) {
		uint32 seconds = newTime / 1000;
		text->setLabel(Common::String::format("%.2d:%.2d:%.2d", seconds / 3600 % 24, seconds / 60 % 60, seconds % 60));
		lastTime = newTime;
	}
}



}
