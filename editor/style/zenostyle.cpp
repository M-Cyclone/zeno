#include "zenostyle.h"
#include "zstyleoption.h"
#include "../comctrl/ztoolbutton.h"
#include "../tmpwidgets/zobjectbutton.h"


ZenoStyle::ZenoStyle()
{

}

ZenoStyle::~ZenoStyle()
{

}

void ZenoStyle::drawPrimitive(PrimitiveElement pe, const QStyleOption* option, QPainter* painter, const QWidget* w) const
{
    switch (pe)
    {
        case PE_FrameTabWidget:
        {
            if (const QStyleOptionTabWidgetFrame* tab = qstyleoption_cast<const QStyleOptionTabWidgetFrame*>(option))
            {
                QStyleOptionTabWidgetFrame frameOpt = *tab;
                frameOpt.rect = w->rect();
                painter->fillRect(frameOpt.rect, QColor(58, 58, 58));
                //p->fillRect(frameOpt.rect, QColor(255, 0, 0));
                QRect contentsRect = subElementRect(SE_TabWidgetTabContents, &frameOpt, w);
                QRegion reg = option->rect;
                //reg -= contentsRect;
                //p->setClipRegion(reg);
                //p->fillRect(contentsRect, QColor(69, 69, 69));// QColor(58, 58, 58));
                //p->setClipRect(contentsRect);
                return;
            }
        }
        case PE_FrameMenu:
        {
            painter->fillRect(option->rect, QColor(51, 51, 51));
            return;
        }
        break;
    }
    return base::drawPrimitive(pe, option, painter, w);
}

void ZenoStyle::drawItemText(QPainter* painter, const QRect& rect, int flags, const QPalette& pal, bool enabled,
    const QString& text, QPalette::ColorRole textRole) const
{
    //painter->setFont(QFont("Calibre", 9));
    return base::drawItemText(painter, rect, flags, pal, enabled, text, textRole);
}

