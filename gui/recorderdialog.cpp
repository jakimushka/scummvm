#include "common/EventRecorder.h"
#include "common/savefile.h"
#include "common/system.h"
#include "common/translation.h"
#include "graphics/scaler.h"
#include "gui/widgets/list.h"
#include "gui/message.h"
#include "gui/saveload.h"
#include "gui/ThemeEval.h"
#include "gui/gui-manager.h"
#include "recorderdialog.h"

#define MAX_RECORDS_NAMES 0xFF

namespace GUI {

enum {
	kRecordCmd = 'RCRD',
	kPlaybackCmd = 'PBCK',
	kDeleteCmd = 'DEL '
};

RecorderDialog::RecorderDialog() : Dialog("RecorderDialog"), _list(0) {
	_backgroundType = ThemeEngine::kDialogBackgroundSpecial;
	ButtonWidget *recordButton;
	ButtonWidget *playbackButton;
	_list = new GUI::ListWidget(this, "RecorderDialog.List");
	_list->setNumberingMode(GUI::kListNumberingZero);
	new GUI::ButtonWidget(this, "RecorderDialog.Delete", _("Delete"), 0, kDeleteCmd);
	new GUI::ButtonWidget(this, "RecorderDialog.Cancel", _("Cancel"), 0, kCloseCmd);
	recordButton = new GUI::ButtonWidget(this, "RecorderDialog.Record", _("Record"), 0, kRecordCmd);
	playbackButton = new GUI::ButtonWidget(this, "RecorderDialog.Playback", _("Playback"), 0, kPlaybackCmd);
	_gfxWidget = new GUI::GraphicsWidget(this, 0, 0, 10, 10);
	_container = new GUI::ContainerWidget(this, 0, 0, 10, 10);
	if (_gfxWidget)
		_gfxWidget->setGfx(0);
}


void RecorderDialog::reflowLayout() {
	if (g_gui.xmlEval()->getVar("Globals.RecorderDialog.ExtInfo.Visible") == 1) {
		int16 x = 200, y = 20;
		uint16 w = 180, h = 200;

		if (!g_gui.xmlEval()->getWidgetData("RecorderDialog.Thumbnail", x, y, w, h)) {
			error("Error when loading position data for Recorder Thumbnails");
		}

		int thumbW = kThumbnailWidth;
		int thumbH = kThumbnailHeight2;
		int thumbX = x + (w >> 1) - (thumbW >> 1);
		int thumbY = y + kLineHeight;

		_container->resize(x, y, w, h);
		_gfxWidget->resize(thumbX, thumbY, thumbW, thumbH);

		_container->setVisible(true);
		_gfxWidget->setVisible(true);
		updateSelection(false);
	} else {
		_container->setVisible(false);
		_gfxWidget->setVisible(false);
	}
	Dialog::reflowLayout();
}



void RecorderDialog::handleCommand(CommandSender *sender, uint32 cmd, uint32 data) {
	switch(cmd) {
	case kDeleteCmd:
		break;
	case kRecordCmd:
		g_eventRec.init(getRecordFileName(),  Common::EventRecorder::kRecorderRecord);
		break;
	case kPlaybackCmd:
		break;
	case kCloseCmd:
		setResult(-1);
	default:
		Dialog::handleCommand(sender, cmd, data);
	}
}

void RecorderDialog::updateList() {
	Common::SaveFileManager *saveFileMan = g_system->getSavefileManager();
	Common::String pattern(_target+".r??");
	Common::StringArray files = saveFileMan->listSavefiles(pattern);
	for (Common::StringArray::const_iterator file = files.begin(); file != files.end(); ++file) {
		_list->append(*file);
	}
}

int RecorderDialog::runModal(Common::String &target) {
	_target = target;
	updateList();
	return Dialog::runModal();
}

RecorderDialog::~RecorderDialog() {

}

void RecorderDialog::updateSelection(bool redraw) {
	_gfxWidget->setGfx(-1, -1, 0, 0, 0);
}

Common::String RecorderDialog::getRecordFileName() {
	ConfMan.getActiveDomainName();
	GUI::ListWidget::StringArray recordsList = _list->getList();
	for (int i = 0; i < MAX_RECORDS_NAMES; ++i) {
		Common::String recordName = Common::String::format("%s.r%02x", _target.c_str(), i);
		if (isStringInList(recordName)) {
			continue;
		}
		return recordName;
	}
	return "";
}


bool RecorderDialog::isStringInList(const Common::String &recordName) {
	for(GUI::ListWidget::StringArray::const_iterator iterator = _list->getList().begin(); iterator != _list->getList().end(); ++iterator) {
		if (recordName == *iterator) {
			return true;
		}
	}
	return false;
}

} // End of namespace GUI
