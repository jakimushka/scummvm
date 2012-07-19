#ifndef OVERLAY_WIDGET_H
#define OVERLAY_WIDGET_H

#include "gui/dialog.h"
#include "gui/widget.h"

namespace GUI {

class OnScreenDialog : public Dialog {

public:
	OnScreenDialog(int x, int y, int w, int h);
	virtual bool isVisible() const;
	virtual void reflowLayout();
	void handleCommand(CommandSender *sender, uint32 cmd, uint32 data);
protected:
	virtual void	releaseFocus();
};

} // End of namespace GUI

#endif
