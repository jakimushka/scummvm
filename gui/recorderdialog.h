#ifndef GUI_RECORDER_DIALOG_H
#define GUI_RECORDER_DIALOG_H
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
	Common::String _target;
	GUI::ListWidget *_list;
	GUI::ContainerWidget *_container;
	GUI::GraphicsWidget *_gfxWidget;
	void updateList();
	void updateSelection(bool redraw);
	Common::String getRecordFileName();
	bool isStringInList(const Common::String &recordName);
public:
	RecorderDialog();
	~RecorderDialog();
	virtual void handleCommand(GUI::CommandSender *sender, uint32 cmd, uint32 data);
	int runModal(Common::String &target);
	virtual void reflowLayout();
};

}  // End of namespace GUI


#endif GUI_RECORDER_DIALOG_H