void ZenoStyle::drawControl(ControlElement element, const QStyleOption* opt, QPainter* p, const QWidget* w) const
{
    if (CE_MenuBarEmptyArea == element)
    {
        p->fillRect(opt->rect, QColor(58, 58, 58));
        return;
    }
    else if (CE_MenuBarItem == element)
    {
        if (const QStyleOptionMenuItem* mbi = qstyleoption_cast<const QStyleOptionMenuItem*>(opt))
        {
            QStyleOptionMenuItem optItem(*mbi);
            bool disabled = !(opt->state & State_Enabled);
            int alignment = Qt::AlignCenter | Qt::TextShowMnemonic | Qt::TextDontClip | Qt::TextSingleLine;
            QPalette::ColorRole textRole = disabled ? QPalette::Text : QPalette::ButtonText;

            if (opt->state & State_Selected)
            {
                if (opt->state & State_Sunken)
                {
                    p->fillRect(opt->rect, QColor(179, 102, 0));
                }
                else
                {
                    p->fillRect(opt->rect, QColor(71, 71, 71));
                }
            }
            else
            {
                p->fillRect(opt->rect, QColor(58, 58, 58));
            }

            optItem.palette.setBrush(QPalette::All, textRole, QColor(190, 190, 190));
            drawItemText(p, optItem.rect, alignment, optItem.palette, optItem.state & State_Enabled, optItem.text, textRole);
        }
        return;
    }
    else if (CE_TabBarTabShape == element)
    {
        if (const QStyleOptionTab* tab = qstyleoption_cast<const QStyleOptionTab*>(opt))
        {
            QRect rect(opt->rect);

            int rotate = 0;

            bool isDisabled = !(tab->state & State_Enabled);
            bool hasFocus = tab->state & State_HasFocus;
            bool isHot = tab->state & State_MouseOver;
            bool selected = tab->state & State_Selected;
            bool lastTab = tab->position == QStyleOptionTab::End;
            bool firstTab = tab->position == QStyleOptionTab::Beginning;
            bool onlyOne = tab->position == QStyleOptionTab::OnlyOneTab;
            bool leftAligned = proxy()->styleHint(SH_TabBar_Alignment, tab, w) == Qt::AlignLeft;
            bool centerAligned = proxy()->styleHint(SH_TabBar_Alignment, tab, w) == Qt::AlignCenter;
            int borderThickness = proxy()->pixelMetric(PM_DefaultFrameWidth, opt, w);
            int tabOverlap = proxy()->pixelMetric(PM_TabBarTabOverlap, opt, w);

            if (isDisabled)
            {

            }
            else if (selected)
            {

            }
            else if (hasFocus)
            {

            }
            else if (isHot)
            {

            }
            else
            {

            }

            // Selecting proper part depending on position
            if (firstTab || onlyOne) {
                if (leftAligned) {

                }
                else if (centerAligned) {

                }
                else { // rightAligned

                }
            }
            else {

            }

            if (tab->direction == Qt::RightToLeft
                && (tab->shape == QTabBar::RoundedNorth
                    || tab->shape == QTabBar::RoundedSouth)) {
                bool temp = firstTab;
                firstTab = lastTab;
                lastTab = temp;
            }
            bool begin = firstTab || onlyOne;
            bool end = lastTab || onlyOne;
            switch (tab->shape) {
            case QTabBar::RoundedNorth:
                if (selected)
                    rect.adjust(begin ? 0 : -tabOverlap, 0, end ? 0 : tabOverlap, borderThickness);
                else
                    rect.adjust(begin ? tabOverlap : 0, tabOverlap, end ? -tabOverlap : 0, 0);
                break;
            case QTabBar::RoundedSouth:
                //vMirrored = true;
                rotate = 180; // Not 100% correct, but works
                if (selected)
                    rect.adjust(begin ? 0 : -tabOverlap, -borderThickness, end ? 0 : tabOverlap, 0);
                else
                    rect.adjust(begin ? tabOverlap : 0, 0, end ? -tabOverlap : 0, -tabOverlap);
                break;
            case QTabBar::RoundedEast:
                rotate = 90;
                if (selected) {
                    rect.adjust(-borderThickness, begin ? 0 : -tabOverlap, 0, end ? 0 : tabOverlap);
                }
                else {
                    rect.adjust(0, begin ? tabOverlap : 0, -tabOverlap, end ? -tabOverlap : 0);
                }
                break;
            case QTabBar::RoundedWest:
                rotate = 90;
                if (selected) {
                    rect.adjust(0, begin ? 0 : -tabOverlap, borderThickness, end ? 0 : tabOverlap);
                }
                else {
                    rect.adjust(tabOverlap, begin ? tabOverlap : 0, 0, end ? -tabOverlap : 0);
                }
                break;
            default:
                // Do our own painting for triangular
                break;
            }

            if (!selected) {
                switch (tab->shape) {
                case QTabBar::RoundedNorth:
                    rect.adjust(0, 0, 0, -1);
                    break;
                case QTabBar::RoundedSouth:
                    rect.adjust(0, 1, 0, 0);
                    break;
                case QTabBar::RoundedEast:
                    rect.adjust(1, 0, 0, 0);
                    break;
                case QTabBar::RoundedWest:
                    rect.adjust(0, 0, -1, 0);
                    break;
                default:
                    break;
                }
            }

            p->fillRect(rect, selected ? QColor(69, 69, 69) : QColor(58, 58, 58));
            QPen pen(QColor(43, 43, 43));
            p->drawRect(rect);
            return;
        }
    }
    else if (CE_TabBarTabLabel == element)
    {
        if (const QStyleOptionTab *tab = qstyleoption_cast<const QStyleOptionTab *>(opt)) {
            QStyleOptionTab _tab(*tab);
            _tab.palette.setBrush(QPalette::WindowText, QColor(188,188,188));
            p->setFont(QFont("Microsoft YaHei", 9));
            return base::drawControl(element, &_tab, p, w);
        }
    }
    else if (CE_MenuItem == element)
    {
        return drawMenuItem(element, opt, p, w);
    }
    else if (CE_MenuEmptyArea == element)
    {
        if (const QStyleOptionMenuItem* menuitem = qstyleoption_cast<const QStyleOptionMenuItem*>(opt))
        {
            p->fillRect(opt->rect, QColor(58, 58, 58));
            return;
        }
    }
    return base::drawControl(element, opt, p, w);
}

