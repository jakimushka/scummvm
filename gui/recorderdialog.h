#ifndef GUI_RECORDER_DIALOG_H
#define GUI_RECORDER_DIALOG_H
#include "common/stream.h"
#include "gui/dialog.h"
namespace GUI {

class ListWidget;
class GraphicsWidget;
class ButtonWidget;
class CommandSender;
class ContainerWidget;
class StaticTextWidget;


class RecorderDialog : public GUI::Dialog {
private:
	Common::SeekableReadStream *_playbackFile;
	Common::String _target;
	Common::String _filename;
	int _currentScreenshot;
	int _screenShotsCount;
	GUI::ListWidget *_list;
	GUI::ContainerWidget *_container;
	GUI::GraphicsWidget *_gfxWidget;
	GUI::StaticTextWidget *_currentScreenshotText;
	void updateList();
	void updateScreenShotsText();
	void updateSelection(bool redraw);
	void updateScreenshot();
	int calculateScreenshotsCount();
	Common::String generateRecordFileName();
public:
	enum DialogResult {
		kRecordDialogClose,
		kRecordDialogRecord,
		kRecordDialogPlayback
	};
	RecorderDialog();
	~RecorderDialog();
	Graphics::Surface *getScreenShot(int number);
	bool skipToNextScreenshot();
	virtual void handleCommand(GUI::CommandSender *sender, uint32 cmd, uint32 data);
	int runModal(Common::String &target);
	virtual void reflowLayout();
	const Common::String getFileName() {return _filename;}
};

}  // End of namespace GUI


#endif GUI_RECORDER_DIALOG_H
