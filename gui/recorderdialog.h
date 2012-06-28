#ifndef GUI_RECORDER_DIALOG_H
#define GUI_RECORDER_DIALOG_H
#include "common/stream.h"
#include "common/recorderfile.h"
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
	Common::PlaybackFile _playbackFile;
	Common::String _target;
	Common::String _filename;
	int _currentScreenshot;
	int _screenShotsCount;
	Common::Array<Common::PlaybackFile::PlaybackFileHeader> _fileHeaders;
	GUI::ListWidget *_list;
	GUI::ContainerWidget *_container;
	GUI::GraphicsWidget *_gfxWidget;
	GUI::StaticTextWidget *_currentScreenshotText;
	GUI::StaticTextWidget *_authorText;
	GUI::StaticTextWidget *_notesText;
	void updateList();
	void updateScreenShotsText();
	void updateSelection(bool redraw);
	void updateScreenshot();
	int calculateScreenshotsCount();
	bool isFileNameExists(Common::String &filename);
	Common::String generateRecordFileName();
public:
	Common::String _author;
	Common::String _name;
	Common::String _notes;
	enum DialogResult {
		kRecordDialogClose,
		kRecordDialogRecord,
		kRecordDialogPlayback
	};
	RecorderDialog();
	~RecorderDialog();
	virtual void handleCommand(GUI::CommandSender *sender, uint32 cmd, uint32 data);
	int runModal(Common::String &target);
	virtual void reflowLayout();
	const Common::String getFileName() {return _filename;}
};

}  // End of namespace GUI


#endif