QRect ZenoStyle::subControlRect(ComplexControl cc, const QStyleOptionComplex* option, SubControl sc, const QWidget* widget) const
{
    if (cc == CC_ZenoToolButton)
    {
        const ZStyleOptionToolButton* opt = qstyleoption_cast<const ZStyleOptionToolButton*>(option);
        Q_ASSERT(opt);

        switch (sc)
        {
        case SC_ZenoToolButtonIcon:
        {
            if (opt->buttonOpts & ZToolButton::Opt_TextUnderIcon)
            {
                int xleft = opt->rect.width() / 2 - opt->iconSize.width() / 2;
                int ytop = pixelMetric(static_cast<QStyle::PixelMetric>(ZenoStyle::PM_ButtonTopMargin), 0, widget);
                return QRect(xleft, ytop, opt->iconSize.width(), opt->iconSize.height());
            }
            else if (opt->buttonOpts & ZToolButton::Opt_TextRightToIcon)
            {
                return QRect(); //todo
            }
            else
            {
                int xpos = opt->rect.width() / 2 - opt->iconSize.width() / 2;
                int ypos = opt->rect.height() / 2 - opt->iconSize.height() / 2;
                return QRect(xpos, ypos, opt->iconSize.width(), opt->iconSize.height());
            }
            break;
        }
        case SC_ZenoToolButtonText:
        {
            if (opt->buttonOpts & ZToolButton::Opt_TextUnderIcon)
            {
                QFontMetrics fontMetrics(opt->font);
                int textWidth = fontMetrics.horizontalAdvance(opt->text);
                int textHeight = fontMetrics.height();
                int xleft = opt->rect.width() / 2 - textWidth / 2;
                int ypos = opt->rect.height() - textHeight - pixelMetric(static_cast<QStyle::PixelMetric>(ZenoStyle::PM_ButtonBottomMargin), 0, widget);
                return QRect(xleft, ypos, textWidth, textHeight);
            }
            else if (opt->buttonOpts & ZToolButton::Opt_TextRightToIcon)
            {
                return QRect(); //todo
            }
            else
            {
                return QRect();
            }
        }
        case SC_ZenoToolButtonArrow:
        {
            //todo
            return QRect();
        }
        }
    }
    return base::subControlRect(cc, option, sc, widget);
}

int ZenoStyle::styleHint(StyleHint sh, const QStyleOption* opt, const QWidget* w, QStyleHintReturn* shret) const
{
    return QProxyStyle::styleHint(sh, opt, w, shret);
}

int ZenoStyle::pixelMetric(PixelMetric m, const QStyleOption* option, const QWidget* widget) const
{
    if (qobject_cast<const ZMiniToolButton*>(widget))
    {
        switch (m)
        {
        case PM_ButtonLeftMargin:
        case PM_ButtonRightMargin:  return 6;
        case PM_ButtonTopMargin:
        case PM_ButtonBottomMargin: return 6;
        }
    }
    else if (qobject_cast<const ZToolButton*>(widget))
    {
        switch (m)
        {
        case PM_ButtonLeftMargin:
        case PM_ButtonRightMargin:  return 9;
        case PM_ButtonTopMargin:
        case PM_ButtonBottomMargin: return 4;
        }
    }
    switch (m)
    {
    case QStyle::PM_MenuPanelWidth: return 1;
    }
    return base::pixelMetric(m, option, widget);
}

