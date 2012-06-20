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

const Common::String EditRecordDialog::getDescription() {
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

GUI::EditRecordDialog::EditRecordDialog() : Dialog("EditRecordDialog") {
	new StaticTextWidget(this,"EditRecordDialog.AuthorLable",_("Author:"));
	new StaticTextWidget(this,"EditRecordDialog.NameLable",_("Name:"));
	new StaticTextWidget(this,"EditRecordDialog.NotesLable",_("Notes:"));
	_authorEdit = new EditTextWidget(this, "EditRecordDialog.AuthorEdit","");
	_notesEdit = new EditTextWidget(this, "EditRecordDialog.NotesEdit","");
	_nameEdit = new EditTextWidget(this, "EditRecordDialog.NameEdit","");
}

EditRecordDialog::~EditRecordDialog() {

}

}
