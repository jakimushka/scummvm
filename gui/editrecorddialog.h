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
public:
	EditRecordDialog();
	~EditRecordDialog();
	const Common::String getAuthor();
	const Common::String getDescription();
	const Common::String getName();
	void setAuthor(const Common::String &author);
	void setNotes(const Common::String &desc);
	void setName(const Common::String &name);
};

}// End of namespace GUI

#endif