QRect ZenoStyle::subElementRect(SubElement element, const QStyleOption* option, const QWidget* widget) const
{
    return base::subElementRect(element, option, widget);
}

void ZenoStyle::drawZenoLineEdit(PrimitiveElement pe, const QStyleOption* option, QPainter* painter, const QWidget* widget) const
{
    QColor clrBorder, clrBackground, clrForeground;

    //clrBorder = DrawerFunc::getColorFromWidget(widget, option->state, "border");
    //clrBackground = DrawerFunc::getColorFromWidget(widget, option->state, "background");
    //clrForeground = DrawerFunc::getColorFromWidget(widget, option->state, "foreground");

    painter->setPen(clrBorder);
    painter->setBrush(clrBackground);
    painter->drawRect(option->rect.adjusted(0, 0, -1, -1));
}

void ZenoStyle::drawDropdownArrow(QPainter* painter, QRect downArrowRect) const
{

}

void ZenoStyle::drawNewItemMenu(const QStyleOptionMenuItem* menuitem, QPainter* p, const QWidget* w) const
{

}

void ZenoStyle::drawMenuItem(ControlElement element, const QStyleOption* option, QPainter* painter, const QWidget* widget) const
{
    if (const QStyleOptionMenuItem* menuitem = qstyleoption_cast<const QStyleOptionMenuItem*>(option)) {
        // windows always has a check column, regardless whether we have an icon or not
        const qreal factor = 1;// QWindowsXPStylePrivate::nativeMetricScaleFactor(widget);
        int checkcol = qRound(qreal(25) * factor);
        const int gutterWidth = qRound(qreal(3) * factor);
        {
            const QSizeF size(16, 16);
            const QMarginsF margins(3,3,3,3);
            checkcol = qMax(menuitem->maxIconWidth, qRound(gutterWidth + size.width() + margins.left() + margins.right()));
        }
        QRect rect = option->rect;

        //draw vertical menu line
        if (option->direction == Qt::LeftToRight)
            checkcol += rect.x();
        QPoint p1 = QStyle::visualPos(option->direction, menuitem->rect, QPoint(checkcol, rect.top()));
        QPoint p2 = QStyle::visualPos(option->direction, menuitem->rect, QPoint(checkcol, rect.bottom()));
        QRect gutterRect(p1.x(), p1.y(), gutterWidth, p2.y() - p1.y() + 1);
        painter->fillRect(gutterRect, QColor(58, 58, 58));

        int x, y, w, h;
        menuitem->rect.getRect(&x, &y, &w, &h);
        int tab = menuitem->tabWidth;
        bool dis = !(menuitem->state & State_Enabled);
        bool checked = menuitem->checkType != QStyleOptionMenuItem::NotCheckable
            ? menuitem->checked : false;
        bool act = menuitem->state & State_Selected;

        if (menuitem->menuItemType == QStyleOptionMenuItem::Separator) {
            int yoff = y - 2 + h / 2;
            const int separatorSize = 0;// qRound(qreal(6) * QWindowsStylePrivate::nativeMetricScaleFactor(widget));
            QPoint p1 = QPoint(x + checkcol, yoff);
            QPoint p2 = QPoint(x + w + separatorSize, yoff);
            
            QPen pen(QColor(148, 148, 148));
            painter->setPen(pen);
            painter->fillRect(option->rect, QColor(58, 58, 58));
            painter->drawLine(p1, p2);
            return;
        }

        QRect vCheckRect = visualRect(option->direction, menuitem->rect, QRect(menuitem->rect.x(),
            menuitem->rect.y(), checkcol - (gutterWidth + menuitem->rect.x()), menuitem->rect.height()));

        if (act)
        {
            painter->fillRect(option->rect, QColor(179, 102, 0));
        }
        else
        {
            painter->fillRect(option->rect, QColor(58, 58, 58));
        }

        if (menuitem->checkType != QStyleOptionMenuItem::NotCheckable)
        {
            const QSizeF size(12, 12);
            const QMarginsF margins(0, 0, 0, 0);
            QRect checkRect(0, 0, qRound(size.width() + margins.left() + margins.right()),
                qRound(size.height() + margins.bottom() + margins.top()));
            checkRect.moveCenter(vCheckRect.center());
            QRect _checkRc = checkRect;

            QPen pen(QColor(148, 148, 148));
            painter->setPen(pen);
            painter->drawRect(checkRect);
            if (checked)
            {
                QIcon iconChecked(":/icons/checked.png");
                painter->drawPixmap(checkRect, iconChecked.pixmap(size.width(), size.height()));
            }
        }

        if (!menuitem->icon.isNull()) {
            QIcon::Mode mode = dis ? QIcon::Disabled : QIcon::Normal;
            if (act && !dis)
                mode = QIcon::Active;
            QPixmap pixmap;
            if (checked)
                pixmap = menuitem->icon.pixmap(proxy()->pixelMetric(PM_SmallIconSize, option, widget), mode, QIcon::On);
            else
                pixmap = menuitem->icon.pixmap(proxy()->pixelMetric(PM_SmallIconSize, option, widget), mode);
            const int pixw = pixmap.width() / pixmap.devicePixelRatio();
            const int pixh = pixmap.height() / pixmap.devicePixelRatio();
            QRect pmr(0, 0, pixw, pixh);
            pmr.moveCenter(vCheckRect.center());
            painter->setPen(menuitem->palette.text().color());
            painter->drawPixmap(pmr.topLeft(), pixmap);
        }

        const QColor textColor = QColor(200, 200, 200);// menuitem->palette.text().color();
        if (dis)
            painter->setPen(textColor);
        else
            painter->setPen(textColor);

        int xm = /*windowsItemFrame*/2 + checkcol + /*windowsItemHMargin*/3 + (gutterWidth - menuitem->rect.x()) - 1;
        int xpos = menuitem->rect.x() + xm;
        QRect textRect(xpos, y + /*windowsItemVMargin*/4, w - xm - /*windowsRightBorder*/15 - tab + 1, h - 2 * 4/*windowsItemVMargin*/);
        QRect vTextRect = visualRect(option->direction, menuitem->rect, textRect);
        QString s = menuitem->text;
        if (!s.isEmpty()) {    // draw text
            painter->save();
            int t = s.indexOf(QLatin1Char('\t'));
            int text_flags = Qt::AlignVCenter | Qt::TextShowMnemonic | Qt::TextDontClip | Qt::TextSingleLine;
            if (!proxy()->styleHint(SH_UnderlineShortcut, menuitem, widget))
                text_flags |= Qt::TextHideMnemonic;
            text_flags |= Qt::AlignLeft;
            if (t >= 0) {
                QRect vShortcutRect = visualRect(option->direction, menuitem->rect,
                    QRect(textRect.topRight(), QPoint(menuitem->rect.right(), textRect.bottom())));
                painter->drawText(vShortcutRect, text_flags, s.mid(t + 1));
                s = s.left(t);
            }
            QFont font = menuitem->font;
            if (menuitem->menuItemType == QStyleOptionMenuItem::DefaultItem)
                font.setBold(true);
            painter->setFont(font);
            painter->drawText(vTextRect, text_flags, s.left(t));
            painter->restore();
        }
        if (menuitem->menuItemType == QStyleOptionMenuItem::SubMenu) {// draw sub menu arrow
            int dim = (h - 2 * /*windowsItemFrame*/2) / 2;
            PrimitiveElement arrow;
            arrow = (option->direction == Qt::RightToLeft) ? PE_IndicatorArrowLeft : PE_IndicatorArrowRight;
            xpos = x + w - 6/*windowsArrowHMargin*/ - /*windowsItemFrame*/2 - dim;
            QRect  vSubMenuRect = visualRect(option->direction, menuitem->rect, QRect(xpos, y + h / 2 - dim / 2, dim, dim));
            QStyleOptionMenuItem newMI = *menuitem;
            newMI.rect = vSubMenuRect;
            newMI.state = dis ? State_None : State_Enabled;
            proxy()->drawPrimitive(arrow, &newMI, painter, widget);
        }
    }
}

