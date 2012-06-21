#include "editrecorddialog.h"
#include "gui/widgets/edittext.h"
#include "common/translation.h"


namespace GUI {

const Common::String EditRecordDialog::getAuthor() {
	return _authorEdit->getEditString();
}

void EditRecordDialog::setAuthor(const Common::String &author) {
	_authorEdit->setEditString(author);
}

const Common::String EditRecordDialog::getNotes() {
	return _notesEdit->getEditString();
} 

void EditRecordDialog::setNotes(const Common::String &desc) {
	_notesEdit->setEditString(desc);
}

const Common::String EditRecordDialog::getName() {
	return _nameEdit->getEditString();
}

void EditRecordDialog::setName(const Common::String &name) {
	_nameEdit->setEditString(name);
}

EditRecordDialog::~EditRecordDialog() {
}

EditRecordDialog::EditRecordDialog(const Common::String author, const Common::String name, const Common::String notes) : Dialog("EditRecordDialog") {
	new StaticTextWidget(this,"EditRecordDialog.AuthorLable",_("Author:"));
	new StaticTextWidget(this,"EditRecordDialog.NameLable",_("Name:"));
	new StaticTextWidget(this,"EditRecordDialog.NotesLable",_("Notes:"));
	_authorEdit = new EditTextWidget(this, "EditRecordDialog.AuthorEdit","");
	_notesEdit = new EditTextWidget(this, "EditRecordDialog.NotesEdit","");
	_nameEdit = new EditTextWidget(this, "EditRecordDialog.NameEdit","");
	_authorEdit->setEditString(author);
	_notesEdit->setEditString(notes);
	_nameEdit->setEditString(name);
	new GUI::ButtonWidget(this, "EditRecordDialog.Cancel", _("Cancel"), 0, kCloseCmd);
	new GUI::ButtonWidget(this, "EditRecordDialog.OK", _("Ok"), 0, kOKCmd);
}

void EditRecordDialog::handleCommand(GUI::CommandSender *sender, uint32 cmd, uint32 data) {
	switch(cmd) {
	case kCloseCmd:
		setResult(kCloseCmd);
		close();
		break;
	case kOKCmd:
		setResult(kOKCmd);
		close();
		break;
	default:
		Dialog::handleCommand(sender, cmd, data);
		break;
	}
}

}
