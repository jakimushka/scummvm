#ifndef OVERLAY_WIDGET_H
#define OVERLAY_WIDGET_H

#include "gui/dialog.h"
#include "gui/widget.h"

namespace GUI {

class OnScreenDialog : public Dialog {
private:
	uint32 lastTime;
	bool _enableDrag;
	bool _mouseOver;
	Common::Point _dragPoint;
	GUI::StaticTextWidget *text;
	bool isMouseOver(int x, int y);
public:
	OnScreenDialog();
	~OnScreenDialog();
	virtual bool isVisible() const;
	virtual void reflowLayout();
	void setReplayedTime(uint32 newTime);
	virtual void handleMouseMoved(int x, int y, int button);
	virtual void handleMouseDown(int x, int y, int button, int clickCount);
	virtual void handleMouseUp(int x, int y, int button, int clickCount);
	void handleCommand(CommandSender *sender, uint32 cmd, uint32 data);
protected:
	virtual void	releaseFocus();
};

} // End of namespace GUI

#endif