void ZenoStyle::drawZenoToolButton(const ZStyleOptionToolButton* option, QPainter* painter, const QWidget* widget) const
{
    QStyle::ComplexControl cc = static_cast<QStyle::ComplexControl>(CC_ZenoToolButton);
    QRect rcIcon = subControlRect(cc, option, static_cast<QStyle::SubControl>(SC_ZenoToolButtonIcon), widget);
    QRect rcText = subControlRect(cc, option, static_cast<QStyle::SubControl>(SC_ZenoToolButtonText), widget);
    QRect rcArrow = subControlRect(cc, option, static_cast<QStyle::SubControl>(SC_ZenoToolButtonArrow), widget);

    //draw the background
    if (option->buttonEnabled && (option->state & (State_MouseOver | State_On)))
    {
        QRect rect = option->rect.adjusted(0, 0, -1, -1);
        //todo: round corner
        QBrush bgBrush = option->palette.brush(QPalette::Active, QPalette::Window);

        painter->fillRect(rect, bgBrush);

        //seems no need to draw a border.
        //painter->setPen(option->borderColor);   //todo: borderColor colorRole£¿
        //painter->drawRect(rect);
    }

    //draw icon 
    if (!option->icon.isNull())
    {
        const ZToolButton* pToolButton = qobject_cast<const ZToolButton*>(widget);
        QIcon::Mode mode;
        if (!option->buttonEnabled)
            mode = QIcon::Disabled;
        //else if (pToolButton->isPressed() || pToolButton->isChecked())
        //    mode = QIcon::Selected;
        else if (pToolButton->isHovered())
            mode = QIcon::Active;
        else
            mode = QIcon::Normal;

        option->icon.paint(painter, rcIcon, Qt::AlignCenter, mode);
    }

    //draw text
    if (!option->text.isEmpty())
    {
        QColor text_color = option->buttonEnabled ? option->palette.brush(QPalette::Active, QPalette::WindowText).color() : QColor();
        if (option->buttonOpts & ZToolButton::Opt_TextUnderIcon)
        {
            QStringList textList = option->text.split('\n');
            for (auto iter = textList.begin(); iter != textList.end(); iter++)
            {
                int height = option->fontMetrics.height();
                QString str = *iter;
                painter->save();
                painter->setFont(option->font);
                painter->setPen(text_color);
                painter->drawText(rcText, Qt::AlignHCenter | Qt::TextShowMnemonic, str);
                painter->restore();
            }
        }
        else
        {
            //todo
        }
    }
    
    //draw arrow
    if (option->m_arrowOption != ZStyleOptionToolButton::NO_ARROW)
    {
        if (option->m_arrowOption == ZStyleOptionToolButton::DOWNARROW)
        {
            //todo
        }
        else if (option->m_arrowOption == ZStyleOptionToolButton::RIGHTARROW)
        {
            //todo
        }
    }
}

void ZenoStyle::drawComplexControl(ComplexControl control, const QStyleOptionComplex* option, QPainter* painter, const QWidget* widget) const
{
    switch (control)
    {
    case CC_ZenoComboBox:
        return base::drawComplexControl(control, option, painter, widget);
    case CC_ZenoToolButton:
        if (const ZStyleOptionToolButton* opt = qstyleoption_cast<const ZStyleOptionToolButton*>(option))
        {
            drawZenoToolButton(opt, painter, widget);
            break;
        }
    default:
        return base::drawComplexControl(control, option, painter, widget);
    }
}