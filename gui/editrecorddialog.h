#ifndef GUI_EDITRECORDDIALOG_H
#define GUI_EDITRECORDDIALOG_H

#include "gui/dialog.h"

namespace GUI {

class EditTextWidget;
class StaticTextWidget;

class EditRecordDialog : public Dialog {
private:
	EditTextWidget *_notesEdit;
	EditTextWidget *_nameEdit;
	EditTextWidget *_authorEdit;
	EditRecordDialog() : Dialog("EditRecordDialog") {};
public:
	EditRecordDialog(const Common::String author, const Common::String name, const Common::String notes);
	~EditRecordDialog();
	const Common::String getAuthor();
	const Common::String getNotes();
	const Common::String getName();
	virtual void handleCommand(GUI::CommandSender *sender, uint32 cmd, uint32 data);
	void setAuthor(const Common::String &author);
	void setNotes(const Common::String &desc);
	void setName(const Common::String &name);
};

}// End of namespace GUI

#endif
